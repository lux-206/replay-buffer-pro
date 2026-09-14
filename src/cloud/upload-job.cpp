#include "cloud/upload-job.hpp"

#include <algorithm>

namespace ReplayBufferPro::Cloud {

const char *toString(UploadStatus status)
{
  switch (status) {
  case UploadStatus::Pending: return "Pending";
  case UploadStatus::Uploading: return "Uploading";
  case UploadStatus::Completed: return "Completed";
  case UploadStatus::Failed: return "Failed";
  case UploadStatus::RetryWaiting: return "RetryWaiting";
  }
  return "Pending";
}

UploadStatus uploadStatusFromString(const std::string &value)
{
  if (value == "Uploading") return UploadStatus::Uploading;
  if (value == "Completed") return UploadStatus::Completed;
  if (value == "Failed") return UploadStatus::Failed;
  if (value == "RetryWaiting") return UploadStatus::RetryWaiting;
  return UploadStatus::Pending;
}

int retryDelaySeconds(int retryCount)
{
  static constexpr int delays[] = {5, 15, 30, 60, 300};
  return delays[std::clamp(retryCount - 1, 0, 4)];
}

} // namespace ReplayBufferPro::Cloud
