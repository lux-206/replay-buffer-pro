#pragma once

#include "cloud/credential-store.hpp"

#include <string>

class QWidget;

namespace ReplayBufferPro::Cloud {

class GoogleAuth {
public:
  explicit GoogleAuth(CredentialStore &store);
  bool connectAccount(const std::string &clientId, const std::string &clientSecret,
                      QWidget *parent, std::string *error);
  bool disconnect(std::string *error);
  bool isConnected() const;

private:
  CredentialStore &store;
};

} // namespace ReplayBufferPro::Cloud
