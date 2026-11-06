/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class QAction;
class QComboBox;
class QLabel;
class QMenu;
class QToolButton;

/**
 * @brief Profile selector shown at the top of the main window.
 *
 * Lists the saved profiles and lets the user create, duplicate, rename and
 * delete them. Switching is requested through switchRequested() because the
 * main window has to stop and restart the core around the switch.
 */
class ProfileBar : public QWidget
{
  Q_OBJECT

public:
  explicit ProfileBar(QWidget *parent = nullptr);

  /**
   * @brief Reload the list of profiles and select the active one
   */
  void refresh();

Q_SIGNALS:
  /**
   * @brief The user picked or created a profile that should become active
   */
  void switchRequested(const QString &name);

  /**
   * @brief Profiles were added, renamed or deleted
   */
  void profilesChanged();

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  void comboActivated(int index);
  void newProfile();
  void duplicateProfile();
  void renameProfile();
  void deleteProfile();

  /**
   * @brief Ask the user for a profile name until a valid, unused one is given
   * @param title dialog title
   * @param initial the text shown when the dialog opens
   * @param current name being renamed, it may be reused with a different case
   * @return the name, or an empty string when the user cancelled
   */
  QString askName(const QString &title, const QString &initial, const QString &current = QString());

  /**
   * @brief Propose an unused name based on base, e.g. "Home 2"
   */
  static QString uniqueName(const QString &base);

  QLabel *m_label = nullptr;
  QComboBox *m_combo = nullptr;
  QToolButton *m_btnMenu = nullptr;
  QMenu *m_menu = nullptr;
  QAction *m_actionNew = nullptr;
  QAction *m_actionDuplicate = nullptr;
  QAction *m_actionRename = nullptr;
  QAction *m_actionDelete = nullptr;
};
