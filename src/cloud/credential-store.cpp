#include "cloud/credential-store.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace ReplayBufferPro::Cloud {

CredentialStore::CredentialStore(std::filesystem::path path) : encryptedFile(std::move(path)) {}

bool CredentialStore::available() const
{
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

bool CredentialStore::read(std::map<std::string, std::string> *values, std::string *error) const
{
  values->clear();
  QFile file(QString::fromStdWString(encryptedFile.wstring()));
  if (!file.exists()) return true;
  if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString().toStdString(); return false; }
#ifdef _WIN32
  const QByteArray cipher = file.readAll();
  DATA_BLOB input{static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE *>(const_cast<char *>(cipher.data()))};
  DATA_BLOB output{};
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    if (error) *error = "Windows DPAPI could not decrypt credentials";
    return false;
  }
  const QByteArray plain(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
  LocalFree(output.pbData);
  const QJsonDocument document = QJsonDocument::fromJson(plain);
  if (!document.isObject()) { if (error) *error = "Invalid encrypted credential data"; return false; }
  for (auto it = document.object().begin(); it != document.object().end(); ++it)
    (*values)[it.key().toStdString()] = it.value().toString().toStdString();
  return true;
#else
  if (error) *error = "Secure credential storage is only available on Windows in this release";
  return false;
#endif
}

bool CredentialStore::write(const std::map<std::string, std::string> &values, std::string *error) const
{
#ifdef _WIN32
  QJsonObject object;
  for (const auto &[key, value] : values) object[QString::fromStdString(key)] = QString::fromStdString(value);
  const QByteArray plain = QJsonDocument(object).toJson(QJsonDocument::Compact);
  DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
  DATA_BLOB output{};
  if (!CryptProtectData(&input, L"Replay Buffer Pro Cloud Credentials", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output)) {
    if (error) *error = "Windows DPAPI could not encrypt credentials";
    return false;
  }
  std::filesystem::create_directories(encryptedFile.parent_path());
  QSaveFile file(QString::fromStdWString(encryptedFile.wstring()));
  const bool ok = file.open(QIODevice::WriteOnly) &&
                  file.write(reinterpret_cast<const char *>(output.pbData), output.cbData) == static_cast<qint64>(output.cbData) &&
                  file.commit();
  LocalFree(output.pbData);
  if (!ok && error) *error = file.errorString().toStdString();
  return ok;
#else
  (void)values;
  if (error) *error = "Secure credential storage is only available on Windows in this release";
  return false;
#endif
}

bool CredentialStore::set(const std::string &key, const std::string &value, std::string *error)
{
  std::map<std::string, std::string> values;
  if (!read(&values, error)) return false;
  values[key] = value;
  return write(values, error);
}

std::string CredentialStore::get(const std::string &key, std::string *error) const
{
  std::map<std::string, std::string> values;
  if (!read(&values, error)) return {};
  const auto it = values.find(key);
  return it == values.end() ? std::string{} : it->second;
}

bool CredentialStore::remove(const std::string &key, std::string *error)
{
  std::map<std::string, std::string> values;
  if (!read(&values, error)) return false;
  values.erase(key);
  return write(values, error);
}

bool CredentialStore::clear(std::string *error)
{
  if (!std::filesystem::exists(encryptedFile)) return true;
  std::error_code ec;
  std::filesystem::remove(encryptedFile, ec);
  if (ec && error) *error = ec.message();
  return !ec;
}

} // namespace ReplayBufferPro::Cloud
