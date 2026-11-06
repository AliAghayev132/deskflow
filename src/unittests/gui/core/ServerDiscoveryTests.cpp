/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerDiscoveryTests.h"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QUdpSocket>

using deskflow::gui::DiscoveredServer;
using deskflow::gui::ServerDiscovery;

void ServerDiscoveryTests::queryRoundTrip()
{
  QVERIFY(ServerDiscovery::isQuery(ServerDiscovery::encodeQuery()));
  QVERIFY(!ServerDiscovery::isQuery(ServerDiscovery::encodeAnnounce("pc", 24800, true)));
}

void ServerDiscoveryTests::announceRoundTrip()
{
  const auto data = ServerDiscovery::encodeAnnounce("huseyn-pc", 24800, true);
  const auto server = ServerDiscovery::parseAnnounce(data, QHostAddress("192.168.1.5"));
  QVERIFY(server.has_value());
  QCOMPARE(server->name, QStringLiteral("huseyn-pc"));
  QCOMPARE(server->address, QHostAddress("192.168.1.5"));
  QCOMPARE(server->port, quint16(24800));
  QVERIFY(server->tls);

  // IPv4 addresses mapped into IPv6 are shown as plain IPv4
  const auto mapped = ServerDiscovery::parseAnnounce(data, QHostAddress("::ffff:192.168.1.5"));
  QVERIFY(mapped.has_value());
  QCOMPARE(mapped->address.toString(), QStringLiteral("192.168.1.5"));
}

void ServerDiscoveryTests::rejectsForeignMessages()
{
  QVERIFY(!ServerDiscovery::isQuery({}));
  QVERIFY(!ServerDiscovery::isQuery("hello"));
  QVERIFY(!ServerDiscovery::isQuery(R"({"app":"other","v":1,"type":"query"})"));
  QVERIFY(!ServerDiscovery::isQuery(R"({"app":"deskflow","v":2,"type":"query"})"));
  QVERIFY(!ServerDiscovery::isQuery(R"(["app","deskflow"])"));
  QVERIFY(!ServerDiscovery::isQuery(QByteArray(ServerDiscovery::kMaxMessageSize + 1, ' ')));
}

void ServerDiscoveryTests::rejectsBadAnnounce()
{
  const QHostAddress from("10.0.0.2");
  QVERIFY(!ServerDiscovery::parseAnnounce(ServerDiscovery::encodeQuery(), from));
  QVERIFY(!ServerDiscovery::parseAnnounce(R"({"app":"deskflow","v":1,"type":"announce","port":0})", from));
  QVERIFY(!ServerDiscovery::parseAnnounce(R"({"app":"deskflow","v":1,"type":"announce","port":70000})", from));
  QVERIFY(!ServerDiscovery::parseAnnounce(ServerDiscovery::encodeAnnounce("pc", 24800, false), QHostAddress()));

  // A missing name falls back to the address
  const auto unnamed =
      ServerDiscovery::parseAnnounce(R"({"app":"deskflow","v":1,"type":"announce","port":24800})", from);
  QVERIFY(unnamed.has_value());
  QCOMPARE(unnamed->name, QStringLiteral("10.0.0.2"));
}

void ServerDiscoveryTests::hostIncludesNonDefaultPort()
{
  DiscoveredServer server{"pc", QHostAddress("192.168.1.5"), 24800, true, {}};
  QCOMPARE(server.host(24800), QStringLiteral("192.168.1.5"));
  server.port = 25000;
  QCOMPARE(server.host(24800), QStringLiteral("192.168.1.5:25000"));
}

void ServerDiscoveryTests::findsServerOverUdp()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("test-server", 24800, true));
  QVERIFY(server.isResponding());

  ServerDiscovery client(nullptr, m_testPort);
  client.addQueryTarget(QHostAddress::LocalHost);
  QSignalSpy found(&client, &ServerDiscovery::serverFound);
  QSignalSpy finished(&client, &ServerDiscovery::scanFinished);

  client.scan(800);
  QVERIFY(client.isScanning());
  QVERIFY(finished.wait(3000));
  QVERIFY(!client.isScanning());

  // Answers to the broadcast and the loopback query are merged into one server
  QCOMPARE(found.count(), 1);
  const auto servers = client.servers();
  QCOMPARE(servers.size(), 1);
  QCOMPARE(servers.first().name, QStringLiteral("test-server"));
  QCOMPARE(servers.first().port, quint16(24800));
  QVERIFY(servers.first().tls);
  // When the server answered on a real network too, that address is kept
  QVERIFY(!servers.first().address.isNull());
}

