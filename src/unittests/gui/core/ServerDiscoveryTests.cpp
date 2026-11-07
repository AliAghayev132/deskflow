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

void ServerDiscoveryTests::helloRoundTrip()
{
  const auto data = ServerDiscovery::encodeHello("laptop", "aa:bb:cc:dd:ee:ff", 42);
  QCOMPARE(ServerDiscovery::messageType(data), QStringLiteral("hello"));
  QCOMPARE(ServerDiscovery::messageId(data), std::optional<quint32>(42));

  const auto peer = ServerDiscovery::parseHello(data, QHostAddress("::ffff:192.168.1.7"), 50000);
  QVERIFY(peer.has_value());
  QCOMPARE(peer->name, QStringLiteral("laptop"));
  QCOMPARE(peer->address.toString(), QStringLiteral("192.168.1.7"));
  QCOMPARE(peer->port, quint16(50000));
  QCOMPARE(peer->mac, QStringLiteral("AA:BB:CC:DD:EE:FF"));

  const auto pong = ServerDiscovery::encodeEcho("pong", 4000000000u);
  QCOMPARE(ServerDiscovery::messageType(pong), QStringLiteral("pong"));
  QCOMPARE(ServerDiscovery::messageId(pong), std::optional<quint32>(4000000000u));

  // Older versions don't know these messages and must not mistake them for queries
  QVERIFY(!ServerDiscovery::isQuery(data));
  QVERIFY(!ServerDiscovery::parseAnnounce(data, QHostAddress("10.0.0.1")));
}

void ServerDiscoveryTests::rejectsBadHello()
{
  const QHostAddress from("10.0.0.2");
  QVERIFY(!ServerDiscovery::parseHello(ServerDiscovery::encodeHello("", "", 1), from, 5000));
  QVERIFY(!ServerDiscovery::parseHello(ServerDiscovery::encodeHello("pc", "", 0), from, 5000));
  QVERIFY(!ServerDiscovery::parseHello(ServerDiscovery::encodeHello("pc", "", 1), QHostAddress(), 5000));
  QVERIFY(!ServerDiscovery::parseHello(ServerDiscovery::encodeHello("pc", "", 1), from, 0));
  QVERIFY(!ServerDiscovery::parseHello(ServerDiscovery::encodeQuery(), from, 5000));
  QVERIFY(!ServerDiscovery::messageId(R"({"app":"deskflow","v":1,"type":"pong","id":-3})"));
  QVERIFY(!ServerDiscovery::messageId(R"({"app":"deskflow","v":1,"type":"pong","id":5000000000})"));
  QVERIFY(ServerDiscovery::messageType("nonsense").isEmpty());
}

void ServerDiscoveryTests::hostPart()
{
  QCOMPARE(ServerDiscovery::hostPart("192.168.1.5"), QStringLiteral("192.168.1.5"));
  QCOMPARE(ServerDiscovery::hostPart("192.168.1.5:25000"), QStringLiteral("192.168.1.5"));
  QCOMPARE(ServerDiscovery::hostPart(" office.local , 10.0.0.2"), QStringLiteral("office.local"));
  QCOMPARE(ServerDiscovery::hostPart("fe80::1"), QStringLiteral("fe80::1"));
  QCOMPARE(ServerDiscovery::hostPart("[fe80::1]:24800"), QStringLiteral("fe80::1"));
  QCOMPARE(ServerDiscovery::hostPart(""), QString());
}

