/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ComputerSetupView.h"

#include "ComputerSetupModel.h"
#include "ComputerTileDelegate.h"
#include "dialogs/ComputerSettingsDialog.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>

ComputerSetupView::ComputerSetupView(QWidget *parent) : QTableView(parent), m_delegate{new ComputerTileDelegate(this)}
{
  setObjectName(QStringLiteral("computerSetupView"));
  setItemDelegate(m_delegate);
  setShowGrid(false);
  setMouseTracking(true);
  setFrameShape(QFrame::NoFrame);

  setDropIndicatorShown(true);
  setDragDropMode(DragDrop);
  setSelectionMode(SingleSelection);

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  setIconSize(QSize(96, 96));
  horizontalHeader()->hide();
  verticalHeader()->hide();
}

void ComputerSetupView::setModel(QAbstractItemModel *model)
{
  QTableView::setModel(model);
  setTableSize();
}

ComputerSetupModel *ComputerSetupView::model() const
{
  return qobject_cast<ComputerSetupModel *>(QTableView::model());
}

void ComputerSetupView::showComputerConfig(int col, int row)
{
  ComputerSettingsDialog dlg(this, &model()->computer(col, row), &model()->m_computers);
  dlg.exec();
  Q_EMIT model()->computersChanged();
}

void ComputerSetupView::setTableSize()
{
  for (int i = 0; i < model()->columnCount(); i++)
    setColumnWidth(i, width() / model()->columnCount());

  for (int i = 0; i < model()->rowCount(); i++)
    setRowHeight(i, height() / model()->rowCount());
}

void ComputerSetupView::resizeEvent(QResizeEvent *event)
{
  setTableSize();
  event->ignore();
}

void ComputerSetupView::mouseDoubleClickEvent(QMouseEvent *event)
{
  if (event->buttons() & Qt::LeftButton) {
    int col = columnAt(event->pos().x());
    int row = rowAt(event->pos().y());

    if (!model()->computer(col, row).isNull()) {
      showComputerConfig(col, row);
    }
  } else
    event->ignore();
}

void ComputerSetupView::dragEnterEvent(QDragEnterEvent *event)
{
  // we accept anything that enters us by a drag as long as the
  // mime type is okay. anything else is dealt with in dragMoveEvent()
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType()))
    event->accept();
  else
    event->ignore();
}

void ComputerSetupView::setDropTarget(const QModelIndex &index)
{
  if (m_delegate->dropTarget() == index)
    return;
  m_delegate->setDropTarget(index);
  viewport()->update();
}

void ComputerSetupView::dragLeaveEvent(QDragLeaveEvent *event)
{
  setDropTarget({});
  QTableView::dragLeaveEvent(event);
}

void ComputerSetupView::dropEvent(QDropEvent *event)
{
  setDropTarget({});
  QTableView::dropEvent(event);
}

void ComputerSetupView::dragMoveEvent(QDragMoveEvent *event)
{
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType())) {
    // where does the event come from? myself or someone else?
    if (event->source() == this) {
      // myself is ok, but then it must be a move action, never a copy
      event->setDropAction(Qt::MoveAction);
      event->accept();
    } else {
      const auto &point = event->position().toPoint();
      int col = columnAt(point.x());
      int row = rowAt(point.y());

      // a drop from outside is not allowed if there's a computer already there.
      if (!model()->computer(col, row).isNull())
        event->ignore();
      else {
        event->acceptProposedAction();
      }
    }
  } else
    event->ignore();

  // Highlight the cell the computer would land in
  setDropTarget(event->isAccepted() ? indexAt(event->position().toPoint()) : QModelIndex());
}

// this is reimplemented from QAbstractItemView::startDrag()
void ComputerSetupView::startDrag(Qt::DropActions)
{
  QModelIndexList indexes = selectedIndexes();

  if (indexes.count() != 1)
    return;

  QMimeData *pData = model()->mimeData(indexes);
  if (pData == nullptr)
    return;

  const auto &computer = model()->computer(indexes[0]);
  const QSize tileSize(std::min(columnWidth(indexes[0].column()), 160), std::min(rowHeight(indexes[0].row()), 120));
  auto *pDrag = new QDrag(this);
  pDrag->setPixmap(ComputerTileDelegate::tilePixmap(computer.name(), computer.isServer(), tileSize, palette()));
  pDrag->setMimeData(pData);
  pDrag->setHotSpot(QPoint(tileSize.width() / 2, tileSize.height() / 2));

  if (pDrag->exec(Qt::MoveAction, Qt::MoveAction) == Qt::MoveAction) {
    selectionModel()->clear();

    // make sure to only delete the drag source if computerss weren't swapped
    // see ComputerSetupModel::dropMimeData
    if (!model()->computer(indexes[0]).swapped())
      model()->computer(indexes[0]) = Computer();
    else
      model()->computer(indexes[0]).setSwapped(false);

    Q_EMIT model()->computersChanged();
  }
}

void ComputerSetupView::initViewItemOption(QStyleOptionViewItem *option) const
{
  // HACK make a basic widget and init from it
  auto w = new QWidget();
  option->initFrom(w);
  w->deleteLater();
  delete w;

  option->decorationSize = QSize(96, 96);
  // The tile delegate draws the selection itself
  option->showDecorationSelected = false;
  option->decorationPosition = QStyleOptionViewItem::Top;
  option->decorationAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
  option->displayAlignment = Qt::AlignTop | Qt::AlignHCenter;
  option->textElideMode = Qt::ElideMiddle;
}
