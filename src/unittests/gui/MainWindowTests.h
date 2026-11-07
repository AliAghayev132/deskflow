/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

/**
 * @brief Builds the real main window, so a crash while starting up fails CI instead of the app
 */
class MainWindowTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void startsAndQuits();
  void menusOpen();

private:
  inline static const QString m_settingsPath = QStringLiteral("tmp/mainwindow");
};
