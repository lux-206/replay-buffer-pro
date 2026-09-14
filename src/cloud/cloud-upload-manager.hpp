#pragma once

#include "cloud/cloud-settings.hpp"
#include "cloud/credential-store.hpp"
#include "cloud/upload-queue.hpp"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

class QWidget;

namespace ReplayBufferPro::Cloud {

class CloudUploadManager {
public:
  CloudUploadManager();
  ~CloudUploadManager();
  CloudUploadManager(const CloudUploadManager &) = delete;
  CloudUploadManager &operator=(const CloudUploadManager &) = delete;

  bool enqueueClip(const std::filesystem::path &clip);
  CloudSettings settings() const;
  bool updateSettings(const CloudSettings &settings, std::string *error = nullptr);
  bool connectGoogle(const std::string &clientId, const std::string &clientSecret,
                     QWidget *parent, std::string *error);
  bool disconnectGoogle(std::string *error);
  bool isConnected() const;
  std::size_t pendingCount() const;

  static std::filesystem::path dataDirectory();
  static std::filesystem::path temporaryDirectory();
  static std::string datedRemoteFolder(const std::string &root);

private:
  void workerLoop();
  void persistLocked();

  std::filesystem::path baseDirectory;
  CredentialStore credentialStore;
  UploadQueue queue;
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::thread worker;
  CloudSettings currentSettings;
  bool stopping = false;
  bool accepting = true;
};

} // namespace ReplayBufferPro::Cloud
