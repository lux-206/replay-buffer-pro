#include "cloud/upload-queue.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ReplayBufferPro::Cloud {

UploadQueue::UploadQueue(std::filesystem::path path) : persistencePath(std::move(path)) {}

bool UploadQueue::load(std::string *error)
{
  items.clear();
  QFile file(QString::fromStdWString(persistencePath.wstring()));
  if (!file.exists()) return true;
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) *error = file.errorString().toStdString();
    return false;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error) *error = parseError.errorString().toStdString();
    return false;
  }
  const QJsonArray savedJobs = document.object().value("jobs").toArray();
  for (const QJsonValue value : savedJobs) {
    const QJsonObject object = value.toObject();
    UploadJob job;
    job.id = object.value("id").toString().toStdString();
    job.path = object.value("path").toString().toStdWString();
    job.remoteFolder = object.value("remoteFolder").toString().toStdString();
    job.status = uploadStatusFromString(object.value("status").toString().toStdString());
    if (job.status == UploadStatus::Uploading) job.status = UploadStatus::Pending;
    job.retryCount = object.value("retryCount").toInt();
    job.retryAtEpochSeconds = static_cast<std::int64_t>(object.value("retryAt").toDouble());
    job.removeAfterUpload = object.value("removeAfterUpload").toBool(true);
    job.lastError = object.value("lastError").toString().toStdString();
    if (std::filesystem::exists(job.path)) items.push_back(std::move(job));
  }
  return true;
}

bool UploadQueue::save(std::string *error) const
{
  QJsonArray jobsArray;
  for (const UploadJob &job : items) {
    if (job.status == UploadStatus::Completed) continue;
    QJsonObject object;
    object["id"] = QString::fromStdString(job.id);
    object["path"] = QString::fromStdWString(job.path.wstring());
    object["remoteFolder"] = QString::fromStdString(job.remoteFolder);
    object["status"] = toString(job.status);
    object["retryCount"] = job.retryCount;
    object["retryAt"] = static_cast<double>(job.retryAtEpochSeconds);
    object["removeAfterUpload"] = job.removeAfterUpload;
    object["lastError"] = QString::fromStdString(job.lastError);
    jobsArray.append(object);
  }
  std::filesystem::create_directories(persistencePath.parent_path());
  QSaveFile file(QString::fromStdWString(persistencePath.wstring()));
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject{{"jobs", jobsArray}}).toJson()) < 0 || !file.commit()) {
    if (error) *error = file.errorString().toStdString();
    return false;
  }
  return true;
}

void UploadQueue::add(UploadJob job) { items.push_back(std::move(job)); }

std::optional<std::size_t> UploadQueue::nextReady(std::int64_t now) const
{
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto &job = items[i];
    if (job.status == UploadStatus::Pending ||
        (job.status == UploadStatus::RetryWaiting && job.retryAtEpochSeconds <= now)) return i;
  }
  return std::nullopt;
}

std::size_t UploadQueue::pendingCount() const
{
  std::size_t count = 0;
  for (const auto &job : items) if (job.status != UploadStatus::Completed) ++count;
  return count;
}

} // namespace ReplayBufferPro::Cloud
