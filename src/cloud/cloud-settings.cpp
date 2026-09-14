#include "cloud/cloud-settings.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ReplayBufferPro::Cloud {

bool loadCloudSettings(const std::filesystem::path &path, CloudSettings *settings, std::string *error)
{
  QFile file(QString::fromStdWString(path.wstring()));
  if (!file.exists()) return true;
  if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString().toStdString(); return false; }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) { if (error) *error = "Invalid cloud settings JSON"; return false; }
  const QJsonObject object = document.object();
  settings->enabled = object.value("enabled").toBool(false);
  settings->destination = object.value("destination").toString("OBS Clips").toStdString();
  settings->deleteLocalAfterUpload = object.value("deleteLocalAfterUpload").toBool(true);
  settings->retryFailedUploads = object.value("retryFailedUploads").toBool(true);
  settings->concurrentUploads = 1;
  return true;
}

bool saveCloudSettings(const std::filesystem::path &path, const CloudSettings &settings, std::string *error)
{
  std::filesystem::create_directories(path.parent_path());
  QSaveFile file(QString::fromStdWString(path.wstring()));
  QJsonObject object{{"enabled", settings.enabled},
                     {"provider", "Google Drive"},
                     {"destination", QString::fromStdString(settings.destination)},
                     {"deleteLocalAfterUpload", settings.deleteLocalAfterUpload},
                     {"retryFailedUploads", settings.retryFailedUploads},
                     {"concurrentUploads", 1}};
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson()) < 0 || !file.commit()) {
    if (error) *error = file.errorString().toStdString();
    return false;
  }
  return true;
}

} // namespace ReplayBufferPro::Cloud
