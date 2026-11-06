/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>

#include <optional>

class QTimer;
class QUdpSocket;

namespace deskflow::gui {

/**
 * @brief A server found on the local network
 */
struct DiscoveredServer
{
  QString name;         ///< computer name of the server
  QHostAddress address; ///< address the answer came from
  quint16 port = 0;     ///< port the server listens on
  bool tls = false;     ///< whether the server uses TLS
  QString mac;          ///< hardware address of the server's adapter, for Wake-on-LAN, may be empty

  /**
   * @brief Text to put in the "Connect to" box, includes the port when it isn't the default one
   */
  [[nodiscard]] QString host(quint16 defaultPort) const;

  bool operator==(const DiscoveredServer &other) const = default;
};

/**
 * @brief A client that said hello to this server
 */
struct LinkPeer
{
  QString name;         ///< computer name of the client
  QHostAddress address; ///< address the hello came from
  quint16 port = 0;     ///< UDP port of the client's link socket
  QString mac;          ///< hardware address of the client's adapter, for Wake-on-LAN, may be empty
};

/**
 * @brief Finds Deskflow servers on the local network without typing an address.
 *
 * A computer running as a server answers small UDP queries on kPort while it
 * is discoverable. A client broadcasts a query to every local network and lists
 * the servers that answer.
 *
 * Only the computer name, port, whether TLS is used and the MAC address of the
 * adapter facing the client (for Wake-on-LAN) are shared; connecting
 * still goes through the usual TLS fingerprint check.
 *
 * While connected, a client also sends a "hello" to its server every few seconds. The
 * answers give the round trip time shown next to each connection, and the server learns
 * the client's MAC so it can wake that client up later.
 *
 * Messages are small JSON objects:
 *   query:    {"app":"deskflow","v":1,"type":"query"}
 *   announce: {"app":"deskflow","v":1,"type":"announce","name":"...","port":24800,"tls":true,"mac":"AA:BB:..."}
 *   hello:    {"app":"deskflow","v":1,"type":"hello","id":7,"name":"...","mac":"AA:BB:..."} client to server
 *   welcome:  {"app":"deskflow","v":1,"type":"welcome","id":7}                            answer to hello
 *   ping:     {"app":"deskflow","v":1,"type":"ping","id":9}                               server to client
 *   pong:     {"app":"deskflow","v":1,"type":"pong","id":9}                               answer to ping
 * Older versions ignore the message types they don't know.
 */
class ServerDiscovery : public QObject
{
  Q_OBJECT

public:
  static constexpr quint16 kPort = 24802;
  static constexpr int kProtocolVersion = 1;
  static constexpr int kMaxMessageSize = 1024;
  static constexpr int kDefaultScanTime = 1500;
  static constexpr int kLinkInterval = 3000;
  /// Clients that haven't said hello for this long are no longer pinged
  static constexpr int kLinkTimeout = 15000;

  /**
   * @param port UDP port used for queries and answers, tests use their own
   */
  explicit ServerDiscovery(QObject *parent = nullptr, quint16 port = kPort);
  ~ServerDiscovery() override;

  // Server side

  /**
   * @brief Start answering queries with the given server details
   * @param discoverable answer scans from clients, hello messages are answered either way
   * @return false if the port couldn't be opened
   */
  bool startResponding(const QString &name, quint16 serverPort, bool tls, bool discoverable = true);
  void stopResponding();
  [[nodiscard]] bool isResponding() const;
  [[nodiscard]] bool isDiscoverable() const
  {
    return m_discoverable;
  }

  /**
   * @brief Clients that said hello recently, by name
   */
  [[nodiscard]] QList<LinkPeer> linkPeers() const;

  // Client side

  /**
   * @brief Ask the local networks for servers, results arrive through serverFound and scanFinished
   * @param timeoutMs how long to wait for answers
   */
  void scan(int timeoutMs = kDefaultScanTime);
  [[nodiscard]] bool isScanning() const;

