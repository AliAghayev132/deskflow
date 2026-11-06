/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ConnectionsPanelTests.h"

#include <QLabel>

using deskflow::core::ConnectionState;

namespace {
QString bodyText(const ConnectionsPanel &panel)
{
  // The second label holds the list
  const auto labels = panel.findChildren<QLabel *>();
  return labels.size() > 1 ? labels.at(1)->text() : QString();
}
} // namespace

void ConnectionsPanelTests::durationText()
{
  QCOMPARE(ConnectionsPanel::durationText(0), QStringLiteral("just now"));
  QCOMPARE(ConnectionsPanel::durationText(59), QStringLiteral("just now"));
  QCOMPARE(ConnectionsPanel::durationText(60), QStringLiteral("1 min"));
  QCOMPARE(ConnectionsPanel::durationText(59 * 60), QStringLiteral("59 min"));
  QCOMPARE(ConnectionsPanel::durationText(65 * 60), QStringLiteral("1 h 5 min"));
}

void ConnectionsPanelTests::hiddenUntilSomethingRuns()
{
  // Hidden explicitly, so showing the main window doesn't show an empty card
  QWidget window;
  auto panel = new ConnectionsPanel(&window);
  window.show();
  QVERIFY(panel->isHidden());
  QVERIFY(!panel->isVisible());
  panel->refresh();
  QVERIFY(!panel->isVisible());
}

void ConnectionsPanelTests::serverListsClients()
{
  ConnectionsPanel panel;
  panel.showServer({});
  QVERIFY(!panel.isHidden());
  QVERIFY(panel.connectedNames().isEmpty());
  QVERIFY(bodyText(panel).contains("Waiting"));

  panel.showServer({"laptop", "mac<mini>"});
  QCOMPARE(panel.connectedNames(), (QStringList{"laptop", "mac<mini>"}));
  QVERIFY(bodyText(panel).contains("laptop"));
  // Names are escaped, they are shown as rich text
  QVERIFY(bodyText(panel).contains("mac&lt;mini&gt;"));
  QVERIFY(bodyText(panel).contains("just now"));

  panel.showServer({"laptop"});
  QCOMPARE(panel.connectedNames(), QStringList{"laptop"});
  QVERIFY(!bodyText(panel).contains("mac&lt;mini&gt;"));
}

void ConnectionsPanelTests::clientShowsServerState()
{
  ConnectionsPanel panel;
  panel.showClient(ConnectionState::Connecting, "192.168.1.5");
  QVERIFY(!panel.isHidden());
  QVERIFY(panel.connectedNames().isEmpty());
  QVERIFY(bodyText(panel).contains("connecting"));

  panel.showClient(ConnectionState::Connected, "192.168.1.5");
  QCOMPARE(panel.connectedNames(), QStringList{"192.168.1.5"});
  QVERIFY(bodyText(panel).contains("just now"));

  panel.showClient(ConnectionState::Disconnected, "192.168.1.5");
  QVERIFY(panel.connectedNames().isEmpty());
  QVERIFY(bodyText(panel).contains("not connected"));
}

void ConnectionsPanelTests::clearHides()
{
  ConnectionsPanel panel;
  panel.showServer({"laptop"});
  panel.clear();
  QVERIFY(panel.isHidden());
  QVERIFY(panel.connectedNames().isEmpty());
}

void ConnectionsPanelTests::showsLatency()
{
  QCOMPARE(ConnectionsPanel::latencyText(0), QStringLiteral("<1 ms"));
  QCOMPARE(ConnectionsPanel::latencyText(12), QStringLiteral("12 ms"));

  ConnectionsPanel panel;
  panel.showServer({"laptop", "desktop"});
  panel.setLatency("laptop", 3);
  QVERIFY(bodyText(panel).contains("3 ms"));
  panel.setLatency("desktop", 0);
  QVERIFY(bodyText(panel).contains("&lt;1 ms"));

  // Slow links are highlighted
  panel.setLatency("laptop", 80);
  QVERIFY(bodyText(panel).contains("80 ms</span>"));

  // A client that leaves takes its time with it
  panel.showServer({"desktop"});
  panel.showServer({"desktop", "laptop"});
  QVERIFY(!bodyText(panel).contains("80 ms"));

  panel.showClient(deskflow::core::ConnectionState::Connected, "10.0.0.2");
  panel.setLatency("10.0.0.2", 7);
  QVERIFY(bodyText(panel).contains("7 ms"));
  panel.showClient(deskflow::core::ConnectionState::Disconnected, "10.0.0.2");
  QVERIFY(!bodyText(panel).contains("7 ms"));
}

QTEST_MAIN(ConnectionsPanelTests)