void ServerDiscoveryTests::noAnswerWhenNotResponding()
{
  ServerDiscovery client(nullptr, m_testPort);
  client.addQueryTarget(QHostAddress::LocalHost);
  QSignalSpy finished(&client, &ServerDiscovery::scanFinished);
  client.scan(400);
  QVERIFY(finished.wait(3000));
  QVERIFY(client.servers().isEmpty());
}

void ServerDiscoveryTests::stopRespondingStopsAnswers()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("going-away", 24800, false));
  server.stopResponding();
  QVERIFY(!server.isResponding());

  ServerDiscovery client(nullptr, m_testPort);
  client.addQueryTarget(QHostAddress::LocalHost);
  QSignalSpy finished(&client, &ServerDiscovery::scanFinished);
  client.scan(400);
  QVERIFY(finished.wait(3000));
  QVERIFY(client.servers().isEmpty());
}

void ServerDiscoveryTests::respondingTwiceUpdatesDetails()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("old-name", 24800, false));
  QVERIFY(server.startResponding("new-name", 25000, true));
  // Port changes are still one server, as the name stays unique

  ServerDiscovery client(nullptr, m_testPort);
  client.addQueryTarget(QHostAddress::LocalHost);
  QSignalSpy finished(&client, &ServerDiscovery::scanFinished);
  client.scan(800);
  QVERIFY(finished.wait(3000));
  QCOMPARE(client.servers().size(), 1);
  QCOMPARE(client.servers().first().name, QStringLiteral("new-name"));
  QCOMPARE(client.servers().first().port, quint16(25000));
}

void ServerDiscoveryTests::macRoundTrip()
{
  const auto data = ServerDiscovery::encodeAnnounce("pc", 24800, true, "aa:bb:cc:dd:ee:ff");
  const auto server = ServerDiscovery::parseAnnounce(data, QHostAddress("192.168.1.5"));
  QVERIFY(server.has_value());
  QCOMPARE(server->mac, QStringLiteral("AA:BB:CC:DD:EE:FF"));

  // Older servers send no MAC, and nonsense is dropped
  QVERIFY(
      ServerDiscovery::parseAnnounce(ServerDiscovery::encodeAnnounce("pc", 24800, true), QHostAddress("10.0.0.1"))
          ->mac.isEmpty()
  );
  QVERIFY(
      ServerDiscovery::parseAnnounce(
          ServerDiscovery::encodeAnnounce("pc", 24800, true, "not a mac"), QHostAddress("10.0.0.1")
      )
          ->mac.isEmpty()
  );
}

void ServerDiscoveryTests::normalizesMac()
{
  QCOMPARE(ServerDiscovery::normalizedMac("aa-bb-cc-dd-ee-0f"), QStringLiteral("AA:BB:CC:DD:EE:0F"));
  QCOMPARE(ServerDiscovery::normalizedMac(" 01:23:45:67:89:ab "), QStringLiteral("01:23:45:67:89:AB"));
  QCOMPARE(ServerDiscovery::normalizedMac("00:00:00:00:00:00"), QString());
  QCOMPARE(ServerDiscovery::normalizedMac("01:23:45:67:89"), QString());
  QCOMPARE(ServerDiscovery::normalizedMac("01:23:45:67:89:ag"), QString());
  QCOMPARE(ServerDiscovery::normalizedMac(""), QString());
}

void ServerDiscoveryTests::magicPacket()
{
  using deskflow::gui::WakeOnLan;
  const auto packet = WakeOnLan::magicPacket("01:23:45:67:89:AB");
  QCOMPARE(packet.size(), 102);
  QCOMPARE(packet.left(6), QByteArray(6, char(0xFF)));
  const auto mac = QByteArray::fromHex("0123456789AB");
  for (int i = 0; i < 16; ++i)
    QCOMPARE(packet.mid(6 + i * 6, 6), mac);

  QVERIFY(WakeOnLan::magicPacket("bad").isEmpty());
  QVERIFY(WakeOnLan::magicPacket("00:00:00:00:00:00").isEmpty());
}

void ServerDiscoveryTests::wakeSendsPacket()
{
  using deskflow::gui::WakeOnLan;
  QUdpSocket receiver;
  QVERIFY(receiver.bind(QHostAddress::LocalHost, 0));
  QVERIFY(WakeOnLan::wake("01:23:45:67:89:AB", receiver.localPort(), {QHostAddress::LocalHost}));
  QVERIFY(receiver.waitForReadyRead(2000));
  const auto datagram = receiver.receiveDatagram();
  QCOMPARE(datagram.data(), WakeOnLan::magicPacket("01:23:45:67:89:AB"));

  QVERIFY(!WakeOnLan::wake("bad", receiver.localPort(), {QHostAddress::LocalHost}));
}

QTEST_MAIN(ServerDiscoveryTests)