  /**
   * @brief Servers found by the last scan, sorted by name
   */
  [[nodiscard]] QList<DiscoveredServer> servers() const
  {
    return m_servers;
  }

  /**
   * @brief Also send queries to this address, e.g. a specific host or loopback in tests
   */
  void addQueryTarget(const QHostAddress &address);

  /**
   * @brief Keep saying hello to the server this client is connected to
   * @param name this computer's name
   * @param serverHost the text from the "Connect to" box, a name or address, optionally with a port
   */
  void startLink(const QString &name, const QString &serverHost);
  void stopLink();
  [[nodiscard]] bool isLinked() const;

  /**
   * @brief The address part of the "Connect to" text, e.g. "pc.local" from "pc.local:24800, 10.0.0.2"
   */
  static QString hostPart(const QString &serverHost);

  // Message helpers, public for testing
  static QByteArray encodeQuery();
  static QByteArray encodeAnnounce(const QString &name, quint16 port, bool tls, const QString &mac = QString());
  static QByteArray encodeHello(const QString &name, const QString &mac, quint32 id);
  /// welcome, ping or pong
  static QByteArray encodeEcho(const QString &type, quint32 id);

  /**
   * @brief A MAC address as "AA:BB:CC:DD:EE:FF", or an empty string when it isn't a usable one
   */
  static QString normalizedMac(const QString &mac);

  /**
   * @brief MAC of the local adapter on the same network as peer, empty if none
   */
  static QString macFacing(const QHostAddress &peer);
  static bool isQuery(const QByteArray &data);
  /// The type of a valid message, empty for anything else
  static QString messageType(const QByteArray &data);
  static std::optional<quint32> messageId(const QByteArray &data);
  static std::optional<LinkPeer> parseHello(const QByteArray &data, const QHostAddress &from, quint16 fromPort);
  static std::optional<DiscoveredServer> parseAnnounce(const QByteArray &data, const QHostAddress &from);

Q_SIGNALS:
  void serverFound(const deskflow::gui::DiscoveredServer &server);
  void scanFinished(const QList<deskflow::gui::DiscoveredServer> &servers);

  /**
   * @brief A connected client said hello (server side)
   */
  void clientSeen(const deskflow::gui::LinkPeer &peer);

  /**
   * @brief Round trip time to a client (server side, by client name)
   * or to the server (client side, with the host given to startLink)
   */
  void latencyMeasured(const QString &name, int milliseconds);

private:
  void readQueries();
  void readAnswers();
  void finishScan();
  void sendQueries();
  [[nodiscard]] QList<QHostAddress> queryTargets() const;
  void answerHello(const QByteArray &data, const QHostAddress &from, quint16 fromPort);
  void pingClients();
  void sendHello();
  void readLink();
  void measured(quint32 id, const QHostAddress &from);
  quint32 track(const QString &name, const QHostAddress &to);

  struct Pending
  {
    QString name;
    QHostAddress to;
    qint64 sentAt = 0;
  };

  quint16 m_port;
  QUdpSocket *m_responder = nullptr;
  QUdpSocket *m_browser = nullptr;
  QTimer *m_scanTimer = nullptr;
  QTimer *m_resendTimer = nullptr;
  QString m_name;
  quint16 m_serverPort = 0;
  bool m_tls = false;
  QList<DiscoveredServer> m_servers;
  QList<QHostAddress> m_extraTargets;
  bool m_discoverable = true;

  // Link between a connected client and its server
  QElapsedTimer m_clock;
  QTimer *m_linkTimer = nullptr;
  QHash<QString, LinkPeer> m_peers;
  QHash<QString, qint64> m_peerSeen;
  QHash<quint32, Pending> m_pending;
  quint32 m_nextId = 1;
  QUdpSocket *m_linkSocket = nullptr;
  QString m_linkName;
  QString m_linkHost;
  QHostAddress m_linkAddress;
  bool m_resolving = false;
};

} // namespace deskflow::gui
