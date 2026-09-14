#pragma once

#include <filesystem>
#include <string>

namespace ReplayBufferPro::Cloud {

struct CloudSettings {
  bool enabled = false;
  std::string destination = "OBS Clips";
  bool deleteLocalAfterUpload = true;
  bool retryFailedUploads = true;
  int concurrentUploads = 1;
};

bool loadCloudSettings(const std::filesystem::path &path, CloudSettings *settings, std::string *error = nullptr);
bool saveCloudSettings(const std::filesystem::path &path, const CloudSettings &settings, std::string *error = nullptr);

} // namespace ReplayBufferPro::Cloud
