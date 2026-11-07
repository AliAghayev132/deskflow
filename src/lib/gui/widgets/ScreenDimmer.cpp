/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenDimmer.h"

#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QScreen>
#include <QWidget>

ScreenDimmer::ScreenDimmer(QObject *parent) : QObject(parent)
{
  // Screens come and go, e.g. when a monitor is plugged in
  connect(qApp, &QGuiApplication::screenAdded, this, &ScreenDimmer::rebuild);
  connect(qApp, &QGuiApplication::screenRemoved, this, &ScreenDimmer::rebuild);
}

ScreenDimmer::~ScreenDimmer()
{
  for (const auto &overlay : std::as_const(m_overlays))
    delete overlay;
}

QList<QWidget *> ScreenDimmer::overlays() const
{
  QList<QWidget *> list;
  for (const auto &overlay : m_overlays) {
    if (overlay)
      list.append(overlay);
  }
  return list;
}

void ScreenDimmer::setDimmed(bool dimmed)
{
  if (m_dimmed == dimmed)
    return;
  m_dimmed = dimmed;

  if (dimmed && m_overlays.isEmpty())
    rebuild();
  for (const auto &overlay : std::as_const(m_overlays)) {
    if (overlay)
      fade(overlay, dimmed);
  }
}

QWidget *ScreenDimmer::createOverlay(QScreen *screen)
{
  auto overlay = new QWidget(
      nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                   Qt::WindowDoesNotAcceptFocus | Qt::X11BypassWindowManagerHint | Qt::NoDropShadowWindowHint
  );
  overlay->setObjectName(QStringLiteral("screenDimmer"));
  overlay->setAttribute(Qt::WA_ShowWithoutActivating);
  overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
  overlay->setFocusPolicy(Qt::NoFocus);
  overlay->setStyleSheet(QStringLiteral("background-color: black;"));
  overlay->setAutoFillBackground(true);
  overlay->setGeometry(screen->geometry());
  overlay->setWindowOpacity(0.0);
  return overlay;
}

void ScreenDimmer::rebuild()
{
  for (const auto &overlay : std::as_const(m_overlays))
    delete overlay;
  m_overlays.clear();

  const auto screens = QGuiApplication::screens();
  for (auto screen : screens)
    m_overlays.append(createOverlay(screen));

  if (m_dimmed) {
    for (const auto &overlay : std::as_const(m_overlays))
      fade(overlay, true);
  }
}

void ScreenDimmer::fade(QWidget *overlay, bool in)
{
  if (in) {
    overlay->show();
    overlay->raise();
  }

  auto animation = new QPropertyAnimation(overlay, "windowOpacity", overlay);
  animation->setDuration(kFadeTime);
  animation->setStartValue(overlay->windowOpacity());
  animation->setEndValue(in ? kOpacity : 0.0);
  if (!in) {
    connect(animation, &QPropertyAnimation::finished, overlay, [this, overlay] {
      // A dim that came back during the fade keeps the window
      if (!m_dimmed)
        overlay->hide();
    });
  }
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}
