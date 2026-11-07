/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QObject>

namespace deskflow::gui {

/**
 * @brief Notices when this computer's screen gets locked, and locks it on request.
 *
 * Windows reports session lock messages, macOS posts com.apple.screenIsLocked, and Linux
 * desktops send ActiveChanged from their screen saver over D-Bus.
 */
class ScreenLock : public QObject, public QAbstractNativeEventFilter
{
  Q_OBJECT

public:
  explicit ScreenLock(QObject *parent = nullptr);
  ~ScreenLock() override;

  /**
   * @brief Start watching, windowId is a native window that receives the Windows messages
   */
  void watch(quintptr windowId);

  /**
   * @brief Lock this computer now
   * @return false when no way to lock was found
   */
  bool lock();

  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

Q_SIGNALS:
  /// The screen was locked, by the person or by lock()
  void locked();

private Q_SLOTS:
  void screenSaverActiveChanged(bool active);

private:
  void reportLocked();

  bool m_watching = false;
  quintptr m_windowId = 0;
  QElapsedTimer m_lastLock;
};

} // namespace deskflow::gui
