/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerDiscovery.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>

namespace deskflow::gui {

namespace {
const auto kApp = QStringLiteral("deskflow");
const auto kTypeQuery = QStringLiteral("query");
const auto kTypeAnnounce = QStringLiteral("announce");
constexpr int kMaxNameLength = 255;
constexpr int kResendDelay = 400;

std::optional<QJsonObject> parseMessage(const QByteArray &data)
{
  if (data.isEmpty() || data.size() > ServerDiscovery::kMaxMessageSize)
    return std::nullopt;

  QJsonParseError error;
  const auto doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return std::nullopt;

  const auto object = doc.object();
  if (object.value(QStringLiteral("app")).toString() != kApp ||
      object.value(QStringLiteral("v")).toInt() != ServerDiscovery::kProtocolVersion)
    return std::nullopt;

  return object;
}

QByteArray toJson(const QJsonObject &object)
{
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
} // namespace

QString DiscoveredServer::host(quint16 defaultPort) const
{
  auto text = address.toString();
  if (port != 0 && port != defaultPort)
    text.append(QStringLiteral(":%1").arg(port));
  return text;
}

ServerDiscovery::ServerDiscovery(QObject *parent, quint16 port)
    : QObject(parent),
      m_port{port},
      m_scanTimer{new QTimer(this)},
      m_resendTimer{new QTimer(this)}
{
  m_scanTimer->setSingleShot(true);
  connect(m_scanTimer, &QTimer::timeout, this, &ServerDiscovery::finishScan);

  // UDP can drop packets, ask a second time a moment later
  m_resendTimer->setSingleShot(true);
  m_resendTimer->setInterval(kResendDelay);
  connect(m_resendTimer, &QTimer::timeout, this, &ServerDiscovery::sendQueries);
}

ServerDiscovery::~ServerDiscovery() = default;

QByteArray ServerDiscovery::encodeQuery()
{
  return toJson({
      {QStringLiteral("app"), kApp},
      {QStringLiteral("v"), kProtocolVersion},
      {QStringLiteral("type"), kTypeQuery},
  });
}

QByteArray ServerDiscovery::encodeAnnounce(const QString &name, quint16 port, bool tls, const QString &mac)
{
  QJsonObject object{
      {QStringLiteral("app"), kApp},           {QStringLiteral("v"), kProtocolVersion},
      {QStringLiteral("type"), kTypeAnnounce}, {QStringLiteral("name"), name.left(kMaxNameLength)},
      {QStringLiteral("port"), port},          {QStringLiteral("tls"), tls},
  };
  if (!mac.isEmpty())
    object.insert(QStringLiteral("mac"), mac);
  return toJson(object);
}

QString ServerDiscovery::normalizedMac(const QString &mac)
{
  static const auto macRegex = QRegularExpression(QStringLiteral("^([0-9A-F]{2}[:-]){5}[0-9A-F]{2}$"));
  auto upper = mac.trimmed().toUpper();
  if (!macRegex.match(upper).hasMatch())
    return {};
  upper.replace('-', ':');
  // An all zero address is what loopback and some virtual adapters report
  if (upper == QStringLiteral("00:00:00:00:00:00"))
    return {};
  return upper;
}

QString ServerDiscovery::macFacing(const QHostAddress &peer)
{
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : interfaces) {
    const auto entries = interface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && entry.prefixLength() > 0 &&
          peer.isInSubnet(entry.ip(), entry.prefixLength()))
        return normalizedMac(interface.hardwareAddress());
    }
  }
  return {};
}

bool ServerDiscovery::isQuery(const QByteArray &data)
{
  const auto message = parseMessage(data);
  return message && message->value(QStringLiteral("type")).toString() == kTypeQuery;
}

std::optional<DiscoveredServer> ServerDiscovery::parseAnnounce(const QByteArray &data, const QHostAddress &from)
{
  const auto message = parseMessage(data);
  if (!message || message->value(QStringLiteral("type")).toString() != kTypeAnnounce)
    return std::nullopt;

  const auto port = message->value(QStringLiteral("port")).toInt();
  if (port <= 0 || port > 65535 || from.isNull())
    return std::nullopt;

  DiscoveredServer server;
  server.name = message->value(QStringLiteral("name")).toString().simplified().left(kMaxNameLength);
  server.address = from.toIPv4Address() != 0 ? QHostAddress(from.toIPv4Address()) : from;
  server.port = static_cast<quint16>(port);
  server.tls = message->value(QStringLiteral("tls")).toBool();
  server.mac = normalizedMac(message->value(QStringLiteral("mac")).toString());
  if (server.name.isEmpty())
    server.name = server.address.toString();
  return server;
}

