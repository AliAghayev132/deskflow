/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/widgets/ConnectionsPanel.h"

#include <QTest>

class ConnectionsPanelTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void durationText();
  void hiddenUntilSomethingRuns();
  void serverListsClients();
  void clientShowsServerState();
  void clearHides();
};
