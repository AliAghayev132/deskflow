/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/ProfileBar.h"

#include <QTest>

class ProfileBarTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void showsActiveProfile();
  void deleteNeedsTwoProfiles();
  void pickingProfileRequestsSwitch();
  void pickingActiveProfileDoesNothing();
  void refreshFollowsActiveProfile();

private:
  inline static const QString m_settingsPath = QStringLiteral("tmp/profilebar-test");
};
