#pragma once

#include "cloud/cloud-provider.hpp"
#include "cloud/credential-store.hpp"

#include <QString>

namespace ReplayBufferPro::Cloud {

class GoogleDriveProvider : public ICloudProvider {
public:
  explicit GoogleDriveProvider(CredentialStore &store);
  bool authenticate(std::string *error) override;
  UploadResult upload(const std::filesystem::path &file, const std::string &remoteFolder) override;

private:
  bool refreshAccessToken(std::string *error);
  QString ensureFolderPath(const std::string &remoteFolder, std::string *error);
  CredentialStore &store;
  QString accessToken;
};

} // namespace ReplayBufferPro::Cloud
