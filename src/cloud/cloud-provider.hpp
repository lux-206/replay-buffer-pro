#pragma once

#include <filesystem>
#include <string>

namespace ReplayBufferPro::Cloud {

struct UploadResult {
  bool success = false;
  std::string fileId;
  std::string error;
  std::uintmax_t remoteSize = 0;
};

class ICloudProvider {
public:
  virtual ~ICloudProvider() = default;
  virtual bool authenticate(std::string *error) = 0;
  virtual UploadResult upload(const std::filesystem::path &file,
                              const std::string &remoteFolder) = 0;
};

} // namespace ReplayBufferPro::Cloud
