/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ComputerTileDelegate.h"

#include "ComputerSetupModel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr qreal kCellMargin = 6;
constexpr qreal kRadius = 10;

// Red that reads well on light and dark backgrounds, the palette has no role for it
const QColor kDanger(0xD6, 0x45, 0x45);

QColor withAlpha(QColor color, int alpha)
{
  color.setAlpha(alpha);
  return color;
}

QPen roundPen(const QColor &color, qreal width)
{
  return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QPixmap makePixmap(const QSize &size, const std::function<void(QPainter &, const QRectF &)> &draw)
{
  const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
  QPixmap pixmap(size * dpr);
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  draw(painter, QRectF(QPointF(0, 0), QSizeF(size)));
  painter.end();
  return pixmap;
}

void drawCaption(QPainter &painter, const QRectF &rect, const QString &text, const QColor &color)
{
  auto font = QApplication::font();
  font.setPointSizeF(font.pointSizeF() * 0.9);
  painter.setFont(font);
  painter.setPen(color);
  const QFontMetricsF metrics(font);
  painter.drawText(rect, Qt::AlignHCenter | Qt::AlignTop, metrics.elidedText(text, Qt::ElideRight, rect.width()));
}

} // namespace

ComputerTileDelegate::ComputerTileDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
  // do nothing
}

void ComputerTileDelegate::setDropTarget(const QModelIndex &index)
{
  m_dropTarget = index;
}

void ComputerTileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
  const auto *view = qobject_cast<const QAbstractItemView *>(option.widget);
  const auto &palette = view ? view->palette() : option.palette;
  const QRectF rect = QRectF(option.rect).adjusted(kCellMargin, kCellMargin, -kCellMargin, -kCellMargin);

  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->setRenderHint(QPainter::TextAntialiasing);

  const auto name = index.data(Qt::DisplayRole).toString();
  if (name.isEmpty()) {
    paintEmpty(painter, rect, palette, m_dropTarget.isValid() && m_dropTarget == index);
  } else {
    TileState state = TileState::Normal;
    if (option.state & QStyle::State_Selected)
      state = TileState::Selected;
    else if ((option.state & QStyle::State_MouseOver) || (m_dropTarget.isValid() && m_dropTarget == index))
      state = TileState::Hovered;
    const bool isServer = index.data(ComputerSetupModel::IsServerRole).toBool();
    paintComputer(painter, rect, palette, name, isServer, state);
  }

  painter->restore();
}

