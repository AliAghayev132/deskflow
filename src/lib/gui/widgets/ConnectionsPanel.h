/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QDateTime>
#include <QFrame>
#include <QHash>

class QLabel;

/**
 * @brief Card in the main window listing who is connected right now and for how long.
 *
 * As a server it lists the connected clients, as a client the server it is
 * connected or connecting to. It hides itself when there is nothing to show.
 */
class ConnectionsPanel : public QFrame
{
  Q_OBJECT

public:
  explicit ConnectionsPanel(QWidget *parent = nullptr);

  /**
   * @brief This computer is a running server with these clients connected
   */
  void showServer(const QStringList &clients);

  /**
   * @brief This computer is a client in the given state towards server
   */
  void showClient(deskflow::core::ConnectionState state, const QString &server);

  /**
   * @brief Nothing is running, hide the panel
   */
  void clear();

  /**
   * @brief Update the "connected for" times
   */
  void refresh();

  /**
   * @brief Short text for how long something has been connected, e.g. "just now" or "1 h 5 min"
   */
  static QString durationText(qint64 seconds);

  /**
   * @brief Names currently shown as connected, for testing
   */
  [[nodiscard]] QStringList connectedNames() const;

protected:
  void changeEvent(QEvent *event) override;

private:
  enum class Mode
  {
    Hidden,
    Server,
    Client
  };

  void render();
  QString since(const QString &name) const;

  QLabel *m_title = nullptr;
  QLabel *m_body = nullptr;
  Mode m_mode = Mode::Hidden;
  deskflow::core::ConnectionState m_clientState = deskflow::core::ConnectionState::Disconnected;
  QString m_server;
  QStringList m_clients;
  QHash<QString, QDateTime> m_since;
};
