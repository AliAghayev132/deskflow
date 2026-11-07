/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>

class QProcess;

namespace deskflow::gui {

/**
 * @brief A computer in the same Tailscale network
 */
struct TailscalePeer
{
  QString name;
  QHostAddress address; ///< its Tailscale IPv4 address, 100.x.y.z
  bool online = false;
};

/**
 * @brief Lists the computers of this computer's Tailscale network, so they can be found from
 * anywhere and not only on the same local network.
 *
 * Uses the tailscale command line tool, nothing happens when Tailscale isn't installed.
 */
class Tailscale : public QObject
{
  Q_OBJECT

public:
  explicit Tailscale(QObject *parent = nullptr);

  /// Start listing the peers, peersFound follows (empty when Tailscale isn't there)
  void lookup();

  /// Path of the tailscale tool, empty when it isn't installed
  static QString executable();

  /// The peers in the output of "tailscale status --json"
  static QList<TailscalePeer> parseStatus(const QByteArray &json);

  /// True for addresses Tailscale hands out (100.64.0.0/10)
  static bool isTailscaleAddress(const QHostAddress &address);

Q_SIGNALS:
  void peersFound(const QList<deskflow::gui::TailscalePeer> &peers);

private:
  QPointer<QProcess> m_process;
};

} // namespace deskflow::gui
