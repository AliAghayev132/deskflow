/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QLabel>

class QWidget;
class QDragEnterEvent;
class QDropEvent;

class RemoveComputerWidget : public QLabel
{
  Q_OBJECT

public:
  explicit RemoveComputerWidget(QWidget *parent);

  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragLeaveEvent(QDragLeaveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateTile(bool active);

Q_SIGNALS:
  void computerRemoved();
};
