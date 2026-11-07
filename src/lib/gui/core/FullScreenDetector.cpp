/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FullScreenDetector.h"

#include <cstddef>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include "gui/OSXHelpers.h"
#else
#include <QGuiApplication>
#include <dlfcn.h>
#endif

namespace deskflow::gui {

#if defined(Q_OS_WIN) || defined(_WIN32)

bool isFullScreenAppInFront()
{
  QUERY_USER_NOTIFICATION_STATE state{};
  if (SHQueryUserNotificationState(&state) != S_OK)
    return false;
  return state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_BUSY || state == QUNS_PRESENTATION_MODE;
}

#elif defined(__APPLE__)

bool isFullScreenAppInFront()
{
  return isOSXFullScreenAppInFront();
}

#else

namespace {
// libX11 is loaded when needed, so the app doesn't depend on it and Wayland sessions are untouched
struct X11
{
  using Display = void;
  using Window = unsigned long;
  using Atom = unsigned long;

  Display *(*openDisplay)(const char *) = nullptr;
  Atom (*internAtom)(Display *, const char *, int) = nullptr;
  Window (*defaultRootWindow)(Display *) = nullptr;
  int (*getWindowProperty)(
      Display *, Window, Atom, long, long, int, Atom, Atom *, int *, unsigned long *, unsigned long *, unsigned char **
  ) = nullptr;
  int (*freeData)(void *) = nullptr;
  Display *display = nullptr;
  Atom activeWindow = 0;
  Atom wmState = 0;
  Atom fullScreen = 0;

  bool load()
  {
    auto library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!library)
      return false;
    openDisplay = reinterpret_cast<decltype(openDisplay)>(dlsym(library, "XOpenDisplay"));
    internAtom = reinterpret_cast<decltype(internAtom)>(dlsym(library, "XInternAtom"));
    defaultRootWindow = reinterpret_cast<decltype(defaultRootWindow)>(dlsym(library, "XDefaultRootWindow"));
    getWindowProperty = reinterpret_cast<decltype(getWindowProperty)>(dlsym(library, "XGetWindowProperty"));
    freeData = reinterpret_cast<decltype(freeData)>(dlsym(library, "XFree"));
    if (!openDisplay || !internAtom || !defaultRootWindow || !getWindowProperty || !freeData)
      return false;
    display = openDisplay(nullptr);
    if (!display)
      return false;
    activeWindow = internAtom(display, "_NET_ACTIVE_WINDOW", 0);
    wmState = internAtom(display, "_NET_WM_STATE", 0);
    fullScreen = internAtom(display, "_NET_WM_STATE_FULLSCREEN", 0);
    return true;
  }

  // The values of an atom or window list property, at most a few dozen
  template <typename Value> std::size_t read(Window window, Atom property, Value *out, std::size_t capacity)
  {
    constexpr Atom kAnyPropertyType = 0;
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = nullptr;
    if (getWindowProperty(
            display, window, property, 0, 64, 0, kAnyPropertyType, &type, &format, &count, &remaining, &data
        ) != 0 ||
        !data)
      return 0;
    std::size_t copied = 0;
    if (format == 32) {
      // Xlib hands out 32 bit values as longs
      const auto *values = reinterpret_cast<const unsigned long *>(data);
      for (; copied < count && copied < capacity; ++copied)
        out[copied] = static_cast<Value>(values[copied]);
    }
    freeData(data);
    return copied;
  }
};
} // namespace

bool isFullScreenAppInFront()
{
  if (QGuiApplication::platformName() != QLatin1String("xcb"))
    return false;

  static X11 x11;
  static const bool loaded = x11.load();
  if (!loaded)
    return false;

  X11::Window active = 0;
  if (x11.read(x11.defaultRootWindow(x11.display), x11.activeWindow, &active, 1) != 1 || active == 0)
    return false;

  X11::Atom states[32] = {};
  const auto count = x11.read(active, x11.wmState, states, 32);
  for (std::size_t i = 0; i < count; ++i) {
    if (states[i] == x11.fullScreen)
      return true;
  }
  return false;
}

#endif

} // namespace deskflow::gui
