/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransfer.h"

#include "common/Constants.h"

#include <QDateTime>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QStorageInfo>
#include <QTimer>

#include <algorithm>

namespace deskflow::gui {

namespace {
const auto kApp = QStringLiteral("deskflow");
constexpr int kVersion = 1;
const auto kTypeOffer = QStringLiteral("offer");
const auto kTypeAccept = QStringLiteral("accept");
const auto kTypeDecline = QStringLiteral("decline");
const auto kTypeDone = QStringLiteral("done");
constexpr int kMaxNameLength = 200;
/// The offer has to arrive soon after connecting
constexpr int kOfferTimeout = 30000;
/// After the last byte, the receiver confirms quickly
constexpr int kDoneTimeout = 60000;
constexpr int kConnectTimeout = 15000;
constexpr qint64 kProgressStep = 1024 * 1024;
/// Only a few offers can wait at the same time
constexpr std::size_t kMaxIncoming = 8;

std::optional<QJsonObject> parseLine(const QByteArray &line)
{
  if (line.isEmpty() || line.size() > filetransfer::kMaxLine)
    return std::nullopt;
  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(line, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return std::nullopt;
  const auto object = doc.object();
  if (object.value(QStringLiteral("app")).toString() != kApp || object.value(QStringLiteral("v")).toInt() != kVersion)
    return std::nullopt;
  return object;
}

QByteArray toLine(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject header(const QString &type)
{
  return {{QStringLiteral("app"), kApp}, {QStringLiteral("v"), kVersion}, {QStringLiteral("type"), type}};
}

/// Takes one line off the front of buffer, without the newline
std::optional<QByteArray> takeLine(QByteArray &buffer)
{
  const auto end = buffer.indexOf('\n');
  if (end < 0)
    return std::nullopt;
  auto line = buffer.left(end);
  buffer.remove(0, end + 1);
  return line;
}

QHostAddress plainAddress(const QHostAddress &address)
{
  return address.toIPv4Address() != 0 ? QHostAddress(address.toIPv4Address()) : address;
}
} // namespace

qint64 FileOffer::totalSize() const
{
  qint64 total = 0;
  for (const auto size : sizes)
    total += size;
  return total;
}

namespace filetransfer {

QByteArray encodeOffer(const QString &from, const QStringList &names, const QList<qint64> &sizes, bool requested)
{
  QJsonArray files;
  for (qsizetype i = 0; i < names.size(); ++i)
    files.append(QJsonObject{{QStringLiteral("name"), names.at(i)}, {QStringLiteral("size"), sizes.value(i)}});
  auto object = header(kTypeOffer);
  object.insert(QStringLiteral("from"), from);
  object.insert(QStringLiteral("files"), files);
  if (requested)
    object.insert(QStringLiteral("requested"), true);
  return toLine(object);
}

QByteArray encodeRequest(const QString &type, const QString &from, int count)
{
  auto object = header(type);
  object.insert(QStringLiteral("from"), from);
  if (count > 0)
    object.insert(QStringLiteral("count"), count);
  return toLine(object);
}

std::optional<PeerRequest> parseRequest(const QByteArray &line)
{
  const auto object = parseLine(line);
  if (!object)
    return std::nullopt;
  const auto type = object->value(QStringLiteral("type")).toString();
  if (type != kRequestCopied && type != kRequestPull && type != kRequestLock && type != kRequestGame)
    return std::nullopt;

  PeerRequest request;
  request.type = type;
  request.from = object->value(QStringLiteral("from")).toString().simplified().left(255);
  request.count = std::clamp(object->value(QStringLiteral("count")).toInt(), 0, kMaxFiles);
  return request;
}

std::optional<FileOffer> parseOffer(const QByteArray &line)
{
  const auto object = parseLine(line);
  if (!object || object->value(QStringLiteral("type")).toString() != kTypeOffer)
    return std::nullopt;

  const auto files = object->value(QStringLiteral("files")).toArray();
  if (files.isEmpty() || files.size() > kMaxFiles)
    return std::nullopt;

  FileOffer offer;
  offer.from = object->value(QStringLiteral("from")).toString().simplified().left(255);
  offer.requested = object->value(QStringLiteral("requested")).toBool();
  for (const auto &value : files) {
    const auto file = value.toObject();
    const auto size = file.value(QStringLiteral("size")).toInteger(-1);
    if (size < 0)
      return std::nullopt;
    offer.names.append(safeRelativePath(file.value(QStringLiteral("name")).toString()));
    offer.sizes.append(size);
  }
  return offer;
}

QByteArray encodeMessage(const QString &type)
{
  return toLine(header(type));
}

QString messageType(const QByteArray &line)
{
  const auto object = parseLine(line.trimmed());
  return object ? object->value(QStringLiteral("type")).toString() : QString();
}

QString safeFileName(const QString &name)
{
  // Only the last part counts, the sender doesn't get to pick a folder
  auto safe = name.section(QRegularExpression(QStringLiteral("[/\\\\]")), -1);

  // Characters Windows refuses, and control characters anywhere
  static const QRegularExpression invalid(QStringLiteral("[<>:\"|?*\\x00-\\x1F\\x7F]"));
  safe.replace(invalid, QStringLiteral("_"));

  // Windows drops trailing dots and spaces, which could turn "a.exe." into "a.exe"
  while (!safe.isEmpty() && (safe.endsWith(QLatin1Char('.')) || safe.endsWith(QLatin1Char(' '))))
    safe.chop(1);
  safe = safe.trimmed();

  if (safe.isEmpty() || safe == QLatin1String(".") || safe == QLatin1String(".."))
    safe = QStringLiteral("file");

  static const QRegularExpression reserved(
      QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\\..*)?$"), QRegularExpression::CaseInsensitiveOption
  );
  if (reserved.match(safe).hasMatch())
    safe.prepend(QLatin1Char('_'));

  if (safe.size() > kMaxNameLength) {
    const auto suffix = QFileInfo(safe).suffix();
    const auto keep = suffix.isEmpty() || suffix.size() > 20 ? 0 : suffix.size() + 1;
    safe = safe.left(kMaxNameLength - keep) + (keep ? QStringLiteral(".") + suffix : QString());
  }
  return safe;
}

QString safeRelativePath(const QString &path)
{
  static const QRegularExpression separators(QStringLiteral("[/\\\\]"));
  QStringList parts;
  const auto raw = path.split(separators, Qt::SkipEmptyParts);
  for (const auto &part : raw) {
    const auto trimmed = part.trimmed();
    if (trimmed.isEmpty() || trimmed == QLatin1String(".") || trimmed == QLatin1String(".."))
      continue;
    parts.append(safeFileName(trimmed));
  }
  return parts.isEmpty() ? QStringLiteral("file") : parts.join(QLatin1Char('/'));
}

QString uniquePath(const QDir &dir, const QString &name)
{
  if (!dir.exists(name))
    return dir.filePath(name);

  // "report.pdf" becomes "report (2).pdf", a leading dot is part of the name
  auto split = name.indexOf(QLatin1Char('.'), 1);
  if (split < 0)
    split = name.size();
  const auto base = name.left(split);
  const auto extension = name.mid(split);
  for (int i = 2; i < 10000; ++i) {
    const auto candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(extension);
    if (!dir.exists(candidate))
      return dir.filePath(candidate);
  }
  return dir.filePath(
      QStringLiteral("%1 (%2)%3").arg(base, QString::number(QDateTime::currentMSecsSinceEpoch()), extension)
  );
}

QSslConfiguration sslConfiguration(const QString &certificatePath)
{
  auto config = QSslConfiguration::defaultConfiguration();
  config.setProtocol(QSsl::TlsV1_2OrLater);
  // Both sides use self signed certificates, the fingerprints are checked instead
  config.setPeerVerifyMode(QSslSocket::QueryPeer);

  QFile file(certificatePath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning().noquote() << "file transfer can't read the certificate:" << certificatePath;
    return config;
  }
  const auto pem = file.readAll();
  auto key = QSslKey(pem, QSsl::Rsa, QSsl::Pem);
  if (key.isNull())
    key = QSslKey(pem, QSsl::Ec, QSsl::Pem);
  const auto certificates = QSslCertificate::fromData(pem, QSsl::Pem);
  if (key.isNull() || certificates.isEmpty()) {
    qWarning().noquote() << "file transfer can't use the certificate:" << certificatePath;
    return config;
  }
  config.setPrivateKey(key);
  config.setLocalCertificate(certificates.first());
  return config;
}

QString sizeText(qint64 bytes)
{
  return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace filetransfer

// Receiving

struct FileReceiver::Incoming
{
  enum class State
  {
    Offer,
    Waiting,
    Receiving
  };

  int id = 0;
  QSslSocket *socket = nullptr;
  QTimer *timer = nullptr;
  State state = State::Offer;
  QByteArray buffer;
  FileOffer offer;
  QDir dir;
  int fileIndex = 0;
  qint64 fileReceived = 0;
  qint64 received = 0;
  qint64 reported = 0;
  std::unique_ptr<QSaveFile> file;
  /// What the current file adds to the list of saved items: itself, or its top folder
  QString currentEntry;
  /// Top folder names as sent, and the free names they got here
  QHash<QString, QString> topFolders;
  QStringList saved;
};

FileReceiver::FileReceiver(QObject *parent) : QObject(parent)
{
  // Nothing to set up until listen
}

FileReceiver::~FileReceiver()
{
  close();
}

bool FileReceiver::listen(const QSslConfiguration &config, quint16 port)
{
  if (m_server)
    m_server->setSslConfiguration(config);
  else {
    m_server = new QSslServer(this);
    m_server->setSslConfiguration(config);
    connect(m_server, &QSslServer::pendingConnectionAvailable, this, &FileReceiver::newConnection);
    // Self signed certificates are expected, the user sees who is sending and decides
    connect(m_server, &QSslServer::sslErrors, this, [](QSslSocket *socket, const QList<QSslError> &) {
      socket->ignoreSslErrors();
    });
  }

  if (m_server->isListening())
    return true;
  if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
    qWarning().noquote() << "receiving files unavailable, could not open tcp port" << port << ":"
                         << m_server->errorString();
    return false;
  }
  qInfo().noquote() << "ready to receive files on tcp port" << m_server->serverPort();
  return true;
}

void FileReceiver::close()
{
  std::vector<int> ids;
  for (const auto &[id, incoming] : m_incoming)
    ids.push_back(id);
  for (const auto id : ids)
    fail(id, tr("Receiving files was stopped"));

  if (m_server && m_server->isListening()) {
    m_server->close();
    qDebug() << "stopped receiving files";
  }
}

bool FileReceiver::isListening() const
{
  return m_server && m_server->isListening();
}

quint16 FileReceiver::port() const
{
  return m_server ? m_server->serverPort() : 0;
}

FileReceiver::Incoming *FileReceiver::find(int id)
{
  const auto it = m_incoming.find(id);
  return it == m_incoming.end() ? nullptr : it->second.get();
}

void FileReceiver::newConnection()
{
  while (m_server && m_server->hasPendingConnections()) {
    auto socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection());
    if (!socket)
      continue;
    if (m_incoming.size() >= kMaxIncoming) {
      socket->abort();
      socket->deleteLater();
      continue;
    }

    const auto id = m_nextId++;
    auto incoming = std::make_unique<Incoming>();
    incoming->id = id;
    incoming->socket = socket;
    incoming->timer = new QTimer(socket);
    incoming->timer->setSingleShot(true);
    connect(incoming->timer, &QTimer::timeout, this, [this, id] {
      auto in = find(id);
      if (in && in->state == Incoming::State::Waiting)
        fail(id, tr("The files were not accepted in time"));
      else
        fail(id, tr("The other computer stopped responding"));
    });
    incoming->timer->start(kOfferTimeout);

    connect(socket, &QSslSocket::readyRead, this, [this, id] { readFrom(id); });
    connect(socket, &QSslSocket::disconnected, this, [this, id] {
      // Bytes can still be waiting after the sender closed its side
      if (auto in = find(id); in && in->state == Incoming::State::Receiving)
        receive(*in);
      fail(id, tr("The other computer closed the connection"));
    });
    m_incoming.emplace(id, std::move(incoming));

    if (socket->bytesAvailable() > 0)
      readFrom(id);
  }
}

void FileReceiver::readFrom(int id)
{
  auto in = find(id);
  if (!in)
    return;

  switch (in->state) {
  case Incoming::State::Offer: {
    in->buffer.append(in->socket->read(filetransfer::kMaxLine + 1 - in->buffer.size()));
    const auto line = takeLine(in->buffer);
    if (!line) {
      if (in->buffer.size() > filetransfer::kMaxLine)
        fail(id, {});
      return;
    }
    if (auto request = filetransfer::parseRequest(*line)) {
      const auto peer = in->socket->peerCertificate();
      request->fingerprint = peer.isNull() ? QByteArray() : peer.digest(QCryptographicHash::Sha256);
      request->address = plainAddress(in->socket->peerAddress());
      const auto answer = m_requestHandler ? m_requestHandler(*request) : QString();
      answerRequest(id, answer.isEmpty() ? kTypeDone : answer);
      return;
    }

    auto offer = filetransfer::parseOffer(*line);
    if (!offer) {
      fail(id, {});
      return;
    }
    const auto certificate = in->socket->peerCertificate();
    offer->fingerprint = certificate.isNull() ? QByteArray() : certificate.digest(QCryptographicHash::Sha256);
    offer->address = plainAddress(in->socket->peerAddress());
    in->offer = *offer;
    in->state = Incoming::State::Waiting;
    in->timer->start(filetransfer::kAnswerTimeout);
    Q_EMIT offered(id, in->offer);
    return;
  }

  case Incoming::State::Waiting:
    // The sender has to wait for an answer
    fail(id, tr("The other computer sent data before it was accepted"));
    return;

  case Incoming::State::Receiving:
    receive(*in);
    return;
  }
}

void FileReceiver::accept(int id, const QString &directory)
{
  auto in = find(id);
  if (!in || in->state != Incoming::State::Waiting)
    return;
  in->timer->stop();

  in->dir = QDir(directory);
  if (!in->dir.mkpath(QStringLiteral("."))) {
    in->socket->write(filetransfer::encodeMessage(kTypeDecline));
    //: %1 is a folder
    fail(id, tr("Could not create the folder %1").arg(QDir::toNativeSeparators(directory)));
    return;
  }

  const QStorageInfo storage(in->dir.absolutePath());
  if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < in->offer.totalSize()) {
    in->socket->write(filetransfer::encodeMessage(kTypeDecline));
    fail(id, tr("There is not enough free space for these files"));
    return;
  }

