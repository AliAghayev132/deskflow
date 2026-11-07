/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

namespace deskflow::gui {

/**
 * @brief True while a game or other full screen app is in front on this computer.
 *
 * Windows asks the shell (the same check that holds back notifications during games),
 * macOS looks for a window of the front app that covers a whole display, and X11 checks
 * the active window's full screen state. Wayland offers no way to know, there it is false.
 */
bool isFullScreenAppInFront();

} // namespace deskflow::gui
