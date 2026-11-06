/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "NewComputerWidget.h"
#include "ComputerSetupModel.h"
#include "ComputerTileDelegate.h"

#include <QDrag>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>

namespace {
const QSize kTileSize(124, 84);
}

NewComputerWidget::NewComputerWidget(QWidget *parent) : QLabel(parent)
{
  setCursor(Qt::OpenHandCursor);
  setAlignment(Qt::AlignCenter);
  updateTile();
}

void NewComputerWidget::changeEvent(QEvent *event)
{
  QLabel::changeEvent(event);
  if (event->type() == QEvent::EnabledChange || event->type() == QEvent::PaletteChange)
    updateTile();
}

void NewComputerWidget::updateTile()
{
  setPixmap(ComputerTileDelegate::newComputerPixmap(kTileSize, palette(), isEnabled()));
}

void NewComputerWidget::mousePressEvent(QMouseEvent *)
{
  //: Used as the hostname. Translation may not contain spaces
  Computer newComputer(tr("Unnamed"));

  QByteArray itemData;
  QDataStream dataStream(&itemData, QIODevice::WriteOnly);
  dataStream << -1 << -1 << newComputer;

  auto *pMimeData = new QMimeData;
  pMimeData->setData(ComputerSetupModel::mimeType(), itemData);

  auto *pDrag = new QDrag(this);
  pDrag->setMimeData(pMimeData);
  pDrag->setPixmap(ComputerTileDelegate::tilePixmap(newComputer.name(), false, kTileSize, palette()));
  pDrag->setHotSpot(QPoint(kTileSize.width() / 2, kTileSize.height() / 2));
  pDrag->exec(Qt::CopyAction, Qt::CopyAction);
}