  in->state = Incoming::State::Receiving;
  in->socket->write(filetransfer::encodeMessage(kTypeAccept));
  if (!openNextFile(*in))
    return;
  receive(*in);
}

void FileReceiver::answerRequest(int id, const QString &answer)
{
  const auto it = m_incoming.find(id);
  if (it == m_incoming.end())
    return;

  auto incoming = std::move(it->second);
  m_incoming.erase(it);
  auto socket = incoming->socket;
  socket->disconnect(this);
  socket->write(filetransfer::encodeMessage(answer));
  connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
  socket->disconnectFromHost();
}

void FileReceiver::decline(int id)
{
  const auto it = m_incoming.find(id);
  if (it == m_incoming.end() || it->second->state != Incoming::State::Waiting)
    return;

  auto incoming = std::move(it->second);
  m_incoming.erase(it);
  auto socket = incoming->socket;
  socket->disconnect(this);
  socket->write(filetransfer::encodeMessage(kTypeDecline));
  connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
  socket->disconnectFromHost();
}

bool FileReceiver::openNextFile(Incoming &in)
{
  const auto id = in.id;

  while (in.fileIndex < in.offer.names.size()) {
    const auto &name = in.offer.names.at(in.fileIndex);
    QString path;
    if (name.contains(QLatin1Char('/'))) {
      // Files of a folder go into one new folder, "Photos (2)" when "Photos" is already there
      const auto top = name.section(QLatin1Char('/'), 0, 0);
      if (!in.topFolders.contains(top))
        in.topFolders.insert(top, QFileInfo(filetransfer::uniquePath(in.dir, top)).fileName());
      const auto folder = in.topFolders.value(top);
      path = in.dir.filePath(folder + QLatin1Char('/') + name.section(QLatin1Char('/'), 1));
      in.currentEntry = in.dir.filePath(folder);
      if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail(id, tr("Could not create the folder %1").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath())));
        return false;
      }
    } else {
      path = filetransfer::uniquePath(in.dir, name);
      in.currentEntry = path;
    }
    in.file = std::make_unique<QSaveFile>(path);
    if (!in.file->open(QIODevice::WriteOnly)) {
      //: %1 is a file name
      fail(id, tr("Could not save %1").arg(in.offer.names.at(in.fileIndex)));
      return false;
    }
    if (in.offer.sizes.at(in.fileIndex) > 0)
      return true;

    // Empty files are complete right away
    if (!in.file->commit()) {
      fail(id, tr("Could not save %1").arg(in.offer.names.at(in.fileIndex)));
      return false;
    }
    if (!in.saved.contains(in.currentEntry))
      in.saved.append(in.currentEntry);
    in.file.reset();
    ++in.fileIndex;
  }

  succeed(id);
  return false;
}