void ComputerTileDelegate::paintEmpty(QPainter *painter, const QRectF &rect, const QPalette &palette, bool dropTarget)
{
  const auto accent = palette.color(QPalette::Highlight);
  const auto inner = rect.adjusted(0.5, 0.5, -0.5, -0.5);

  if (dropTarget) {
    painter->setPen(roundPen(accent, 2));
    painter->setBrush(withAlpha(accent, 30));
    painter->drawRoundedRect(inner.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    // A "+" in the middle shows where the computer will land
    const auto center = inner.center();
    const qreal arm = std::min(inner.width(), inner.height()) * 0.12;
    painter->setPen(roundPen(accent, 2));
    painter->drawLine(QPointF(center.x() - arm, center.y()), QPointF(center.x() + arm, center.y()));
    painter->drawLine(QPointF(center.x(), center.y() - arm), QPointF(center.x(), center.y() + arm));
    return;
  }

  QPen pen(withAlpha(palette.color(QPalette::Mid), 220), 1, Qt::CustomDashLine);
  pen.setDashPattern({4, 4});
  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  painter->drawRoundedRect(inner, kRadius, kRadius);
}

void ComputerTileDelegate::paintMonitor(QPainter *painter, const QRectF &screen, const QColor &line, const QColor &fill)
{
  const qreal stroke = std::max<qreal>(1.6, screen.height() / 16.0);
  painter->setPen(roundPen(line, stroke));
  painter->setBrush(fill);
  painter->drawRoundedRect(screen, screen.height() * 0.1, screen.height() * 0.1);

  // Stand and foot
  const qreal neck = screen.height() * 0.22;
  const qreal footHalf = screen.width() * 0.2;
  const QPointF top(screen.center().x(), screen.bottom());
  const QPointF bottom(top.x(), top.y() + neck);
  painter->setBrush(Qt::NoBrush);
  painter->drawLine(top, bottom);
  painter->drawLine(QPointF(bottom.x() - footHalf, bottom.y()), QPointF(bottom.x() + footHalf, bottom.y()));
}

void ComputerTileDelegate::paintComputer(
    QPainter *painter, const QRectF &rect, const QPalette &palette, const QString &name, bool isServer, TileState state
)
{
  const auto accent = palette.color(QPalette::Highlight);
  const auto card = palette.color(QPalette::Base);
  const auto text = palette.color(QPalette::Text);
  const auto muted = palette.color(QPalette::PlaceholderText);
  auto border = palette.color(QPalette::Mid);
  qreal borderWidth = 1;

  if (state == TileState::Selected || isServer) {
    border = accent;
    borderWidth = state == TileState::Selected ? 2 : 1.5;
  } else if (state == TileState::Hovered) {
    border = palette.color(QPalette::Dark);
  }

  // Card
  const auto inner = rect.adjusted(borderWidth / 2, borderWidth / 2, -borderWidth / 2, -borderWidth / 2);
  painter->setPen(QPen(border, borderWidth));
  painter->setBrush(isServer ? withAlpha(accent, 22) : card);
  if (isServer) {
    // Keep the card opaque under the tinted fill
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(card);
    painter->drawRoundedRect(inner, kRadius, kRadius);
    painter->restore();
  }
  painter->drawRoundedRect(inner, kRadius, kRadius);

  // Monitor, sized to the tile
  auto font = QApplication::font();
  const QFontMetricsF nameMetrics(font);
  const qreal textHeight = nameMetrics.height();
  const qreal available = inner.height() - textHeight - 22;
  const qreal screenHeight = std::clamp(available * 0.62, 14.0, 46.0);
  const qreal screenWidth = screenHeight * 1.45;
  const qreal blockHeight = screenHeight * 1.22 + 8 + textHeight;
  const qreal top = inner.top() + (inner.height() - blockHeight) / 2;
  const QRectF screen(inner.center().x() - screenWidth / 2, top, screenWidth, screenHeight);
  paintMonitor(painter, screen, isServer ? accent : muted, isServer ? withAlpha(accent, 40) : withAlpha(muted, 18));

  // Name
  font.setWeight(QFont::DemiBold);
  painter->setFont(font);
  painter->setPen(text);
  const QRectF nameRect(inner.left() + 8, screen.bottom() + screenHeight * 0.22 + 8, inner.width() - 16, textHeight);
  painter->drawText(
      nameRect, Qt::AlignHCenter | Qt::AlignTop, QFontMetricsF(font).elidedText(name, Qt::ElideMiddle, nameRect.width())
  );

  // Server badge
  if (isServer) {
    auto badgeFont = QApplication::font();
    badgeFont.setPointSizeF(badgeFont.pointSizeF() * 0.78);
    badgeFont.setWeight(QFont::DemiBold);
    const QFontMetricsF badgeMetrics(badgeFont);
    const auto label = tr("Server");
    const QSizeF badgeSize(badgeMetrics.horizontalAdvance(label) + 12, badgeMetrics.height() + 2);
    if (badgeSize.width() < inner.width() - 12) {
      const QRectF badge(inner.left() + 6, inner.top() + 6, badgeSize.width(), badgeSize.height());
      painter->setPen(Qt::NoPen);
      painter->setBrush(accent);
      painter->drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
      painter->setFont(badgeFont);
      painter->setPen(palette.color(QPalette::HighlightedText));
      painter->drawText(badge, Qt::AlignCenter, label);
    }
  }
}

QPixmap ComputerTileDelegate::tilePixmap(const QString &name, bool isServer, const QSize &size, const QPalette &palette)
{
  return makePixmap(size, [&](QPainter &painter, const QRectF &rect) {
    paintComputer(&painter, rect.adjusted(1, 1, -1, -1), palette, name, isServer, TileState::Selected);
  });
}

QPixmap ComputerTileDelegate::newComputerPixmap(const QSize &size, const QPalette &palette, bool enabled)
{
  return makePixmap(size, [&](QPainter &painter, const QRectF &rect) {
    const auto accent = enabled ? palette.color(QPalette::Highlight) : palette.color(QPalette::Mid);
    const auto inner = rect.adjusted(1, 1, -1, -1);
    painter.setPen(QPen(accent, 1.5, Qt::DashLine));
    painter.setBrush(withAlpha(accent, enabled ? 18 : 0));
    painter.drawRoundedRect(inner, kRadius, kRadius);

    const qreal captionHeight = QFontMetricsF(QApplication::font()).height();
    const QRectF screen(inner.center().x() - 17, inner.top() + (inner.height() - 40 - captionHeight) / 2, 34, 23);
    paintMonitor(&painter, screen, accent, Qt::transparent);
    painter.setPen(roundPen(accent, 2));
    const auto c = screen.center();
    painter.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
    painter.drawLine(QPointF(c.x(), c.y() - 5), QPointF(c.x(), c.y() + 5));

    drawCaption(
        painter, QRectF(inner.left() + 4, screen.bottom() + 12, inner.width() - 8, captionHeight + 2),
        tr("New computer"), enabled ? palette.color(QPalette::Text) : palette.color(QPalette::PlaceholderText)
    );
  });
}

QPixmap ComputerTileDelegate::removeComputerPixmap(const QSize &size, const QPalette &palette, bool active)
{
  return makePixmap(size, [&](QPainter &painter, const QRectF &rect) {
    const auto color = active ? kDanger : palette.color(QPalette::PlaceholderText);
    const auto inner = rect.adjusted(1, 1, -1, -1);
    painter.setPen(QPen(active ? kDanger : palette.color(QPalette::Mid), active ? 2 : 1.5, Qt::DashLine));
    painter.setBrush(active ? withAlpha(kDanger, 28) : Qt::transparent);
    painter.drawRoundedRect(inner, kRadius, kRadius);

    // Trash can
    const qreal captionHeight = QFontMetricsF(QApplication::font()).height();
    const qreal top = inner.top() + (inner.height() - 40 - captionHeight) / 2;
    const qreal cx = inner.center().x();
    painter.setPen(roundPen(color, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(cx - 12, top + 5), QPointF(cx + 12, top + 5));
    painter.drawLine(QPointF(cx - 4, top + 1), QPointF(cx + 4, top + 1));
    QPainterPath can;
    can.moveTo(cx - 9, top + 9);
    can.lineTo(cx - 7.5, top + 27);
    can.lineTo(cx + 7.5, top + 27);
    can.lineTo(cx + 9, top + 9);
    painter.drawPath(can);
    painter.drawLine(QPointF(cx - 3, top + 13), QPointF(cx - 3, top + 23));
    painter.drawLine(QPointF(cx + 3, top + 13), QPointF(cx + 3, top + 23));

    drawCaption(
        painter, QRectF(inner.left() + 4, top + 36, inner.width() - 8, captionHeight + 2), tr("Drop to remove"), color
    );
  });
}
