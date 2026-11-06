/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Profiles.h"
#include "common/Settings.h"

#include <QTest>

class ProfilesTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void validNames();
  void firstRunWithNoSettings();
  void profileKeys();
  void firstRunCreatesDefault();
  void createCopyOfCurrent();
  void createEmpty();
  void createRejectsDuplicates();
  void switchRestoresProfileSettings();
  void switchKeepsGlobalSettings();
  void switchToEmptyUsesDefaults();
  void proxySeesSwitchedValues();
  void renameActive();
  void renameCaseOnly();
  void removeRules();
  void missingActiveFileIsRecreated();

private:
  inline static const QString m_settingsPath = QStringLiteral("tmp/profiles-test");
  inline static const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPath);
  inline static const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPath);
};