void FileReceiver::receive(Incoming &in)
{
  const auto id = in.id;
  const auto total = in.offer.totalSize();

  while (in.file && in.socket->bytesAvailable() > 0) {
    const auto size = in.offer.sizes.at(in.fileIndex);
    const auto data = in.socket->read(std::min(size - in.fileReceived, filetransfer::kChunkSize));
    if (data.isEmpty())
      break;
    if (in.file->write(data) != data.size()) {
      fail(id, tr("Could not save %1").arg(in.offer.names.at(in.fileIndex)));
      return;
    }
    in.fileReceived += data.size();
    in.received += data.size();

    if (in.received - in.reported >= kProgressStep) {
      in.reported = in.received;
      Q_EMIT progress(id, in.received, total);
    }

    if (in.fileReceived == size) {
      if (!in.file->commit()) {
        fail(id, tr("Could not save %1").arg(in.offer.names.at(in.fileIndex)));
        return;
      }
      if (!in.saved.contains(in.currentEntry))
        in.saved.append(in.currentEntry);
      in.file.reset();
      in.fileReceived = 0;
      ++in.fileIndex;
      if (!openNextFile(in))
        return;
    }
  }
}

void FileReceiver::succeed(int id)
{
  const auto it = m_incoming.find(id);
  if (it == m_incoming.end())
    return;
  auto incoming = std::move(it->second);
  m_incoming.erase(it);

  auto socket = incoming->socket;
  socket->disconnect(this);
  socket->write(filetransfer::encodeMessage(kTypeDone));
  connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
  socket->disconnectFromHost();

  Q_EMIT progress(id, incoming->received, incoming->offer.totalSize());
  Q_EMIT finished(id, true, {}, incoming->saved);
}

