/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>

namespace deskflow::gui {

/**
 * @brief A short code people can read out and compare instead of a whole certificate fingerprint.
 *
 * The code has 12 digits (about 40 bits), shown in groups of three, for example "482 913 077 251".
 * The same certificate always gives the same code, so a computer can show its code all the time.
 * An empty or invalid fingerprint gives an empty string.
 */
QString pairingCode(const QByteArray &fingerprint);

inline constexpr int kPairingCodeDigits = 12;

} // namespace deskflow::gui
