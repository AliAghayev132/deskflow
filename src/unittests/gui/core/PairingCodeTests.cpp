/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingCodeTests.h"

#include <QRegularExpression>
#include <QSet>

using deskflow::gui::pairingCode;

namespace {
QByteArray countingBytes(int size, int start = 0)
{
  QByteArray data;
  for (int i = 0; i < size; ++i)
    data.append(char(start + i));
  return data;
}
} // namespace

void PairingCodeTests::knownValue()
{
  // The code must never change for a certificate, or people would see codes that don't match
  QCOMPARE(pairingCode(countingBytes(32)), QStringLiteral("763 580 697 921"));
}

void PairingCodeTests::format()
{
  static const QRegularExpression pattern(QStringLiteral("^\\d{3} \\d{3} \\d{3} \\d{3}$"));
  for (int i = 0; i < 200; ++i) {
    const auto code = pairingCode(countingBytes(32, i));
    QVERIFY2(pattern.match(code).hasMatch(), qPrintable(code));
  }
}

void PairingCodeTests::sameFingerprintSameCode()
{
  QCOMPARE(pairingCode(countingBytes(32, 7)), pairingCode(countingBytes(32, 7)));
}

void PairingCodeTests::differentFingerprintsDiffer()
{
  QSet<QString> codes;
  for (int i = 0; i < 200; ++i)
    codes.insert(pairingCode(countingBytes(32, i)));
  QCOMPARE(codes.size(), 200);
}

void PairingCodeTests::emptyFingerprint()
{
  QVERIFY(pairingCode({}).isEmpty());
}

QTEST_MAIN(PairingCodeTests)
