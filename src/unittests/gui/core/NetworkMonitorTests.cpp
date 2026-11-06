/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitorTests.h"

#include "gui/core/NetworkMonitor.h"

#include <QSignalSpy>

using namespace deskflow::gui;
void NetworkMonitorTests::testVirtualInterface()
{
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vboxnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vboXnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("vmnet0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("docker-bridge")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("virbr0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("veth0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("br-0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("tun0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("utun0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("awdl0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("p2p0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("llw0")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("anpi5")));
  QVERIFY(NetworkMonitor::isVirtualInterface(QStringLiteral("tap1")));

  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("eth0")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("enp0s0f")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("wifi")));
  QVERIFY(!NetworkMonitor::isVirtualInterface(QStringLiteral("wlan0")));
}
void NetworkMonitorTests::testNetworkOf()
{
  using deskflow::gui::NetworkMonitor;
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("192.168.1.23"), 24), QStringLiteral("192.168.1.0/24"));
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("10.20.30.40"), 8), QStringLiteral("10.0.0.0/8"));
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("172.16.5.4"), 20), QStringLiteral("172.16.0.0/20"));
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("192.168.1.23"), 32), QStringLiteral("192.168.1.23/32"));
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("192.168.1.23"), 0), QString());
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("192.168.1.23"), 33), QString());
  QCOMPARE(NetworkMonitor::networkOf(QHostAddress("fe80::1"), 64), QString());

  // Whatever the machine is on, the list is well formed
  const auto networks = NetworkMonitor::localNetworks();
  for (const auto &network : networks)
    QVERIFY(network.contains('/'));
}

QTEST_MAIN(NetworkMonitorTests)
