/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NetworkMonitor.h"

#include <QAbstractSocket>
#include <QList>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace deskflow::gui {

bool NetworkMonitor::isVirtualInterface(const QString &interfaceName)
{
  // Common virtual network interface patterns
  static const auto virtualRegEx = QRegularExpression(
      QStringLiteral("^vboxnet|vmnet|docker|virbr|veth|br\\-|tun|utun|awdl|p2p|llw|anpi|tap|vEth"),
      QRegularExpression::CaseInsensitiveOption
  );
  return virtualRegEx.match(interfaceName).hasMatch();
}

NetworkMonitor::NetworkMonitor(QObject *parent) : QObject(parent), m_checkTimer(new QTimer(this))
{
  connect(m_checkTimer, &QTimer::timeout, this, &NetworkMonitor::updateNetworkState);
}

void NetworkMonitor::startMonitoring(int intervalMs)
{
  if (m_isMonitoring) {
    return;
  }

  updateNetworkState();

  m_checkTimer->start(intervalMs);
  m_isMonitoring = true;
}

void NetworkMonitor::stopMonitoring()
{
  if (!m_isMonitoring) {
    return;
  }

  m_checkTimer->stop();
  m_isMonitoring = false;
}

QStringList NetworkMonitor::validAddresses()
{
  QList<QHostAddress> physicalIP4;
  QList<QHostAddress> physicalIP6;
  QList<QHostAddress> virtualIP4;
  QList<QHostAddress> virtualIP6;
  QSet<QHostAddress> uniqueAddresses;

  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : allInterfaces) {
    if (!(interface.flags() & QNetworkInterface::IsUp) || !(interface.flags() & QNetworkInterface::IsRunning) ||
        (interface.flags() & QNetworkInterface::IsLoopBack)) {
      continue;
    }

    const bool isP2P = (interface.flags() & QNetworkInterface::IsPointToPoint);
    const bool isVirtualType = interface.type() == QNetworkInterface::Virtual;
    const bool isVirtual = isVirtualInterface(interface.humanReadableName()) || isP2P || isVirtualType;
    const auto addressEntries = interface.addressEntries();
    // link-local is only worth offering when it's a physical interface's only address, e.g. a direct ethernet cable
    const bool hasNonLinkLocal = std::ranges::any_of(addressEntries, [](const QNetworkAddressEntry &entry) {
      return !entry.ip().isLoopback() && !entry.ip().isLinkLocal();
    });
    const bool skipLinkLocal = isVirtual || hasNonLinkLocal;

    for (const auto &entry : addressEntries) {
      const QHostAddress address = entry.ip();

      if (address.isLoopback() || (skipLinkLocal && address.isLinkLocal()) || uniqueAddresses.contains(address)) {
        continue;
      }

      uniqueAddresses.insert(address);

      if (address.protocol() == QHostAddress::IPv6Protocol) {
        if (isVirtual)
          virtualIP6.append(address);
        else
          physicalIP6.append(address);
      } else {
        if (isVirtual)
          virtualIP4.append(address);
        else
          physicalIP4.append(address);
      }
    }
  }

  std::ranges::sort(physicalIP4, [](const QHostAddress &a, const QHostAddress &b) {
    if (a.isPrivateUse() != b.isPrivateUse())
      return a.isPrivateUse();
    if (a.isLinkLocal() != b.isLinkLocal())
      return a.isLinkLocal();
    return a.toIPv4Address() < b.toIPv4Address();
  });

  std::ranges::sort(virtualIP4, [](const QHostAddress &a, const QHostAddress &b) {
    return a.toIPv4Address() < b.toIPv4Address();
  });

  std::ranges::sort(physicalIP6, [](const QHostAddress &a, const QHostAddress &b) {
    if (a.isPrivateUse() != b.isPrivateUse())
      return a.isPrivateUse();
    if (a.isLinkLocal() != b.isLinkLocal())
      return a.isLinkLocal();
    return a.toString() < b.toString();
  });

  std::ranges::sort(virtualIP6, [](const QHostAddress &a, const QHostAddress &b) {
    return a.toString() < b.toString();
  });

  auto result = physicalIP4;
  result.append(virtualIP4);
  result.append(physicalIP6);
  result.append(virtualIP6);

  QStringList ipList;
  for (const auto &host : result) {
    ipList.append(host.toString());
  }
  return ipList;
}

void NetworkMonitor::setIpAddresses(const QStringList &newAddresses)
{
  if (newAddresses == m_lastAddresses)
    return;
  m_lastAddresses = newAddresses;
  Q_EMIT ipAddressesChanged(m_lastAddresses);
}

void NetworkMonitor::updateNetworkState()
{
  setIpAddresses(validAddresses());
}

QString NetworkMonitor::networkOf(const QHostAddress &address, int prefixLength)
{
  if (address.protocol() != QAbstractSocket::IPv4Protocol || prefixLength <= 0 || prefixLength > 32)
    return {};
  const quint32 mask = prefixLength == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> prefixLength);
  const QHostAddress network(address.toIPv4Address() & mask);
  return QStringLiteral("%1/%2").arg(network.toString()).arg(prefixLength);
}

QStringList NetworkMonitor::localNetworks()
{
  QStringList networks;
  const auto allInterfaces = QNetworkInterface::allInterfaces();
  for (const auto &interface : allInterfaces) {
    const auto flags = interface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) ||
        flags.testFlag(QNetworkInterface::IsLoopBack) || flags.testFlag(QNetworkInterface::IsPointToPoint) ||
        interface.type() == QNetworkInterface::Virtual || isVirtualInterface(interface.humanReadableName()))
      continue;

    const auto entries = interface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().isLinkLocal())
        continue;
      if (const auto network = networkOf(entry.ip(), entry.prefixLength());
          !network.isEmpty() && !networks.contains(network))
        networks.append(network);
    }
  }
  networks.sort();
  return networks;
}

} // namespace deskflow::gui