void FileReceiver::fail(int id, const QString &error)
{
  const auto it = m_incoming.find(id);
  if (it == m_incoming.end())
    return;
  auto incoming = std::move(it->second);
  m_incoming.erase(it);

  // Partly written files are thrown away, finished ones are kept
  if (incoming->file)
    incoming->file->cancelWriting();
  incoming->file.reset();

  auto socket = incoming->socket;
  socket->disconnect(this);
  socket->abort();
  socket->deleteLater();

  // Nobody saw an offer that never arrived, there is nothing to report
  if (incoming->state != Incoming::State::Offer) {
    qWarning().noquote() << "receiving files failed:" << error;
    Q_EMIT finished(id, false, error, incoming->saved);
  }
}

// Sending

FileSender::FileSender(const QString &from, const QStringList &paths, QObject *parent, bool requested)
    : QObject(parent),
      m_from{from},
      m_requested{requested},
      m_answerTimer{new QTimer(this)}
{
  // Folders are sent as the files inside them, named by their place in the folder
  for (const auto &path : paths) {
    const QFileInfo info(path);
    if (info.isDir()) {
      const QDir root(path);
      const auto folderName = info.fileName().isEmpty() ? QStringLiteral("folder") : info.fileName();
      // Links are not followed, so nothing outside the folder is sent by accident
      QDirIterator files(
          path, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories
      );
      qsizetype found = 0;
      while (files.hasNext()) {
        const auto file = files.next();
        m_paths.append(file);
        m_names.append(folderName + QLatin1Char('/') + root.relativeFilePath(file));
        ++found;
      }
      if (found == 0 && m_problem.isEmpty())
        //: %1 is a folder name
        m_problem = tr("%1 has no files to send").arg(folderName);
    } else {
      m_paths.append(path);
      m_names.append(info.fileName());
    }
  }

  for (const auto &path : std::as_const(m_paths)) {
    const QFileInfo info(path);
    const auto size = info.isFile() ? info.size() : -1;
    m_sizes.append(size);
    if (size > 0)
      m_total += size;
  }
  m_answerTimer->setSingleShot(true);
  connect(m_answerTimer, &QTimer::timeout, this, [this] {
    if (m_streaming || m_sent > 0)
      finish(false, tr("The other computer stopped responding"));
    else if (m_socket && m_socket->isEncrypted())
      finish(false, tr("Nobody answered on the other computer"));
    else
      finish(false, tr("Could not reach the other computer"));
  });
}

