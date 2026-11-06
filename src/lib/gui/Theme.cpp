/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Theme.h"

#include "StyleUtils.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleHints>
#include <QTemporaryDir>

namespace deskflow::gui::theme {

namespace {

const Colors kLight{
    .window = QColor(0xF3, 0xF5, 0xF8),
    .card = QColor(0xFF, 0xFF, 0xFF),
    .cardHover = QColor(0xF7, 0xF9, 0xFC),
    .border = QColor(0xDF, 0xE3, 0xEA),
    .borderHover = QColor(0xB9, 0xC4, 0xD4),
    .text = QColor(0x1A, 0x1F, 0x29),
    .muted = QColor(0x66, 0x70, 0x85),
    .accent = QColor(0x25, 0x63, 0xEB),
    .accentHover = QColor(0x1D, 0x4F, 0xD7),
    .accentText = QColor(0xFF, 0xFF, 0xFF),
    .accentSoft = QColor(0xEB, 0xF2, 0xFF),
    .danger = QColor(0xD1, 0x43, 0x43),
    .dangerSoft = QColor(0xFD, 0xEE, 0xEE),
};

const Colors kDark{
    .window = QColor(0x14, 0x16, 0x1B),
    .card = QColor(0x1E, 0x21, 0x28),
    .cardHover = QColor(0x25, 0x29, 0x31),
    .border = QColor(0x2F, 0x34, 0x3D),
    .borderHover = QColor(0x4A, 0x52, 0x60),
    .text = QColor(0xE6, 0xE9, 0xEE),
    .muted = QColor(0x9A, 0xA3, 0xB2),
    .accent = QColor(0x4D, 0x8D, 0xFF),
    .accentHover = QColor(0x6B, 0xA1, 0xFF),
    .accentText = QColor(0xFF, 0xFF, 0xFF),
    .accentSoft = QColor(0x1B, 0x2A, 0x45),
    .danger = QColor(0xF0, 0x6A, 0x6A),
    .dangerSoft = QColor(0x3A, 0x22, 0x25),
};

// Detected before our own palette is applied, used when the platform doesn't report a color scheme
bool s_platformDark = false;
bool s_dark = false;
bool s_connected = false;

QString rgba(const QColor &color)
{
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

bool detectDark()
{
  // Lets users (and screenshots) force a variant: DESKFLOW_THEME=dark or DESKFLOW_THEME=light
  if (const auto forced = qEnvironmentVariable("DESKFLOW_THEME").toLower(); forced == QLatin1String("dark"))
    return true;
  else if (forced == QLatin1String("light"))
    return false;

  switch (QGuiApplication::styleHints()->colorScheme()) {
  case Qt::ColorScheme::Dark:
    return true;
  case Qt::ColorScheme::Light:
    return false;
  default:
    return s_platformDark;
  }
}

/**
 * @brief Folder holding the small images the style sheet refers to, regenerated for each variant
 */
QString imagesDir()
{
  static QTemporaryDir dir;
  return dir.path();
}

void writeChevron(const QString &fileName, const QColor &color, qreal scale, bool up = false)
{
  const QSizeF size(12, 12);
  QImage image((size * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(scale, scale);
  QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  if (up)
    painter.drawPolyline(QPolygonF{{2.5, 7.5}, {6.0, 4.0}, {9.5, 7.5}});
  else
    painter.drawPolyline(QPolygonF{{2.5, 4.5}, {6.0, 8.0}, {9.5, 4.5}});
  painter.end();
  image.save(fileName);
}

void writeImages(const Colors &c)
{
  const auto dir = imagesDir();
  writeChevron(QStringLiteral("%1/chevron.png").arg(dir), c.muted, 1);
  writeChevron(QStringLiteral("%1/chevron@2x.png").arg(dir), c.muted, 2);
  writeChevron(QStringLiteral("%1/chevron-disabled.png").arg(dir), c.border, 1);
  writeChevron(QStringLiteral("%1/chevron-disabled@2x.png").arg(dir), c.border, 2);
  writeChevron(QStringLiteral("%1/chevron-up.png").arg(dir), c.muted, 1, true);
  writeChevron(QStringLiteral("%1/chevron-up@2x.png").arg(dir), c.muted, 2, true);
}

QPalette palette(const Colors &c)
{
  QPalette p;
  p.setColor(QPalette::Window, c.window);
  p.setColor(QPalette::WindowText, c.text);
  p.setColor(QPalette::Base, c.card);
  p.setColor(QPalette::AlternateBase, c.cardHover);
  p.setColor(QPalette::ToolTipBase, c.card);
  p.setColor(QPalette::ToolTipText, c.text);
  p.setColor(QPalette::PlaceholderText, c.muted);
  p.setColor(QPalette::Text, c.text);
  p.setColor(QPalette::Button, c.card);
  p.setColor(QPalette::ButtonText, c.text);
  p.setColor(QPalette::BrightText, c.danger);
  p.setColor(QPalette::Light, c.card);
  p.setColor(QPalette::Midlight, c.cardHover);
  p.setColor(QPalette::Mid, c.border);
  p.setColor(QPalette::Dark, c.borderHover);
  p.setColor(QPalette::Shadow, QColor(0, 0, 0, 60));
  p.setColor(QPalette::Highlight, c.accent);
  p.setColor(QPalette::HighlightedText, c.accentText);
  p.setColor(QPalette::Link, c.accent);
  p.setColor(QPalette::LinkVisited, c.danger);
  p.setColor(QPalette::Accent, c.accent);

  for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
    p.setColor(QPalette::Disabled, role, c.muted);
  }
  p.setColor(QPalette::Disabled, QPalette::Base, c.window);
  p.setColor(QPalette::Disabled, QPalette::Button, c.window);
  return p;
}

void applyCurrent()
{
  s_dark = detectDark();
  const auto &c = colors();
  writeImages(c);
  QApplication::setPalette(palette(c));
  qApp->setStyleSheet(styleSheet(c));
  updateIconTheme();
}

QIcon drawIcon(const std::function<void(QPainter &)> &draw)
{
  QIcon icon;
  for (const qreal scale : {1.0, 2.0}) {
    QPixmap pixmap((QSizeF(40, 40) * scale).toSize());
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    draw(painter);
    painter.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

QPen linePen(const QColor &color, qreal width = 2.0)
{
  return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

} // namespace

void apply()
{
  // Remember what the platform looks like before our palette replaces it
  if (!s_connected)
    s_platformDark = isDarkMode();

  QApplication::setStyle(QStringLiteral("fusion"));

  // Small default fonts (9pt on Windows) make the window look cramped
  auto font = QApplication::font();
  if (font.pointSizeF() > 0 && font.pointSizeF() < 10) {
    font.setPointSizeF(10);
    QApplication::setFont(font);
  }

  applyCurrent();

  if (!s_connected) {
    s_connected = true;
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] { applyCurrent(); });
  }
}

bool isDark()
{
  return s_dark;
}

const Colors &colors()
{
  return s_dark ? kDark : kLight;
}

QString styleSheet(const Colors &c)
{
  // clang-format off
  QString sheet = QStringLiteral(R"(
QMainWindow, QDialog { background: @window; }
QWidget#topLevelWidget { background: @window; }

QToolTip { background: @card; color: @text; border: 1px solid @border; border-radius: 6px; padding: 6px 8px; }

/* Cards */
QFrame[card="true"] { background: @card; border: 1px solid @border; border-radius: 12px; }
QFrame[card="true"] QLabel { background: transparent; }
QLabel[role="caption"] { color: @muted; font-size: 9pt; }
QLabel[role="title"] { color: @text; font-size: 15pt; font-weight: 600; }
QLabel[role="heading"] { color: @muted; font-weight: 600; padding: 4px 2px 0px 2px; }
QLabel[role="hint"] { color: @muted; }
QLabel#lblIpAddresses { color: @muted; }
QLabel#lblNoMode { color: @muted; padding: 6px; }

/* Mode cards */
QRadioButton[card="true"] {
  background: @card; color: @text; border: 1px solid @border; border-radius: 12px;
  padding: 15px 16px; spacing: 0px; text-align: left;
}
QRadioButton[card="true"]:hover { border-color: @borderHover; background: @cardHover; }
QRadioButton[card="true"]:checked { border: 2px solid @accent; padding: 14px 15px; background: @accentSoft; }
QRadioButton[card="true"]::indicator { width: 0px; height: 0px; margin: 0px; }

/* Buttons */
QPushButton {
  background: @card; color: @text; border: 1px solid @border; border-radius: 8px;
  padding: 6px 14px; min-height: 20px;
}
QPushButton:hover { background: @cardHover; border-color: @borderHover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled { color: @muted; background: @window; border-color: @border; }
QPushButton:flat { background: transparent; border: none; padding: 4px; }
QPushButton:flat:hover { background: @cardHover; border-radius: 6px; }
QPushButton:default { border-color: @accent; }

QPushButton[primary="true"] {
  background: @accent; color: @accentText; border: 1px solid @accent; font-weight: 600;
  padding: 8px 22px; min-width: 110px;
}
QPushButton[primary="true"]:hover { background: @accentHover; border-color: @accentHover; }
QPushButton[primary="true"]:disabled { background: @border; border-color: @border; color: @muted; }
QPushButton[primary="true"][running="true"] { background: @dangerSoft; color: @danger; border-color: @danger; }
QPushButton[primary="true"][running="true"]:hover { background: @danger; color: @accentText; }

QToolButton { background: transparent; border: none; border-radius: 6px; padding: 4px; }
QToolButton:hover { background: @cardHover; }
QToolButton::menu-indicator { image: none; width: 0px; }

/* Inputs */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit, QListView, QTreeView, QTableView {
  background: @card; color: @text; border: 1px solid @border; border-radius: 8px;
  selection-background-color: @accent; selection-color: @accentText;
}
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { padding: 5px 9px; min-height: 20px; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: @borderHover; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { background: @window; color: @muted; }
QComboBox QLineEdit { border: none; background: transparent; padding: 0px; min-height: 0px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 24px; border: none; }
QComboBox::down-arrow { image: url(@images/chevron.png); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url(@images/chevron-disabled.png); }
QSpinBox, QDoubleSpinBox { padding-right: 22px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; border: none; background: transparent; margin-top: 2px; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; border: none; background: transparent; margin-bottom: 2px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover { background: @cardHover; border-radius: 4px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(@images/chevron-up.png); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(@images/chevron.png); width: 10px; height: 10px; }
QComboBox QAbstractItemView {
  background: @card; border: 1px solid @border; border-radius: 8px; padding: 4px; outline: none;
  selection-background-color: @accentSoft; selection-color: @text;
}

/* Menus */
QMenuBar { background: @window; color: @text; padding: 2px 6px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: @cardHover; }
QMenu { background: @card; color: @text; border: 1px solid @border; border-radius: 10px; padding: 5px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::item:disabled { color: @muted; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QMenu::icon { padding-left: 8px; }

/* Group boxes in dialogs */
QGroupBox { border: 1px solid @border; border-radius: 10px; margin-top: 18px; padding: 12px 10px 10px 10px; background: @card; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0px 4px; color: @muted; font-weight: 600; }

/* Tabs */
QTabWidget::pane { border: 1px solid @border; border-radius: 10px; background: @card; top: -1px; }
QTabBar::tab { background: transparent; color: @muted; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover:!selected { color: @text; }

/* Status bar and log dock */
QStatusBar { background: @card; color: @muted; border-top: 1px solid @border; padding-left: 8px; min-height: 26px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @muted; padding: 0px 4px; }
QDockWidget { color: @muted; }
QDockWidget::title { background: @window; padding: 6px 10px; text-align: left; }

/* Scroll bars */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @border; border-radius: 3px; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: @borderHover; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0px; height: 0px; }
)");
  // clang-format on

  // Longer names first so "@accentHover" isn't replaced by "@accent" + "Hover"
  const QList<std::pair<QString, QString>> tokens = {
      {QStringLiteral("@borderHover"), rgba(c.borderHover)},
      {QStringLiteral("@accentHover"), rgba(c.accentHover)},
      {QStringLiteral("@accentText"), rgba(c.accentText)},
      {QStringLiteral("@accentSoft"), rgba(c.accentSoft)},
      {QStringLiteral("@dangerSoft"), rgba(c.dangerSoft)},
      {QStringLiteral("@cardHover"), rgba(c.cardHover)},
      {QStringLiteral("@window"), rgba(c.window)},
      {QStringLiteral("@border"), rgba(c.border)},
      {QStringLiteral("@accent"), rgba(c.accent)},
      {QStringLiteral("@danger"), rgba(c.danger)},
      {QStringLiteral("@muted"), rgba(c.muted)},
      {QStringLiteral("@card"), rgba(c.card)},
      {QStringLiteral("@text"), rgba(c.text)},
      {QStringLiteral("@images"), imagesDir()},
  };
  for (const auto &[token, value] : tokens)
    sheet.replace(token, value);
  return sheet;
}

QIcon serverModeIcon()
{
  const auto &c = colors();
  return drawIcon([&c](QPainter &p) {
    // Keyboard and mouse sharing outwards
    p.setPen(linePen(c.accent));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(4, 17, 24, 14), 3, 3);
    p.setPen(linePen(c.accent, 2.4));
    for (qreal x : {9.0, 14.0, 19.0, 24.0})
      p.drawPoint(QPointF(x, 22));
    p.drawLine(QPointF(10, 26.5), QPointF(22, 26.5));
    p.setPen(linePen(c.accent));
    QPainterPath mouse;
    mouse.addRoundedRect(QRectF(31, 19, 8, 12), 4, 4);
    p.drawPath(mouse);
    p.drawLine(QPointF(35, 19), QPointF(35, 23));
    // Arrows pointing out
    p.setPen(linePen(c.muted, 1.8));
    p.drawLine(QPointF(14, 11), QPointF(26, 11));
    p.drawPolyline(QPolygonF{{22.5, 7.5}, {26, 11}, {22.5, 14.5}});
  });
}

QIcon clientModeIcon()
{
  const auto &c = colors();
  return drawIcon([&c](QPainter &p) {
    // Laptop receiving input
    p.setPen(linePen(c.accent));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(9, 13, 24, 16), 2.5, 2.5);
    p.drawLine(QPointF(5, 33), QPointF(37, 33));
    // Cursor inside the screen
    p.setPen(linePen(c.accent, 1.6));
    p.setBrush(c.accent);
    p.drawPolygon(QPolygonF{{19, 17}, {19, 25}, {21.2, 22.8}, {23, 26}, {24.4, 25.3}, {22.6, 22.2}, {25.6, 22}});
    // Arrow coming in
    p.setPen(linePen(c.muted, 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(1, 7), QPointF(13, 7));
    p.drawPolyline(QPolygonF{{9.5, 3.5}, {13, 7}, {9.5, 10.5}});
  });
}

} // namespace deskflow::gui::theme
