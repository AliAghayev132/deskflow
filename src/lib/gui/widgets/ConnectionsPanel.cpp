/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ConnectionsPanel.h"

#include <QEvent>
#include <QLabel>
#include <QVBoxLayout>

using deskflow::core::ConnectionState;

namespace {
// Readable on light and dark backgrounds
const auto kConnectedColor = QStringLiteral("#1F9D5C");
const auto kConnectingColor = QStringLiteral("#D18F00");
} // namespace

ConnectionsPanel::ConnectionsPanel(QWidget *parent)
    : QFrame(parent),
      m_title{new QLabel(this)},
      m_body{new QLabel(this)}
{
  setObjectName(QStringLiteral("connectionsPanel"));
  setProperty("card", true);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

  m_title->setProperty("role", QStringLiteral("caption"));
  m_body->setTextFormat(Qt::RichText);
  m_body->setWordWrap(true);

  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 10, 16, 12);
  layout->setSpacing(4);
  layout->addWidget(m_title);
  layout->addWidget(m_body);

  hide();
}

QString ConnectionsPanel::durationText(qint64 seconds)
{
  if (seconds < 60)
    return tr("just now");
  const auto minutes = static_cast<int>(seconds / 60);
  if (minutes < 60)
    //: Short form of a number of minutes, e.g. "5 min"
    return tr("%n min", "", minutes);
  //: Hours and minutes, e.g. "1 h 5 min"
  return tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60);
}

void ConnectionsPanel::showServer(const QStringList &clients)
{
  const auto now = QDateTime::currentDateTime();
  for (const auto &client : clients) {
    if (!m_since.contains(client))
      m_since.insert(client, now);
  }
  // Forget clients that left, so reconnecting starts a new duration
  m_since.removeIf([&clients](const auto &entry) { return !clients.contains(entry.key()); });

  m_mode = Mode::Server;
  m_clients = clients;
  render();
}

void ConnectionsPanel::showClient(ConnectionState state, const QString &server)
{
  if (state == ConnectionState::Connected) {
    if (!m_since.contains(server) || m_server != server) {
      m_since.clear();
      m_since.insert(server, QDateTime::currentDateTime());
    }
  } else {
    m_since.clear();
  }

  m_mode = Mode::Client;
  m_clientState = state;
  m_server = server;
  render();
}

void ConnectionsPanel::clear()
{
  m_mode = Mode::Hidden;
  m_clients.clear();
  m_server.clear();
  m_since.clear();
  render();
}

void ConnectionsPanel::refresh()
{
  if (m_mode != Mode::Hidden)
    render();
}

QStringList ConnectionsPanel::connectedNames() const
{
  if (m_mode == Mode::Server)
    return m_clients;
  if (m_mode == Mode::Client && m_clientState == ConnectionState::Connected)
    return {m_server};
  return {};
}

void ConnectionsPanel::changeEvent(QEvent *event)
{
  QFrame::changeEvent(event);
  if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange)
    render();
}

QString ConnectionsPanel::since(const QString &name) const
{
  const auto start = m_since.value(name);
  if (!start.isValid())
    return {};
  return durationText(start.secsTo(QDateTime::currentDateTime()));
}

void ConnectionsPanel::render()
{
  const auto muted = palette().color(QPalette::PlaceholderText).name();
  const auto dot = QStringLiteral("<span style=\"color:%1\">&#9679;</span>");
  const auto item = QStringLiteral("%1&nbsp;<b>%2</b>&nbsp;<span style=\"color:%3\">%4</span>");

  switch (m_mode) {
  case Mode::Hidden:
    hide();
    return;

  case Mode::Server: {
    m_title->setText(tr("Connected computers"));
    if (m_clients.isEmpty()) {
      m_body->setText(QStringLiteral("<span style=\"color:%1\">%2</span>")
                          .arg(muted, tr("Waiting for your other computers to connect...").toHtmlEscaped()));
    } else {
      QStringList parts;
      for (const auto &client : std::as_const(m_clients))
        parts.append(item.arg(dot.arg(kConnectedColor), client.toHtmlEscaped(), muted, since(client)));
      m_body->setText(parts.join(QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;")));
    }
    break;
  }

  case Mode::Client: {
    m_title->setText(tr("Server"));
    const auto server = m_server.toHtmlEscaped();
    switch (m_clientState) {
    case ConnectionState::Connected:
      m_body->setText(item.arg(dot.arg(kConnectedColor), server, muted, since(m_server)));
      break;
    case ConnectionState::Connecting:
      //: %1 is the server name or address
      m_body->setText(item.arg(dot.arg(kConnectingColor), server, muted, tr("connecting...")));
      break;
    default:
      m_body->setText(item.arg(dot.arg(muted), server, muted, tr("not connected")));
      break;
    }
    break;
  }
  }

  show();
}
