/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindowTests.h"

#include "common/Settings.h"
#include "gui/MainWindow.h"

#include <QDir>
#include <QMenu>
#include <QMenuBar>

void MainWindowTests::initTestCase()
{
  QDir(m_settingsPath).removeRecursively();
  QDir().mkpath(m_settingsPath);
  Settings::setSettingsFile(QStringLiteral("%1/Deskflow.conf").arg(m_settingsPath));
  Settings::setStateFile(QStringLiteral("%1/Deskflow.state").arg(m_settingsPath));

  // Nothing that opens a question or reaches the network
  Settings::setValue(Settings::Gui::AutoUpdateCheck, false);
  Settings::setValue(Settings::Gui::OnboardingDone, true);
  Settings::setValue(Settings::Gui::AutoStartCore, false);
  Settings::setValue(Settings::Gui::ShownFirstConnectedMessage, true);
  Settings::setValue(Settings::Gui::ShownServerFirstStartMessage, true);
  Settings::setValue(Settings::Security::TlsEnabled, false);

  // Remembered computers fill the wake up menu while the window is being built
  Settings::setValue(Settings::Client::WakeMac, QStringLiteral("AA:BB:CC:DD:EE:FF"));
  Settings::setValue(Settings::Client::WakeHost, QStringLiteral("office"));
  Settings::setValue(Settings::Server::KnownClients, QStringList{QStringLiteral("11:22:33:44:55:66=laptop")});
}

void MainWindowTests::startsAndQuits()
{
  auto window = std::make_unique<MainWindow>();
  window->open();
  QTest::qWait(200);
  window.reset();
}

void MainWindowTests::menusOpen()
{
  MainWindow window;
  window.open();
  QTest::qWait(100);

  // Every menu fills itself when it opens, including the submenus in the tray
  const auto menus = window.findChildren<QMenu *>();
  QVERIFY(!menus.isEmpty());
  for (auto menu : menus)
    Q_EMIT menu->aboutToShow();

  const auto wakeMenu = std::ranges::find_if(menus, [](const QMenu *menu) {
    return std::ranges::any_of(menu->actions(), [](const QAction *action) {
      return action->text() == QLatin1String("laptop");
    });
  });
  QVERIFY2(wakeMenu != menus.end(), "the wake up menu lists the remembered client");
}

QTEST_MAIN(MainWindowTests)
