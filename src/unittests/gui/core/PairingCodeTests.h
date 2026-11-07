/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/core/PairingCode.h"

#include <QTest>

class PairingCodeTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void knownValue();
  void format();
  void sameFingerprintSameCode();
  void differentFingerprintsDiffer();
  void emptyFingerprint();
};
