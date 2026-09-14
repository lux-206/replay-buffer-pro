#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace ReplayBufferPro::Cloud {

class CredentialStore {
public:
  explicit CredentialStore(std::filesystem::path encryptedFile);
  bool set(const std::string &key, const std::string &value, std::string *error = nullptr);
  std::string get(const std::string &key, std::string *error = nullptr) const;
  bool remove(const std::string &key, std::string *error = nullptr);
  bool clear(std::string *error = nullptr);
  bool available() const;

private:
  bool read(std::map<std::string, std::string> *values, std::string *error) const;
  bool write(const std::map<std::string, std::string> &values, std::string *error) const;
  std::filesystem::path encryptedFile;
};

} // namespace ReplayBufferPro::Cloud
