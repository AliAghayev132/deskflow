/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenLock.h"

#include <QCoreApplication>
#include <QDebug>
#include <QProcess>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <wtsapi32.h>
#elif defined(Q_OS_MACOS)
#include "gui/OSXHelpers.h"
#else
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace deskflow::gui {

namespace {
// A lock caused by another computer's lock shouldn't bounce back and forth
constexpr qint64 kRepeatGap = 5000;
} // namespace

ScreenLock::ScreenLock(QObject *parent) : QObject(parent)
{
  // Nothing happens until watch is called
}

ScreenLock::~ScreenLock()
{
#if defined(Q_OS_WIN)
  if (m_watching) {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(m_windowId));
  }
#endif
}

void ScreenLock::watch(quintptr windowId)
{
  if (m_watching)
    return;
  m_watching = true;
  m_windowId = windowId;

#if defined(Q_OS_WIN)
  if (WTSRegisterSessionNotification(reinterpret_cast<HWND>(windowId), NOTIFY_FOR_THIS_SESSION)) {
    QCoreApplication::instance()->installNativeEventFilter(this);
  } else {
    qWarning() << "screen lock changes can't be watched, error" << GetLastError();
  }
#elif defined(Q_OS_MACOS)
  observeOSXScreenLock([this] { reportLocked(); });
#else
  // KDE and most others use the freedesktop name, GNOME its own
  auto bus = QDBusConnection::sessionBus();
  const bool freedesktop = bus.connect(
      QString(), QStringLiteral("/org/freedesktop/ScreenSaver"), QStringLiteral("org.freedesktop.ScreenSaver"),
      QStringLiteral("ActiveChanged"), this, SLOT(screenSaverActiveChanged(bool))
  );
  const bool gnome = bus.connect(
      QString(), QStringLiteral("/org/gnome/ScreenSaver"), QStringLiteral("org.gnome.ScreenSaver"),
      QStringLiteral("ActiveChanged"), this, SLOT(screenSaverActiveChanged(bool))
  );
  if (!freedesktop && !gnome)
    qWarning() << "screen lock changes can't be watched, no session bus";
#endif
}

bool ScreenLock::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
  Q_UNUSED(eventType)
  Q_UNUSED(result)
#if defined(Q_OS_WIN)
  const auto msg = static_cast<MSG *>(message);
  if (msg && msg->message == WM_WTSSESSION_CHANGE && msg->wParam == static_cast<WPARAM>(WTS_SESSION_LOCK))
    reportLocked();
#else
  Q_UNUSED(message)
#endif
  return false;
}

void ScreenLock::screenSaverActiveChanged(bool active)
{
  if (active)
    reportLocked();
}

void ScreenLock::reportLocked()
{
  if (m_lastLock.isValid() && m_lastLock.elapsed() < kRepeatGap)
    return;
  m_lastLock.start();
  qInfo() << "screen locked";
  Q_EMIT locked();
}

bool ScreenLock::lock()
{
  // Counts as our own lock, so it isn't sent back to the computer that asked for it
  m_lastLock.start();
  qInfo() << "locking the screen";

#if defined(Q_OS_WIN)
  return LockWorkStation() != 0;
#elif defined(Q_OS_MACOS)
  if (lockOSXScreen())
    return true;
  return QProcess::startDetached(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("displaysleepnow")});
#else
  if (QProcess::startDetached(QStringLiteral("loginctl"), {QStringLiteral("lock-session")}))
    return true;
  QDBusInterface screenSaver(
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
      QStringLiteral("org.freedesktop.ScreenSaver")
  );
  const QDBusReply<void> reply = screenSaver.call(QStringLiteral("Lock"));
  return reply.isValid();
#endif
}

} // namespace deskflow::gui
