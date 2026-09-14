#include "cloud/google-drive-provider.hpp"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrlQuery>

namespace ReplayBufferPro::Cloud {
namespace {

struct HttpResponse {
  int status = 0;
  QByteArray body;
  QByteArray location;
  QString error;
};

HttpResponse request(const QByteArray &method, const QUrl &url, const QString &token,
                     const QByteArray &contentType = {}, const QByteArray &body = {},
                     QIODevice *device = nullptr)
{
  QNetworkAccessManager network;
  QNetworkRequest request(url);
  if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
  if (!contentType.isEmpty()) request.setRawHeader("Content-Type", contentType);
  QNetworkReply *reply = nullptr;
  if (method == "GET") reply = network.get(request);
  else if (method == "POST") reply = network.post(request, body);
  else if (method == "PUT" && device) reply = network.put(request, device);
  else reply = network.sendCustomRequest(request, method, body);

  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  if (device) QObject::connect(reply, &QNetworkReply::uploadProgress, &timeout,
                               [&timeout](qint64, qint64) { timeout.start(120000); });
  timeout.start(120000);
  loop.exec();

  HttpResponse response;
  response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  response.body = reply->readAll();
  response.location = reply->rawHeader("Location");
  response.error = reply->errorString();
  reply->deleteLater();
  return response;
}

QString escapedDriveLiteral(QString value) { return value.replace("\\", "\\\\").replace("'", "\\'"); }

} // namespace

GoogleDriveProvider::GoogleDriveProvider(CredentialStore &credentialStore) : store(credentialStore) {}

bool GoogleDriveProvider::authenticate(std::string *error)
{
  const std::string expires = store.get("expires_at", error);
  const std::string savedAccess = store.get("access_token", error);
  try {
    if (!savedAccess.empty() && !expires.empty() && std::stoll(expires) > QDateTime::currentSecsSinceEpoch()) {
      accessToken = QString::fromStdString(savedAccess);
      return true;
    }
  } catch (const std::exception &) {}
  return refreshAccessToken(error);
}

bool GoogleDriveProvider::refreshAccessToken(std::string *error)
{
  const QString clientId = QString::fromStdString(store.get("client_id", error));
  const QString clientSecret = QString::fromStdString(store.get("client_secret", error));
  const QString refreshToken = QString::fromStdString(store.get("refresh_token", error));
  if (clientId.isEmpty() || clientSecret.isEmpty() || refreshToken.isEmpty()) {
    if (error) *error = "Google account is not connected";
    return false;
  }
  QUrlQuery form;
  form.addQueryItem("client_id", clientId);
  form.addQueryItem("client_secret", clientSecret);
  form.addQueryItem("refresh_token", refreshToken);
  form.addQueryItem("grant_type", "refresh_token");
  const HttpResponse response = request("POST", QUrl("https://oauth2.googleapis.com/token"), {},
                                        "application/x-www-form-urlencoded",
                                        form.query(QUrl::FullyEncoded).toUtf8());
  const QJsonObject object = QJsonDocument::fromJson(response.body).object();
  accessToken = object.value("access_token").toString();
  if (response.status != 200 || accessToken.isEmpty()) {
    if (error) *error = "Google token refresh failed (HTTP " + std::to_string(response.status) + ")";
    return false;
  }
  const qint64 expiresAt = QDateTime::currentSecsSinceEpoch() + object.value("expires_in").toInt(3600) - 60;
  return store.set("access_token", accessToken.toStdString(), error) &&
         store.set("expires_at", std::to_string(expiresAt), error);
}

QString GoogleDriveProvider::ensureFolderPath(const std::string &remoteFolder, std::string *error)
{
  QString parent = "root";
  const QStringList parts = QString::fromStdString(remoteFolder).split('/', Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    QUrl listUrl("https://www.googleapis.com/drive/v3/files");
    QUrlQuery query;
    query.addQueryItem("q", QString("name = '%1' and mimeType = 'application/vnd.google-apps.folder' and '%2' in parents and trashed = false")
                             .arg(escapedDriveLiteral(part), parent));
    query.addQueryItem("fields", "files(id,name)");
    query.addQueryItem("spaces", "drive");
    listUrl.setQuery(query);
    HttpResponse response = request("GET", listUrl, accessToken);
    if (response.status == 401 && refreshAccessToken(error)) response = request("GET", listUrl, accessToken);
    if (response.status != 200) { if (error) *error = "Drive folder lookup failed"; return {}; }
    const QJsonArray files = QJsonDocument::fromJson(response.body).object().value("files").toArray();
    if (!files.isEmpty()) { parent = files.first().toObject().value("id").toString(); continue; }
    const QJsonObject metadata{{"name", part}, {"mimeType", "application/vnd.google-apps.folder"},
                               {"parents", QJsonArray{parent}}};
    response = request("POST", QUrl("https://www.googleapis.com/drive/v3/files?fields=id"), accessToken,
                       "application/json; charset=UTF-8", QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    parent = QJsonDocument::fromJson(response.body).object().value("id").toString();
    if (response.status != 200 || parent.isEmpty()) { if (error) *error = "Drive folder creation failed"; return {}; }
  }
  return parent;
}

UploadResult GoogleDriveProvider::upload(const std::filesystem::path &file, const std::string &remoteFolder)
{
  UploadResult result;
  if (!std::filesystem::exists(file)) { result.error = "Local file no longer exists"; return result; }
  if (!authenticate(&result.error)) return result;
  const QString parent = ensureFolderPath(remoteFolder, &result.error);
  if (parent.isEmpty()) return result;

  const QFileInfo info(QString::fromStdWString(file.wstring()));
  const QJsonObject metadata{{"name", info.fileName()}, {"parents", QJsonArray{parent}}};
  QUrl sessionUrl("https://www.googleapis.com/upload/drive/v3/files");
  QUrlQuery sessionQuery;
  sessionQuery.addQueryItem("uploadType", "resumable");
  sessionQuery.addQueryItem("fields", "id,size,name");
  sessionUrl.setQuery(sessionQuery);
  HttpResponse response = request("POST", sessionUrl, accessToken, "application/json; charset=UTF-8",
                                  QJsonDocument(metadata).toJson(QJsonDocument::Compact));
  if (response.status != 200 || response.location.isEmpty()) {
    result.error = "Could not create resumable upload session (HTTP " + std::to_string(response.status) + ")";
    return result;
  }

  QFile input(info.absoluteFilePath());
  if (!input.open(QIODevice::ReadOnly)) { result.error = input.errorString().toStdString(); return result; }
  const QByteArray mime = QMimeDatabase().mimeTypeForFile(info).name().toUtf8();
  response = request("PUT", QUrl(QString::fromUtf8(response.location)), accessToken, mime, {}, &input);
  input.close();
  const QJsonObject uploaded = QJsonDocument::fromJson(response.body).object();
  result.fileId = uploaded.value("id").toString().toStdString();
  if ((response.status != 200 && response.status != 201) || result.fileId.empty()) {
    result.error = "Resumable upload failed (HTTP " + std::to_string(response.status) + ")";
    return result;
  }

  QUrl verifyUrl("https://www.googleapis.com/drive/v3/files/" + QString::fromStdString(result.fileId));
  QUrlQuery verifyQuery; verifyQuery.addQueryItem("fields", "id,size,trashed"); verifyUrl.setQuery(verifyQuery);
  response = request("GET", verifyUrl, accessToken);
  const QJsonObject verified = QJsonDocument::fromJson(response.body).object();
  result.remoteSize = verified.value("size").toString().toULongLong();
  const auto localSize = std::filesystem::file_size(file);
  if (response.status != 200 || verified.value("trashed").toBool() || result.remoteSize != localSize) {
    result.error = "Remote verification failed: size mismatch or missing file";
    return result;
  }
  result.success = true;
  return result;
}

} // namespace ReplayBufferPro::Cloud