FileSender::~FileSender()
{
  delete m_file;
}

QString FileSender::checkFiles() const
{
  if (!m_problem.isEmpty())
    return m_problem;
  if (m_paths.isEmpty())
    return tr("No files were chosen");
  if (m_paths.size() > filetransfer::kMaxFiles)
    //: %1 is the most files that can be sent at once
    return tr("Up to %1 files can be sent at once").arg(filetransfer::kMaxFiles);
  for (const auto &path : m_paths) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable())
      //: %1 is a file name
      return tr("%1 can't be read").arg(info.fileName());
  }
  return {};
}

void FileSender::start(
    const QHostAddress &address, const QSslConfiguration &config, const TrustCheck &trusted, quint16 port
)
{
  if (const auto problem = checkFiles(); !problem.isEmpty()) {
    finish(false, problem);
    return;
  }

  m_trusted = trusted;
  m_socket = new QSslSocket(this);
  m_socket->setSslConfiguration(config);
  connect(m_socket, &QSslSocket::encrypted, this, &FileSender::encrypted);
  // The certificate is self signed, its fingerprint is checked once the connection is up
  connect(m_socket, &QSslSocket::sslErrors, m_socket, [this](const QList<QSslError> &) {
    m_socket->ignoreSslErrors();
  });
  connect(m_socket, &QSslSocket::readyRead, this, &FileSender::readReply);
  connect(m_socket, &QSslSocket::encryptedBytesWritten, this, &FileSender::sendMore);
  connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
    // The receiver closes the connection after confirming, readReply handles that
    if (error == QAbstractSocket::RemoteHostClosedError) {
      readReply();
      if (m_done)
        return;
    }
    finish(false, m_socket ? m_socket->errorString() : QString());
  });

  m_answerTimer->start(kConnectTimeout);
  m_socket->connectToHostEncrypted(address.toString(), port);
}

