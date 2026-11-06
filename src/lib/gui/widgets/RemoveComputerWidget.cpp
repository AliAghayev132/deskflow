/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RemoveComputerWidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "ComputerSetupModel.h"
#include "ComputerTileDelegate.h"

namespace {
const QSize kTileSize(124, 84);
}

RemoveComputerWidget::RemoveComputerWidget(QWidget *parent) : QLabel(parent)
{
  setAlignment(Qt::AlignCenter);
  updateTile(false);
}

void RemoveComputerWidget::updateTile(bool active)
{
  setPixmap(ComputerTileDelegate::removeComputerPixmap(kTileSize, palette(), active));
}

void RemoveComputerWidget::changeEvent(QEvent *event)
{
  QLabel::changeEvent(event);
  if (event->type() == QEvent::PaletteChange)
    updateTile(false);
}

void RemoveComputerWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
  updateTile(false);
  QLabel::dragLeaveEvent(event);
}

void RemoveComputerWidget::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType())) {
    event->setDropAction(Qt::MoveAction);
    event->accept();
    updateTile(true);
  } else
    event->ignore();
}

void RemoveComputerWidget::dropEvent(QDropEvent *event)
{
  updateTile(false);
  if (event->mimeData()->hasFormat(ComputerSetupModel::mimeType())) {
    event->acceptProposedAction();
    Q_EMIT computerRemoved();
  } else {
    event->ignore();
  }
}
