#include "cloud/google-auth.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QUrlQuery>

namespace ReplayBufferPro::Cloud {
namespace {

QByteArray base64Url(const QByteArray &value)
{
  return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray randomVerifier()
{
  QByteArray bytes(48, Qt::Uninitialized);
  for (int i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
  return base64Url(bytes);
}

} // namespace

GoogleAuth::GoogleAuth(CredentialStore &credentialStore) : store(credentialStore) {}

bool GoogleAuth::connectAccount(const std::string &clientId, const std::string &clientSecret,
                                QWidget *parent, std::string *error)
{
  (void)parent;
  if (!store.available()) { if (error) *error = "Secure credential storage is unavailable"; return false; }
  if (clientId.empty() || clientSecret.empty()) { if (error) *error = "OAuth client ID and client secret are required"; return false; }

  QTcpServer server;
  if (!server.listen(QHostAddress::LocalHost, 0)) { if (error) *error = server.errorString().toStdString(); return false; }
  const QString redirect = QString("http://127.0.0.1:%1/oauth/callback").arg(server.serverPort());
  const QByteArray verifier = randomVerifier();
  const QByteArray challenge = base64Url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
  const QString state = QString::fromLatin1(randomVerifier().left(32));

  QUrl authorization("https://accounts.google.com/o/oauth2/v2/auth");
  QUrlQuery query;
  query.addQueryItem("client_id", QString::fromStdString(clientId));
  query.addQueryItem("redirect_uri", redirect);
  query.addQueryItem("response_type", "code");
  query.addQueryItem("scope", "https://www.googleapis.com/auth/drive.file");
  query.addQueryItem("access_type", "offline");
  query.addQueryItem("prompt", "consent");
  query.addQueryItem("code_challenge", QString::fromLatin1(challenge));
  query.addQueryItem("code_challenge_method", "S256");
  query.addQueryItem("state", state);
  authorization.setQuery(query);
  if (!QDesktopServices::openUrl(authorization)) { if (error) *error = "Could not open the default browser"; return false; }

  QEventLoop callbackLoop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &callbackLoop, &QEventLoop::quit);
  QObject::connect(&server, &QTcpServer::newConnection, &callbackLoop, &QEventLoop::quit);
  timeout.start(180000);
  callbackLoop.exec();
  if (!server.hasPendingConnections()) { if (error) *error = "OAuth callback timed out"; return false; }

  QTcpSocket *socket = server.nextPendingConnection();
  if (!socket->waitForReadyRead(5000)) { if (error) *error = "OAuth callback was incomplete"; socket->deleteLater(); return false; }
  const QByteArray requestLine = socket->readLine();
  const QList<QByteArray> parts = requestLine.split(' ');
  const QUrl callback(parts.size() > 1 ? QString::fromUtf8(parts[1]) : QString());
  const QUrlQuery callbackQuery(callback);
  const QString code = callbackQuery.queryItemValue("code");
  const bool validState = callbackQuery.queryItemValue("state") == state;
  const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                              "<html><body><h2>Replay Buffer Pro connected.</h2>You can close this tab.</body></html>";
  socket->write(response);
  socket->waitForBytesWritten(2000);
  socket->disconnectFromHost();
  socket->deleteLater();
  server.close();
  if (code.isEmpty() || !validState) { if (error) *error = "OAuth callback validation failed"; return false; }

  QNetworkAccessManager network;
  QNetworkRequest tokenRequest(QUrl("https://oauth2.googleapis.com/token"));
  tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
  QUrlQuery tokenBody;
  tokenBody.addQueryItem("code", code);
  tokenBody.addQueryItem("client_id", QString::fromStdString(clientId));
  tokenBody.addQueryItem("client_secret", QString::fromStdString(clientSecret));
  tokenBody.addQueryItem("redirect_uri", redirect);
  tokenBody.addQueryItem("grant_type", "authorization_code");
  tokenBody.addQueryItem("code_verifier", QString::fromLatin1(verifier));
  QNetworkReply *reply = network.post(tokenRequest, tokenBody.query(QUrl::FullyEncoded).toUtf8());
  QEventLoop networkLoop;
  QTimer networkTimeout;
  networkTimeout.setSingleShot(true);
  QObject::connect(&networkTimeout, &QTimer::timeout, reply, &QNetworkReply::abort);
  QObject::connect(reply, &QNetworkReply::finished, &networkLoop, &QEventLoop::quit);
  networkTimeout.start(30000);
  networkLoop.exec();
  const QByteArray payload = reply->readAll();
  const auto networkError = reply->error();
  reply->deleteLater();
  if (networkError != QNetworkReply::NoError) { if (error) *error = "Google token exchange failed"; return false; }
  const QJsonObject tokens = QJsonDocument::fromJson(payload).object();
  const std::string accessToken = tokens.value("access_token").toString().toStdString();
  const std::string refreshToken = tokens.value("refresh_token").toString().toStdString();
  if (accessToken.empty() || refreshToken.empty()) { if (error) *error = "Google did not return the required tokens"; return false; }
  const qint64 expiresAt = QDateTime::currentSecsSinceEpoch() + tokens.value("expires_in").toInt(3600) - 60;
  return store.set("client_id", clientId, error) && store.set("client_secret", clientSecret, error) &&
         store.set("access_token", accessToken, error) && store.set("refresh_token", refreshToken, error) &&
         store.set("expires_at", std::to_string(expiresAt), error);
}

bool GoogleAuth::disconnect(std::string *error) { return store.clear(error); }
bool GoogleAuth::isConnected() const { return !store.get("refresh_token").empty(); }

} // namespace ReplayBufferPro::Cloud