void FileSender::cancel()
{
  finish(false, tr("Sending was cancelled"));
}

void FileSender::encrypted()
{
  const auto certificate = m_socket->peerCertificate();
  const auto fingerprint = certificate.isNull() ? QByteArray() : certificate.digest(QCryptographicHash::Sha256);
  if (fingerprint.isEmpty() || !m_trusted || !m_trusted(fingerprint)) {
    finish(false, tr("The other computer could not be verified, connect to it with %1 first").arg(kAppName));
    return;
  }

  m_socket->write(filetransfer::encodeOffer(m_from, m_names, m_sizes, m_requested));
  m_answerTimer->start(filetransfer::kAnswerTimeout);
  Q_EMIT waiting();
}

void FileSender::readReply()
{
  if (!m_socket)
    return;
  m_buffer.append(m_socket->readAll());
  if (m_buffer.size() > filetransfer::kMaxLine) {
    finish(false, tr("The other computer sent an answer that could not be read"));
    return;
  }

  while (auto line = takeLine(m_buffer)) {
    const auto type = filetransfer::messageType(*line);
    if (type == kTypeAccept && !m_streaming && m_sent == 0) {
      m_answerTimer->stop();
      m_streaming = true;
      Q_EMIT progress(0, m_total);
      sendMore();
    } else if (type == kTypeDecline) {
      finish(false, tr("The other computer declined the files"));
      return;
    } else if (type == kTypeDone) {
      finish(m_sent == m_total, m_sent == m_total ? QString() : tr("The transfer ended early"));
      return;
    }
  }
}

