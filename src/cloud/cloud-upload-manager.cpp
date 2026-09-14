#include "cloud/cloud-upload-manager.hpp"

#include "cloud/google-auth.hpp"
#include "cloud/google-drive-provider.hpp"
#include "utils/logger.hpp"

#include <QDate>
#include <QStandardPaths>
#include <QUuid>

#include <chrono>
#include <algorithm>
#include <ctime>

namespace ReplayBufferPro::Cloud {

std::filesystem::path CloudUploadManager::dataDirectory()
{
  const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (!root.isEmpty()) return std::filesystem::path(root.toStdWString()) / "OBSCloudClips";
  return std::filesystem::temp_directory_path() / "OBSCloudClips";
}

std::filesystem::path CloudUploadManager::temporaryDirectory() { return dataDirectory() / "temp"; }

std::string CloudUploadManager::datedRemoteFolder(const std::string &root)
{
  std::string clean = root.empty() ? "OBS Clips" : root;
  while (!clean.empty() && clean.back() == '/') clean.pop_back();
  return clean + "/" + QDate::currentDate().toString("yyyy-MM-dd").toStdString();
}

CloudUploadManager::CloudUploadManager()
    : baseDirectory(dataDirectory()), credentialStore(baseDirectory / "credentials.dat"),
      queue(baseDirectory / "queue.json")
{
  std::string error;
  std::filesystem::create_directories(temporaryDirectory());
  if (!loadCloudSettings(baseDirectory / "settings.json", &currentSettings, &error))
    Logger::warning("[CloudClips] Could not load settings: %s", error.c_str());
  if (!queue.load(&error)) Logger::warning("[CloudClips] Could not load queue: %s", error.c_str());
  Logger::info("[CloudClips] Loaded %zu pending upload(s)", queue.pendingCount());
  worker = std::thread([this] { workerLoop(); });
}

CloudUploadManager::~CloudUploadManager()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    accepting = false;
    stopping = true;
    persistLocked();
  }
  cv.notify_all();
  if (worker.joinable()) worker.join();
}

bool CloudUploadManager::enqueueClip(const std::filesystem::path &clip)
{
  CloudSettings snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex);
    snapshot = currentSettings;
    if (!accepting || !snapshot.enabled) return false;
  }
  if (!std::filesystem::is_regular_file(clip)) {
    Logger::warning("[CloudClips] Clip ready but file is missing");
    return false;
  }
  const std::string id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
  const std::filesystem::path jobDirectory = temporaryDirectory() / id;
  const std::filesystem::path staged = jobDirectory / clip.filename();
  std::error_code ec;
  std::filesystem::create_directories(jobDirectory, ec);
  if (!ec && snapshot.deleteLocalAfterUpload) {
    std::filesystem::rename(clip, staged, ec);
    if (ec) { ec.clear(); std::filesystem::copy_file(clip, staged, std::filesystem::copy_options::overwrite_existing, ec); }
  } else if (!ec) {
    std::filesystem::copy_file(clip, staged, std::filesystem::copy_options::overwrite_existing, ec);
  }
  if (ec) {
    Logger::error("[CloudClips] Could not stage clip: %s", ec.message().c_str());
    return false;
  }
  if (snapshot.deleteLocalAfterUpload && std::filesystem::exists(clip)) {
    std::error_code removeError;
    std::filesystem::remove(clip, removeError);
    if (removeError) Logger::warning("[CloudClips] Original remained after staging: %s", removeError.message().c_str());
  }
  UploadJob job;
  job.id = id;
  job.path = staged;
  job.remoteFolder = datedRemoteFolder(snapshot.destination);
  job.removeAfterUpload = true; // staged copies are always temporary
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!accepting) return false;
    queue.add(std::move(job));
    persistLocked();
  }
  Logger::info("[CloudClips] Queued upload: %s", staged.filename().string().c_str());
  cv.notify_one();
  return true;
}

CloudSettings CloudUploadManager::settings() const { std::lock_guard<std::mutex> lock(mutex); return currentSettings; }

bool CloudUploadManager::updateSettings(const CloudSettings &settings, std::string *error)
{
  CloudSettings normalized = settings;
  normalized.concurrentUploads = 1;
  if (normalized.destination.empty()) normalized.destination = "OBS Clips";
  if (!saveCloudSettings(baseDirectory / "settings.json", normalized, error)) return false;
  { std::lock_guard<std::mutex> lock(mutex); currentSettings = normalized; }
  cv.notify_all();
  return true;
}

bool CloudUploadManager::connectGoogle(const std::string &clientId, const std::string &clientSecret,
                                       QWidget *parent, std::string *error)
{
  GoogleAuth auth(credentialStore);
  const bool connected = auth.connectAccount(clientId, clientSecret, parent, error);
  if (connected) cv.notify_all();
  return connected;
}

bool CloudUploadManager::disconnectGoogle(std::string *error) { GoogleAuth auth(credentialStore); return auth.disconnect(error); }
bool CloudUploadManager::isConnected() const { GoogleAuth auth(const_cast<CredentialStore &>(credentialStore)); return auth.isConnected(); }
std::size_t CloudUploadManager::pendingCount() const { std::lock_guard<std::mutex> lock(mutex); return queue.pendingCount(); }

void CloudUploadManager::persistLocked()
{
  std::string error;
  if (!queue.save(&error)) Logger::error("[CloudClips] Could not persist queue: %s", error.c_str());
}

void CloudUploadManager::workerLoop()
{
  GoogleDriveProvider provider(credentialStore);
  for (;;) {
    UploadJob job;
    std::size_t index = 0;
    CloudSettings settingsSnapshot;
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait_for(lock, std::chrono::seconds(1), [this] {
        return stopping || (currentSettings.enabled && queue.nextReady(std::time(nullptr)).has_value());
      });
      if (stopping) { persistLocked(); return; }
      const auto ready = currentSettings.enabled ? queue.nextReady(std::time(nullptr)) : std::nullopt;
      if (!ready) continue;
      index = *ready;
      queue.jobs()[index].status = UploadStatus::Uploading;
      job = queue.jobs()[index];
      settingsSnapshot = currentSettings;
      persistLocked();
    }
    Logger::info("[CloudClips] Upload started: %s", job.path.filename().string().c_str());
    UploadResult result = provider.upload(job.path, job.remoteFolder);
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = std::find_if(queue.jobs().begin(), queue.jobs().end(), [&](const UploadJob &item) { return item.id == job.id; });
      if (it == queue.jobs().end()) continue;
      if (result.success) {
        it->status = UploadStatus::Completed;
        std::error_code ec;
        if (it->removeAfterUpload) std::filesystem::remove(it->path, ec);
        std::filesystem::remove(it->path.parent_path(), ec);
        Logger::info("[CloudClips] Remote file verified; local temporary file removed");
      } else {
        it->retryCount++;
        it->lastError = result.error;
        if (settingsSnapshot.retryFailedUploads) {
          it->status = UploadStatus::RetryWaiting;
          it->retryAtEpochSeconds = std::time(nullptr) + retryDelaySeconds(it->retryCount);
          Logger::warning("[CloudClips] Upload failed; retry scheduled in %ds", retryDelaySeconds(it->retryCount));
        } else {
          it->status = UploadStatus::Failed;
          Logger::error("[CloudClips] Upload failed; local clip kept");
        }
      }
      persistLocked();
    }
  }
}

} // namespace ReplayBufferPro::Cloud