bool ServerDiscovery::startResponding(const QString &name, quint16 serverPort, bool tls)
{
  m_name = name;
  m_serverPort = serverPort;
  m_tls = tls;
  if (m_responder)
    return true;

  m_responder = new QUdpSocket(this);
  if (!m_responder->bind(QHostAddress::AnyIPv4, m_port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    qWarning().noquote() << "server discovery unavailable, could not open udp port" << m_port << ":"
                         << m_responder->errorString();
    m_responder->deleteLater();
    m_responder = nullptr;
    return false;
  }

  connect(m_responder, &QUdpSocket::readyRead, this, &ServerDiscovery::readQueries);
  qInfo().noquote() << "answering server discovery on udp port" << m_port;
  return true;
}

void ServerDiscovery::stopResponding()
{
  if (!m_responder)
    return;
  m_responder->close();
  m_responder->deleteLater();
  m_responder = nullptr;
  qDebug() << "stopped answering server discovery";
}

bool ServerDiscovery::isResponding() const
{
  return m_responder != nullptr;
}

void ServerDiscovery::readQueries()
{
  while (m_responder && m_responder->hasPendingDatagrams()) {
    const auto datagram = m_responder->receiveDatagram(kMaxMessageSize + 1);
    if (!isQuery(datagram.data()))
      continue;
    // The MAC of the adapter facing the client lets it wake this computer up later
    const auto sender = datagram.senderAddress();
    const QHostAddress peer = sender.toIPv4Address() != 0 ? QHostAddress(sender.toIPv4Address()) : sender;
    const auto announce = encodeAnnounce(m_name, m_serverPort, m_tls, macFacing(peer));
    m_responder->writeDatagram(announce, sender, static_cast<quint16>(datagram.senderPort()));
  }
}

void ServerDiscovery::addQueryTarget(const QHostAddress &address)
{
  if (!m_extraTargets.contains(address))
    m_extraTargets.append(address);
}

QList<QHostAddress> ServerDiscovery::queryTargets() const
{
  QList<QHostAddress> targets{QHostAddress(QHostAddress::Broadcast)};

  // Directed broadcasts reach networks the limited broadcast may not (e.g. a second adapter)
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : interfaces) {
    const auto flags = interface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
        !flags.testFlag(QNetworkInterface::CanBroadcast) || flags.testFlag(QNetworkInterface::IsLoopBack))
      continue;
    const auto entries = interface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull() &&
          !targets.contains(entry.broadcast()))
        targets.append(entry.broadcast());
    }
  }

  for (const auto &extra : m_extraTargets) {
    if (!targets.contains(extra))
      targets.append(extra);
  }
  return targets;
}

void ServerDiscovery::scan(int timeoutMs)
{
  if (isScanning())
    return;

  m_servers.clear();
  m_browser = new QUdpSocket(this);
  if (!m_browser->bind(QHostAddress::AnyIPv4, 0)) {
    qWarning().noquote() << "server discovery failed, could not open a udp socket:" << m_browser->errorString();
    m_browser->deleteLater();
    m_browser = nullptr;
    Q_EMIT scanFinished(m_servers);
    return;
  }
  connect(m_browser, &QUdpSocket::readyRead, this, &ServerDiscovery::readAnswers);

  sendQueries();
  m_resendTimer->start();
  m_scanTimer->start(timeoutMs);
}

bool ServerDiscovery::isScanning() const
{
  return m_browser != nullptr;
}

void ServerDiscovery::sendQueries()
{
  if (!m_browser)
    return;
  const auto query = encodeQuery();
  const auto targets = queryTargets();
  for (const auto &target : targets)
    m_browser->writeDatagram(query, target, m_port);
}

void ServerDiscovery::readAnswers()
{
  while (m_browser && m_browser->hasPendingDatagrams()) {
    const auto datagram = m_browser->receiveDatagram(kMaxMessageSize + 1);
    const auto server = parseAnnounce(datagram.data(), datagram.senderAddress());
    if (!server)
      continue;

    // The same server answers once per query and once per network it is reachable on
    const auto known = std::ranges::find_if(m_servers, [&server](const DiscoveredServer &existing) {
      return existing.name == server->name && existing.port == server->port;
    });
    if (known != m_servers.end()) {
      // Prefer an address other computers can reach too
      if (known->address.isLoopback() && !server->address.isLoopback())
        known->address = server->address;
      continue;
    }

    m_servers.append(*server);
    Q_EMIT serverFound(*server);
  }
}

void ServerDiscovery::finishScan()
{
  m_resendTimer->stop();
  if (m_browser) {
    readAnswers();
    m_browser->close();
    m_browser->deleteLater();
    m_browser = nullptr;
  }

  std::ranges::sort(m_servers, [](const DiscoveredServer &a, const DiscoveredServer &b) {
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });
  Q_EMIT scanFinished(m_servers);
}

} // namespace deskflow::gui
