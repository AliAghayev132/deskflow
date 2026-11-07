/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/core/Updater.h"

#include <QTest>

class QTcpServer;

class UpdaterTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void versionIn();
  void currentVersionHasFourParts();
  void picksNewestInstallable();
  void skipsTestBuildsWhenAsked();
  void nothingNewer();
  void checksums();
  void checkFindsUpdate();
  void downloadVerifiesChecksum();
  void downloadRejectsDamagedFile();

private:
  QUrl serve(const QString &path) const;
  QTcpServer *m_server = nullptr;
  QHash<QString, QByteArray> m_files;
};
