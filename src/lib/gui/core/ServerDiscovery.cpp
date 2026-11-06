/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerDiscovery.h"

#include <QDebug>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>

#include <limits>

namespace deskflow::gui {

namespace {
const auto kApp = QStringLiteral("deskflow");
const auto kTypeQuery = QStringLiteral("query");
const auto kTypeAnnounce = QStringLiteral("announce");
const auto kTypeHello = QStringLiteral("hello");
const auto kTypeWelcome = QStringLiteral("welcome");
const auto kTypePing = QStringLiteral("ping");
const auto kTypePong = QStringLiteral("pong");
constexpr int kMaxNameLength = 255;
constexpr int kResendDelay = 400;
// Answers later than this are not counted, the packet was most likely lost
constexpr qint64 kMaxRoundTrip = 10000;
constexpr qsizetype kMaxPeers = 64;

QHostAddress plainAddress(const QHostAddress &address)
{
  return address.toIPv4Address() != 0 ? QHostAddress(address.toIPv4Address()) : address;
}

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

  m_clock.start();
  m_linkTimer = new QTimer(this);
  m_linkTimer->setInterval(kLinkInterval);
  connect(m_linkTimer, &QTimer::timeout, this, [this] {
    const auto now = m_clock.elapsed();
    m_pending.removeIf([now](const auto &entry) { return now - entry.value().sentAt > kMaxRoundTrip; });
    if (m_responder)
      pingClients();
    if (m_linkSocket)
      sendHello();
  });
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

QByteArray ServerDiscovery::encodeHello(const QString &name, const QString &mac, quint32 id)
{
  QJsonObject object{
      {QStringLiteral("app"), kApp},
      {QStringLiteral("v"), kProtocolVersion},
      {QStringLiteral("type"), kTypeHello},
      {QStringLiteral("id"), static_cast<qint64>(id)},
      {QStringLiteral("name"), name.left(kMaxNameLength)},
  };
  if (!mac.isEmpty())
    object.insert(QStringLiteral("mac"), mac);
  return toJson(object);
}

QByteArray ServerDiscovery::encodeEcho(const QString &type, quint32 id)
{
  return toJson({
      {QStringLiteral("app"), kApp},
      {QStringLiteral("v"), kProtocolVersion},
      {QStringLiteral("type"), type},
      {QStringLiteral("id"), static_cast<qint64>(id)},
  });
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

QString ServerDiscovery::messageType(const QByteArray &data)
{
  const auto message = parseMessage(data);
  return message ? message->value(QStringLiteral("type")).toString() : QString();
}

std::optional<quint32> ServerDiscovery::messageId(const QByteArray &data)
{
  const auto message = parseMessage(data);
  if (!message)
    return std::nullopt;
  const auto id = message->value(QStringLiteral("id")).toInteger(0);
  if (id <= 0 || id > std::numeric_limits<quint32>::max())
    return std::nullopt;
  return static_cast<quint32>(id);
}

std::optional<LinkPeer> ServerDiscovery::parseHello(const QByteArray &data, const QHostAddress &from, quint16 fromPort)
{
  const auto message = parseMessage(data);
  if (!message || message->value(QStringLiteral("type")).toString() != kTypeHello || from.isNull() || fromPort == 0)
    return std::nullopt;

  LinkPeer peer;
  peer.name = message->value(QStringLiteral("name")).toString().simplified().left(kMaxNameLength);
  if (peer.name.isEmpty() || !messageId(data))
    return std::nullopt;
  peer.address = plainAddress(from);
  peer.port = fromPort;
  peer.mac = normalizedMac(message->value(QStringLiteral("mac")).toString());
  return peer;
}

QString ServerDiscovery::hostPart(const QString &serverHost)
{
  auto host = serverHost.section(QLatin1Char(','), 0, 0).trimmed();
  if (host.startsWith(QLatin1Char('['))) {
    // [IPv6]:port
    return host.section(QLatin1Char(']'), 0, 0).mid(1);
  }
  // A single colon separates the port, more than one is an IPv6 address
  if (host.count(QLatin1Char(':')) == 1)
    host = host.section(QLatin1Char(':'), 0, 0);
  return host;
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

bool ServerDiscovery::startResponding(const QString &name, quint16 serverPort, bool tls, bool discoverable)
{
  m_name = name;
  m_serverPort = serverPort;
  m_tls = tls;
  m_discoverable = discoverable;
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
  m_linkTimer->start();
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
  m_peers.clear();
  m_peerSeen.clear();
  if (!m_linkSocket)
    m_linkTimer->stop();
  qDebug() << "stopped answering server discovery";
}

QList<LinkPeer> ServerDiscovery::linkPeers() const
{
  return m_peers.values();
}

bool ServerDiscovery::isResponding() const
{
  return m_responder != nullptr;
}

void ServerDiscovery::readQueries()
{
  while (m_responder && m_responder->hasPendingDatagrams()) {
    const auto datagram = m_responder->receiveDatagram(kMaxMessageSize + 1);
    const auto data = datagram.data();
    const auto type = messageType(data);
    const auto sender = datagram.senderAddress();
    const auto senderPort = static_cast<quint16>(datagram.senderPort());

    if (type == kTypeQuery) {
      if (!m_discoverable)
        continue;
      // The MAC of the adapter facing the client lets it wake this computer up later
      const auto announce = encodeAnnounce(m_name, m_serverPort, m_tls, macFacing(plainAddress(sender)));
      m_responder->writeDatagram(announce, sender, senderPort);
    } else if (type == kTypeHello) {
      answerHello(data, sender, senderPort);
    } else if (type == kTypePong) {
      if (const auto id = messageId(data))
        measured(*id, plainAddress(sender));
    }
  }
}

void ServerDiscovery::answerHello(const QByteArray &data, const QHostAddress &from, quint16 fromPort)
{
  const auto peer = parseHello(data, from, fromPort);
  // A home network never has this many computers, don't let a flood of names grow the list
  if (!peer || (!m_peers.contains(peer->name) && m_peers.size() >= kMaxPeers))
    return;
  m_responder->writeDatagram(encodeEcho(kTypeWelcome, *messageId(data)), from, fromPort);
  m_peers.insert(peer->name, *peer);
  m_peerSeen.insert(peer->name, m_clock.elapsed());
  Q_EMIT clientSeen(*peer);
}

void ServerDiscovery::pingClients()
{
  const auto now = m_clock.elapsed();
  const auto names = m_peers.keys();
  for (const auto &name : names) {
    if (now - m_peerSeen.value(name) > kLinkTimeout) {
      m_peers.remove(name);
      m_peerSeen.remove(name);
      continue;
    }
    const auto &peer = m_peers[name];
    m_responder->writeDatagram(encodeEcho(kTypePing, track(name, peer.address)), peer.address, peer.port);
  }
}

quint32 ServerDiscovery::track(const QString &name, const QHostAddress &to)
{
  const auto id = m_nextId;
  m_nextId = m_nextId == std::numeric_limits<quint32>::max() ? 1 : m_nextId + 1;
  m_pending.insert(id, {name, to, m_clock.elapsed()});
  return id;
}

void ServerDiscovery::measured(quint32 id, const QHostAddress &from)
{
  const auto pending = m_pending.find(id);
  // Only the computer that was asked can answer
  if (pending == m_pending.end() || pending->to != from)
    return;
  const auto elapsed = m_clock.elapsed() - pending->sentAt;
  const auto name = pending->name;
  m_pending.erase(pending);
  Q_EMIT latencyMeasured(name, static_cast<int>(elapsed));
}

void ServerDiscovery::startLink(const QString &name, const QString &serverHost)
{
  m_linkName = name;
  if (serverHost != m_linkHost) {
    m_linkHost = serverHost;
    m_linkAddress.clear();
  }

  if (!m_linkSocket) {
    m_linkSocket = new QUdpSocket(this);
    if (!m_linkSocket->bind(QHostAddress::AnyIPv4, 0)) {
      qWarning().noquote() << "connection quality unavailable, could not open a udp socket:"
                           << m_linkSocket->errorString();
      m_linkSocket->deleteLater();
      m_linkSocket = nullptr;
      return;
    }
    connect(m_linkSocket, &QUdpSocket::readyRead, this, &ServerDiscovery::readLink);
  }
  m_linkTimer->start();
  sendHello();
}

void ServerDiscovery::stopLink()
{
  if (!m_linkSocket)
    return;
  m_linkSocket->close();
  m_linkSocket->deleteLater();
  m_linkSocket = nullptr;
  m_linkAddress.clear();
  m_linkHost.clear();
  if (!m_responder)
    m_linkTimer->stop();
}

bool ServerDiscovery::isLinked() const
{
  return m_linkSocket != nullptr;
}

void ServerDiscovery::sendHello()
{
  if (!m_linkSocket)
    return;

  if (m_linkAddress.isNull()) {
    const auto host = hostPart(m_linkHost);
    if (QHostAddress address; address.setAddress(host)) {
      m_linkAddress = plainAddress(address);
    } else if (!host.isEmpty() && !m_resolving) {
      m_resolving = true;
      QHostInfo::lookupHost(host, this, [this, wanted = m_linkHost](const QHostInfo &info) {
        m_resolving = false;
        if (wanted != m_linkHost || !m_linkSocket)
          return;
        const auto addresses = info.addresses();
        for (const auto &address : addresses) {
          if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            m_linkAddress = address;
            sendHello();
            return;
          }
        }
      });
      return;
    }
    if (m_linkAddress.isNull())
      return;
  }

  const auto hello = encodeHello(m_linkName, macFacing(m_linkAddress), track(m_linkHost, m_linkAddress));
  m_linkSocket->writeDatagram(hello, m_linkAddress, m_port);
}

void ServerDiscovery::readLink()
{
  while (m_linkSocket && m_linkSocket->hasPendingDatagrams()) {
    const auto datagram = m_linkSocket->receiveDatagram(kMaxMessageSize + 1);
    const auto data = datagram.data();
    const auto sender = plainAddress(datagram.senderAddress());
    const auto id = messageId(data);
    if (!id || sender != m_linkAddress)
      continue;

    const auto type = messageType(data);
    if (type == kTypeWelcome)
      measured(*id, sender);
    else if (type == kTypePing)
      m_linkSocket->writeDatagram(encodeEcho(kTypePong, *id), datagram.senderAddress(), datagram.senderPort());
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
