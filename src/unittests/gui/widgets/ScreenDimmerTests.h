/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/ScreenDimmer.h"

#include <QTest>

class ScreenDimmerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void startsClear();
  void coversEveryScreen();
  void neverTakesTheMouseOrFocus();
  void undimHides();
  void quickChangesEndRight();
};
