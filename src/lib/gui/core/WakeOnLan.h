/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>

namespace deskflow::gui {

/**
 * @brief Wakes a sleeping computer on the local network with a "magic packet".
 *
 * The target's network adapter must have Wake-on-LAN enabled (usually in the
 * BIOS / UEFI and the adapter's power settings) and be on the same network.
 */
class WakeOnLan
{
public:
  static constexpr quint16 kDefaultPort = 9;

  /**
   * @brief The magic packet for a MAC address: 6 bytes of 0xFF followed by the MAC 16 times
   * @return an empty array when the MAC address isn't valid
   */
  static QByteArray magicPacket(const QString &mac);

  /**
   * @brief Broadcast the magic packet on every local network
   * @param extraTargets addresses to send to as well, e.g. loopback in tests
   * @return true when the packet was sent at least once
   */
  static bool wake(const QString &mac, quint16 port = kDefaultPort, const QList<QHostAddress> &extraTargets = {});
};

} // namespace deskflow::gui
