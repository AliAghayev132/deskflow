/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/core/FileTransfer.h"

#include <QTemporaryDir>
#include <QTest>

class FileTransferTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void offerRoundTrip();
  void rejectsBadOffers();
  void safeFileNames();
  void uniquePaths();
  void sendsFiles();
  void declinedFiles();
  void untrustedReceiver();
  void refusesFolders();
  void safeRelativePaths();
  void sendsFolders();
  void folderNamesCantEscape();
  void requestRoundTrip();
  void requestGetsAnswer();
  void untrustedRequestFails();

private:
  QByteArray fingerprintOf(const QString &certificate) const;
  QTemporaryDir m_dir;
  QString m_receiverCert;
  QString m_senderCert;
};
