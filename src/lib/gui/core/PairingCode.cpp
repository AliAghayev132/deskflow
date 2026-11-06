/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingCode.h"

#include <QCryptographicHash>
#include <QStringList>

namespace deskflow::gui {

QString pairingCode(const QByteArray &fingerprint)
{
  if (fingerprint.isEmpty())
    return {};

  // Hashing again keeps the code independent of how the fingerprint itself is shown
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArrayLiteral("deskflow-pairing-code-v1"));
  hash.addData(fingerprint);
  const auto digest = hash.result();

  quint64 value = 0;
  for (int i = 0; i < 8; ++i)
    value = (value << 8) | static_cast<quint8>(digest.at(i));

  quint64 limit = 1;
  for (int i = 0; i < kPairingCodeDigits; ++i)
    limit *= 10;

  const auto digits = QString::number(value % limit).rightJustified(kPairingCodeDigits, QLatin1Char('0'));
  QStringList groups;
  for (int i = 0; i < digits.size(); i += 3)
    groups.append(digits.mid(i, 3));
  return groups.join(QLatin1Char(' '));
}

} // namespace deskflow::gui
