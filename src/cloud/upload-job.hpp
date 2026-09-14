#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ReplayBufferPro::Cloud {

enum class UploadStatus { Pending, Uploading, Completed, Failed, RetryWaiting };

struct UploadJob {
  std::string id;
  std::filesystem::path path;
  std::string remoteFolder;
  UploadStatus status = UploadStatus::Pending;
  int retryCount = 0;
  std::int64_t retryAtEpochSeconds = 0;
  bool removeAfterUpload = true;
  std::string lastError;
};

const char *toString(UploadStatus status);
UploadStatus uploadStatusFromString(const std::string &value);
int retryDelaySeconds(int retryCount);

} // namespace ReplayBufferPro::Cloud
