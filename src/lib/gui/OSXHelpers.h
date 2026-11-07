/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

#include <functional>

void requestOSXNotificationPermission();
bool isOSXDevelopmentBuild();
bool showOSXNotification(const QString &title, const QString &body);
bool isOSXInterfaceStyleDark();
void forceAppActive();
void macOSNativeHide();
void installQuitHandler(std::function<bool()> shouldQuit);

/**
 * @brief Calls onLocked whenever the screen gets locked
 */
void observeOSXScreenLock(std::function<void()> onLocked);

/**
 * @brief Locks the screen right away, returns false when the system offers no way to
 */
bool lockOSXScreen();

/**
 * @brief True when the app in front has a window covering a whole display, e.g. a game
 */
bool isOSXFullScreenAppInFront();
