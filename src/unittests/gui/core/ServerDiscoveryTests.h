/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/core/ServerDiscovery.h"

#include <QTest>

class ServerDiscoveryTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void queryRoundTrip();
  void announceRoundTrip();
  void rejectsForeignMessages();
  void rejectsBadAnnounce();
  void hostIncludesNonDefaultPort();
  void findsServerOverUdp();
  void noAnswerWhenNotResponding();
  void stopRespondingStopsAnswers();
  void respondingTwiceUpdatesDetails();

private:
  // A port of our own so tests don't clash with a running Deskflow
  static constexpr quint16 m_testPort = 34802;
};
