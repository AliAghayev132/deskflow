/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace deskflow::gui::theme {

/**
 * @brief Colors used by the application theme
 */
struct Colors
{
  QColor window;      // window background
  QColor card;        // cards, inputs and menus
  QColor cardHover;   // hovered cards and buttons
  QColor border;      // card and input borders
  QColor borderHover; // hovered borders
  QColor text;        // main text
  QColor muted;       // captions and secondary text
  QColor accent;      // primary actions and selection
  QColor accentHover; // hovered primary action
  QColor accentText;  // text on the accent color
  QColor accentSoft;  // background of selected items
  QColor danger;      // stop and destructive actions
  QColor dangerSoft;  // background of stop actions
};

/**
 * @brief Apply the Deskflow look (Fusion style, palette and style sheet) to the whole application.
 * Follows the system light / dark setting and updates when it changes.
 */
void apply();

/**
 * @brief Whether the dark variant of the theme is in use
 */
bool isDark();

/**
 * @brief Colors of the variant in use
 */
const Colors &colors();

/**
 * @brief The style sheet for a set of colors, exposed for testing
 */
QString styleSheet(const Colors &colors);

/**
 * @brief Line icons for the two mode cards, drawn in the accent color
 */
QIcon serverModeIcon();
QIcon clientModeIcon();

} // namespace deskflow::gui::theme