void ServerDiscoveryTests::linkMeasuresLatency()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("server", 24800, true));
  QSignalSpy seen(&server, &ServerDiscovery::clientSeen);
  QSignalSpy serverLatency(&server, &ServerDiscovery::latencyMeasured);

  ServerDiscovery client(nullptr, m_testPort);
  QSignalSpy clientLatency(&client, &ServerDiscovery::latencyMeasured);
  client.startLink("laptop", "127.0.0.1:24800");
  QVERIFY(client.isLinked());

  // The first hello goes out right away and is answered with a welcome
  QVERIFY(clientLatency.wait(2000));
  QCOMPARE(clientLatency.first().at(0).toString(), QStringLiteral("127.0.0.1:24800"));
  QVERIFY(clientLatency.first().at(1).toInt() >= 0);

  QCOMPARE(seen.count(), 1);
  const auto peer = seen.first().at(0).value<deskflow::gui::LinkPeer>();
  QCOMPARE(peer.name, QStringLiteral("laptop"));
  QCOMPARE(server.linkPeers().size(), 1);

  // The server pings the clients it knows on its own schedule
  QVERIFY(serverLatency.wait(ServerDiscovery::kLinkInterval + 2000));
  QCOMPARE(serverLatency.first().at(0).toString(), QStringLiteral("laptop"));

  client.stopLink();
  QVERIFY(!client.isLinked());
  server.stopResponding();
  QVERIFY(server.linkPeers().isEmpty());
}

void ServerDiscoveryTests::hiddenServerStillAnswersHello()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("hidden", 24800, true, false));
  QVERIFY(!server.isDiscoverable());

  // Not found by a scan...
  ServerDiscovery scanner(nullptr, m_testPort);
  scanner.addQueryTarget(QHostAddress::LocalHost);
  QSignalSpy finished(&scanner, &ServerDiscovery::scanFinished);
  scanner.scan(400);
  QVERIFY(finished.wait(3000));
  QVERIFY(scanner.servers().isEmpty());

  // ...but its connected clients still get answers
  ServerDiscovery client(nullptr, m_testPort);
  QSignalSpy latency(&client, &ServerDiscovery::latencyMeasured);
  client.startLink("laptop", "127.0.0.1");
  QVERIFY(latency.wait(2000));
}

void ServerDiscoveryTests::parsesTailscaleStatus()
{
  using deskflow::gui::Tailscale;
  const QByteArray status = R"({
    "Self": {"HostName": "me", "TailscaleIPs": ["100.64.0.1"]},
    "Peer": {
      "nodekey:a": {"HostName": "office-pc", "TailscaleIPs": ["fd7a:115c:a1e0::2", "100.101.102.103"], "Online": true},
      "nodekey:b": {"HostName": "old-laptop", "TailscaleIPs": ["100.90.1.2"], "Online": false},
      "nodekey:c": {"HostName": "no-ipv4", "TailscaleIPs": ["fd7a:115c:a1e0::3"], "Online": true}
    }
  })";
  const auto peers = Tailscale::parseStatus(status);
  QCOMPARE(peers.size(), 2);
  const auto office = std::ranges::find_if(peers, [](const auto &peer) { return peer.name == "office-pc"; });
  QVERIFY(office != peers.end());
  QCOMPARE(office->address, QHostAddress("100.101.102.103"));
  QVERIFY(office->online);
  QVERIFY(Tailscale::parseStatus("not json").isEmpty());

  QVERIFY(Tailscale::isTailscaleAddress(QHostAddress("100.64.0.1")));
  QVERIFY(Tailscale::isTailscaleAddress(QHostAddress("100.127.255.254")));
  QVERIFY(!Tailscale::isTailscaleAddress(QHostAddress("100.128.0.1")));
  QVERIFY(!Tailscale::isTailscaleAddress(QHostAddress("192.168.1.5")));
}

void ServerDiscoveryTests::queryNowFindsServer()
{
  ServerDiscovery server(nullptr, m_testPort);
  QVERIFY(server.startResponding("far-away", 24800, true));

  // Nothing local answers, the address only becomes known during the scan, like a Tailscale peer
  ServerDiscovery client(nullptr, m_testPort);
  QSignalSpy finished(&client, &ServerDiscovery::scanFinished);
  client.scan(800);
  client.queryNow({QHostAddress::LocalHost});
  QVERIFY(finished.wait(3000));
  const auto servers = client.servers();
  QVERIFY(std::ranges::any_of(servers, [](const auto &found) { return found.name == "far-away"; }));
}

QTEST_MAIN(ServerDiscoveryTests)
