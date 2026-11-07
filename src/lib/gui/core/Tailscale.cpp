/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Tailscale.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace deskflow::gui {

namespace {
constexpr int kLookupTimeout = 3000;
} // namespace

Tailscale::Tailscale(QObject *parent) : QObject(parent)
{
  // Nothing runs until lookup
}

QString Tailscale::executable()
{
  if (const auto found = QStandardPaths::findExecutable(QStringLiteral("tailscale")); !found.isEmpty())
    return found;

  // Where the installers put it when it isn't on the PATH
  const QStringList candidates{
#if defined(Q_OS_WIN)
      QStringLiteral("C:/Program Files/Tailscale/tailscale.exe"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("/Applications/Tailscale.app/Contents/MacOS/Tailscale"),
      QStringLiteral("/opt/homebrew/bin/tailscale"),
      QStringLiteral("/usr/local/bin/tailscale"),
#else
      QStringLiteral("/usr/bin/tailscale"),
      QStringLiteral("/usr/local/bin/tailscale"),
#endif
  };
  for (const auto &candidate : candidates) {
    if (QFileInfo(candidate).isExecutable())
      return candidate;
  }
  return {};
}

bool Tailscale::isTailscaleAddress(const QHostAddress &address)
{
  return address.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10);
}

QList<TailscalePeer> Tailscale::parseStatus(const QByteArray &json)
{
  QList<TailscalePeer> peers;
  const auto doc = QJsonDocument::fromJson(json);
  if (!doc.isObject())
    return peers;

  const auto peerMap = doc.object().value(QStringLiteral("Peer")).toObject();
  for (const auto &value : peerMap) {
    const auto node = value.toObject();
    TailscalePeer peer;
    peer.online = node.value(QStringLiteral("Online")).toBool();
    peer.name = node.value(QStringLiteral("HostName")).toString();
    const auto addresses = node.value(QStringLiteral("TailscaleIPs")).toArray();
    for (const auto &address : addresses) {
      QHostAddress ip(address.toString());
      if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
        peer.address = ip;
        break;
      }
    }
    if (!peer.address.isNull())
      peers.append(peer);
  }
  return peers;
}

void Tailscale::lookup()
{
  if (m_process)
    return;

  const auto tool = executable();
  if (tool.isEmpty()) {
    Q_EMIT peersFound({});
    return;
  }

  m_process = new QProcess(this);
  connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
    auto process = m_process;
    m_process = nullptr;
    process->deleteLater();
    QList<TailscalePeer> peers;
    if (status == QProcess::NormalExit && exitCode == 0)
      peers = parseStatus(process->readAllStandardOutput());
    else
      qDebug().noquote() << "tailscale status failed:" << process->readAllStandardError().trimmed();
    Q_EMIT peersFound(peers);
  });
  connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || !m_process)
      return;
    m_process->deleteLater();
    m_process = nullptr;
    Q_EMIT peersFound({});
  });

  // A stuck tool must not hold up the search
  QTimer::singleShot(kLookupTimeout, m_process, [process = m_process] {
    if (process && process->state() != QProcess::NotRunning)
      process->kill();
  });
  m_process->start(tool, {QStringLiteral("status"), QStringLiteral("--json")});
}

} // namespace deskflow::gui
