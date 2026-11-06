/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

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
 * Messages are small JSON objects:
 *   query:    {"app":"deskflow","v":1,"type":"query"}
 *   announce: {"app":"deskflow","v":1,"type":"announce","name":"...","port":24800,"tls":true,"mac":"AA:BB:..."}
 */
class ServerDiscovery : public QObject
{
  Q_OBJECT

public:
  static constexpr quint16 kPort = 24802;
  static constexpr int kProtocolVersion = 1;
  static constexpr int kMaxMessageSize = 1024;
  static constexpr int kDefaultScanTime = 1500;

  /**
   * @param port UDP port used for queries and answers, tests use their own
   */
  explicit ServerDiscovery(QObject *parent = nullptr, quint16 port = kPort);
  ~ServerDiscovery() override;

  // Server side

  /**
   * @brief Start answering queries with the given server details
   * @return false if the port couldn't be opened
   */
  bool startResponding(const QString &name, quint16 serverPort, bool tls);
  void stopResponding();
  [[nodiscard]] bool isResponding() const;

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

  // Message helpers, public for testing
  static QByteArray encodeQuery();
  static QByteArray encodeAnnounce(const QString &name, quint16 port, bool tls, const QString &mac = QString());

  /**
   * @brief A MAC address as "AA:BB:CC:DD:EE:FF", or an empty string when it isn't a usable one
   */
  static QString normalizedMac(const QString &mac);

  /**
   * @brief MAC of the local adapter on the same network as peer, empty if none
   */
  static QString macFacing(const QHostAddress &peer);
  static bool isQuery(const QByteArray &data);
  static std::optional<DiscoveredServer> parseAnnounce(const QByteArray &data, const QHostAddress &from);

Q_SIGNALS:
  void serverFound(const deskflow::gui::DiscoveredServer &server);
  void scanFinished(const QList<deskflow::gui::DiscoveredServer> &servers);

private:
  void readQueries();
  void readAnswers();
  void finishScan();
  void sendQueries();
  [[nodiscard]] QList<QHostAddress> queryTargets() const;

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
};

} // namespace deskflow::gui
