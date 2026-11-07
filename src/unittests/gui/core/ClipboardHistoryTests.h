/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/core/ClipboardHistory.h"

#include <QTest>

class ClipboardHistoryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void recordsTextNewestFirst();
  void duplicatesMoveToTop();
  void ignoresBlankText();
  void keepsAtMostMaxEntries();
  void recordsImages();
  void prefersTextOverImage();
  void skipsSecrets();
  void disabledRecordsNothing();
  void restoreMovesToTop();
  void summaryIsShortAndMenuSafe();
};
