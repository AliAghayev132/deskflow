/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

class QScreen;
class QWidget;

/**
 * @brief Darkens every screen of this computer while the cursor is on another one.
 *
 * Each screen gets a borderless, see-through window that ignores the mouse, so it shows where
 * the cursor isn't without getting in the way.
 */
class ScreenDimmer : public QObject
{
  Q_OBJECT

public:
  /// How dark the screens get, 0 is clear and 1 is black
  static constexpr qreal kOpacity = 0.35;
  static constexpr int kFadeTime = 150;

  explicit ScreenDimmer(QObject *parent = nullptr);
  ~ScreenDimmer() override;

  void setDimmed(bool dimmed);
  [[nodiscard]] bool isDimmed() const
  {
    return m_dimmed;
  }

  /// The windows covering the screens, for testing
  [[nodiscard]] QList<QWidget *> overlays() const;

private:
  void rebuild();
  QWidget *createOverlay(QScreen *screen);
  void fade(QWidget *overlay, bool in);

  bool m_dimmed = false;
  QList<QPointer<QWidget>> m_overlays;
};
