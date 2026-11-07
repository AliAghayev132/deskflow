/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QModelIndex>
#include <QPixmap>
#include <QStyledItemDelegate>

/**
 * @brief Draws the cells of the computer layout grid as tiles.
 *
 * Computers are rounded cards with a monitor and their name, the server is
 * highlighted with a badge, empty cells are dashed outlines and the cell
 * under a drag is highlighted as the drop target.
 *
 * All colors come from the widget palette so the tiles follow the theme.
 */
class ComputerTileDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  explicit ComputerTileDelegate(QObject *parent = nullptr);

  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  /**
   * @brief The empty cell that should be shown as the drop target, or an invalid index for none
   */
  void setDropTarget(const QModelIndex &index);
  [[nodiscard]] QModelIndex dropTarget() const
  {
    return m_dropTarget;
  }

  /**
   * @brief Picture of a computer tile, used while dragging
   */
  static QPixmap tilePixmap(const QString &name, bool isServer, const QSize &size, const QPalette &palette);

  /**
   * @brief Picture for the "drag to add a computer" source
   */
  static QPixmap newComputerPixmap(const QSize &size, const QPalette &palette, bool enabled);

  /**
   * @brief Picture for the "drop here to remove" target
   * @param active true while a computer is dragged over it
   */
  static QPixmap removeComputerPixmap(const QSize &size, const QPalette &palette, bool active);

private:
  enum class TileState
  {
    Normal,
    Hovered,
    Selected
  };

  static void paintEmpty(QPainter *painter, const QRectF &rect, const QPalette &palette, bool dropTarget);
  static void paintComputer(
      QPainter *painter, const QRectF &rect, const QPalette &palette, const QString &name, bool isServer,
      TileState state
  );
  static void paintMonitor(QPainter *painter, const QRectF &screen, const QColor &line, const QColor &fill);

  QPersistentModelIndex m_dropTarget;
};
