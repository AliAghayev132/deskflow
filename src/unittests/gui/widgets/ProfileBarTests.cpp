/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProfileBarTests.h"

#include "common/Profiles.h"
#include "common/Settings.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QMenu>
#include <QSignalSpy>
#include <QToolButton>

namespace {
QComboBox *combo(ProfileBar &bar)
{
  return bar.findChild<QComboBox *>(QStringLiteral("cbProfile"));
}

QAction *deleteAction(ProfileBar &bar)
{
  auto button = bar.findChild<QToolButton *>(QStringLiteral("btnProfileMenu"));
  return button->menu()->actions().constLast();
}
} // namespace

void ProfileBarTests::initTestCase()
{
  QDir(m_settingsPath).removeRecursively();
  QVERIFY(QDir().mkpath(m_settingsPath));
  Settings::setSettingsFile(QStringLiteral("%1/Deskflow.conf").arg(m_settingsPath));
  Settings::setStateFile(QStringLiteral("%1/Deskflow.state").arg(m_settingsPath));
  Profiles::ensureInitialized();
}

void ProfileBarTests::showsActiveProfile()
{
  ProfileBar bar;
  QVERIFY(combo(bar));
  QCOMPARE(combo(bar)->count(), 1);
  QCOMPARE(combo(bar)->currentText(), Profiles::DefaultName);
}

void ProfileBarTests::deleteNeedsTwoProfiles()
{
  ProfileBar bar;
  QVERIFY(!deleteAction(bar)->isEnabled());

  QVERIFY(Profiles::create(QStringLiteral("Office"), true));
  bar.refresh();
  QCOMPARE(combo(bar)->count(), 2);
  QVERIFY(deleteAction(bar)->isEnabled());
  // Still on the active profile after the list changed
  QCOMPARE(combo(bar)->currentText(), Profiles::DefaultName);
}

void ProfileBarTests::pickingProfileRequestsSwitch()
{
  ProfileBar bar;
  QSignalSpy spy(&bar, &ProfileBar::switchRequested);

  const auto index = combo(bar)->findText(QStringLiteral("Office"));
  QVERIFY(index >= 0);
  combo(bar)->setCurrentIndex(index);
  Q_EMIT combo(bar)->activated(index);

  // The request is queued so the combo box can finish handling the click
  QVERIFY(spy.wait(1000));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.first().at(0).toString(), QStringLiteral("Office"));
}

void ProfileBarTests::pickingActiveProfileDoesNothing()
{
  ProfileBar bar;
  QSignalSpy spy(&bar, &ProfileBar::switchRequested);

  const auto index = combo(bar)->findText(Profiles::active());
  Q_EMIT combo(bar)->activated(index);

  QVERIFY(!spy.wait(200));
}

void ProfileBarTests::refreshFollowsActiveProfile()
{
  ProfileBar bar;
  QVERIFY(Profiles::switchTo(QStringLiteral("Office")));
  bar.refresh();
  QCOMPARE(combo(bar)->currentText(), QStringLiteral("Office"));
}

QTEST_MAIN(ProfileBarTests)
