/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "WakeOnLan.h"

#include "ServerDiscovery.h"

#include <QDebug>
#include <QNetworkInterface>
#include <QUdpSocket>

namespace deskflow::gui {

QByteArray WakeOnLan::magicPacket(const QString &mac)
{
  const auto normalized = ServerDiscovery::normalizedMac(mac);
  if (normalized.isEmpty())
    return {};

  const auto address = QByteArray::fromHex(normalized.toLatin1().replace(':', ""));
  if (address.size() != 6)
    return {};

  QByteArray packet(6, char(0xFF));
  packet.reserve(6 + 16 * 6);
  for (int i = 0; i < 16; ++i)
    packet.append(address);
  return packet;
}

bool WakeOnLan::wake(const QString &mac, quint16 port, const QList<QHostAddress> &extraTargets)
{
  const auto packet = magicPacket(mac);
  if (packet.isEmpty()) {
    qWarning().noquote() << "wake on lan skipped, invalid mac address:" << mac;
    return false;
  }

  QList<QHostAddress> targets{QHostAddress(QHostAddress::Broadcast)};
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : interfaces) {
    const auto flags = interface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::CanBroadcast) ||
        flags.testFlag(QNetworkInterface::IsLoopBack))
      continue;
    const auto entries = interface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull() &&
          !targets.contains(entry.broadcast()))
        targets.append(entry.broadcast());
    }
  }
  for (const auto &extra : extraTargets) {
    if (!targets.contains(extra))
      targets.append(extra);
  }

  QUdpSocket socket;
  bool sent = false;
  for (const auto &target : targets) {
    if (socket.writeDatagram(packet, target, port) == packet.size())
      sent = true;
  }

  qInfo().noquote() << "wake on lan packet for" << ServerDiscovery::normalizedMac(mac)
                    << (sent ? "sent" : "could not be sent");
  return sent;
}

} // namespace deskflow::gui