void FileSender::sendMore()
{
  if (!m_streaming || m_done || !m_socket)
    return;

  // Keep only a little data queued, so big files are read as the network takes them
  while (m_socket->bytesToWrite() + m_socket->encryptedBytesToWrite() < 4 * filetransfer::kChunkSize) {
    if (m_fileIndex >= m_paths.size()) {
      // Everything is out, wait for the receiver to confirm
      m_streaming = false;
      m_answerTimer->start(kDoneTimeout);
      return;
    }

    const auto size = m_sizes.at(m_fileIndex);
    if (!m_file && size > 0) {
      m_file = new QFile(m_paths.at(m_fileIndex));
      if (!m_file->open(QIODevice::ReadOnly)) {
        finish(false, tr("%1 can't be read").arg(QFileInfo(m_paths.at(m_fileIndex)).fileName()));
        return;
      }
    }

    if (m_fileSent < size) {
      const auto data = m_file->read(std::min(size - m_fileSent, filetransfer::kChunkSize));
      if (data.isEmpty()) {
        //: %1 is a file name
        finish(false, tr("%1 changed while it was being sent").arg(QFileInfo(m_paths.at(m_fileIndex)).fileName()));
        return;
      }
      m_socket->write(data);
      m_fileSent += data.size();
      m_sent += data.size();
      Q_EMIT progress(m_sent, m_total);
    }

    if (m_fileSent >= size) {
      delete m_file;
      m_file = nullptr;
      m_fileSent = 0;
      ++m_fileIndex;
    }
  }
}

void FileSender::finish(bool ok, const QString &error)
{
  if (m_done)
    return;
  m_done = true;
  m_streaming = false;
  m_answerTimer->stop();
  delete m_file;
  m_file = nullptr;

  if (m_socket) {
    m_socket->disconnect(this);
    if (ok)
      m_socket->disconnectFromHost();
    else
      m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
  }

  if (!ok)
    qWarning().noquote() << "sending files failed:" << error;
  Q_EMIT finished(ok, error);
}

// One request

PeerMessage::PeerMessage(const QByteArray &request, QObject *parent)
    : QObject(parent),
      m_request{request},
      m_timer{new QTimer(this)}
{
  m_timer->setSingleShot(true);
  connect(m_timer, &QTimer::timeout, this, [this] { finish({}, tr("The other computer didn't answer")); });
}

void PeerMessage::send(
    const QHostAddress &address, const QSslConfiguration &config, const FileSender::TrustCheck &trusted, quint16 port
)
{
  m_socket = new QSslSocket(this);
  m_socket->setSslConfiguration(config);
  connect(m_socket, &QSslSocket::sslErrors, m_socket, [this](const QList<QSslError> &) {
    m_socket->ignoreSslErrors();
  });
  connect(m_socket, &QSslSocket::encrypted, this, [this, trusted] {
    const auto certificate = m_socket->peerCertificate();
    const auto fingerprint = certificate.isNull() ? QByteArray() : certificate.digest(QCryptographicHash::Sha256);
    if (fingerprint.isEmpty() || !trusted || !trusted(fingerprint)) {
      finish({}, tr("The other computer could not be verified"));
      return;
    }
    m_socket->write(m_request);
  });
  connect(m_socket, &QSslSocket::readyRead, this, [this] {
    m_buffer.append(m_socket->readAll());
    if (const auto line = takeLine(m_buffer))
      finish(filetransfer::messageType(*line), {});
    else if (m_buffer.size() > filetransfer::kMaxLine)
      finish({}, tr("The other computer sent an answer that could not be read"));
  });
  connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    finish({}, m_socket ? m_socket->errorString() : QString());
  });

  m_timer->start(kConnectTimeout);
  m_socket->connectToHostEncrypted(address.toString(), port);
}

void PeerMessage::finish(const QString &answer, const QString &error)
{
  if (m_done)
    return;
  m_done = true;
  m_timer->stop();
  if (m_socket) {
    m_socket->disconnect(this);
    m_socket->disconnectFromHost();
    m_socket->deleteLater();
    m_socket = nullptr;
  }
  Q_EMIT finished(answer, error);
}

} // namespace deskflow::gui
