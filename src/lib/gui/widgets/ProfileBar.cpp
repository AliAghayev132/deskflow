/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProfileBar.h"

#include "common/Profiles.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolButton>

ProfileBar::ProfileBar(QWidget *parent)
    : QWidget(parent),
      m_label{new QLabel(this)},
      m_combo{new QComboBox(this)},
      m_btnMenu{new QToolButton(this)},
      m_menu{new QMenu(this)},
      m_actionNew{new QAction(this)},
      m_actionDuplicate{new QAction(this)},
      m_actionRename{new QAction(this)},
      m_actionDelete{new QAction(this)}
{
  setObjectName(QStringLiteral("profileBar"));

  m_combo->setObjectName(QStringLiteral("cbProfile"));
  m_combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  m_combo->setMinimumContentsLength(12);
  m_label->setBuddy(m_combo);

  m_actionNew->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
  m_actionDuplicate->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
  m_actionRename->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename")));
  m_actionDelete->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
  m_menu->addActions({m_actionNew, m_actionDuplicate, m_actionRename});
  m_menu->addSeparator();
  m_menu->addAction(m_actionDelete);

  m_btnMenu->setObjectName(QStringLiteral("btnProfileMenu"));
  m_btnMenu->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_btnMenu->setPopupMode(QToolButton::InstantPopup);
  m_btnMenu->setMenu(m_menu);
  m_btnMenu->setAutoRaise(true);

  auto layout = new QHBoxLayout(this);
  layout->setContentsMargins(3, 0, 3, 0);
  layout->addWidget(m_label);
  layout->addWidget(m_combo);
  layout->addWidget(m_btnMenu);
  layout->addStretch();

  connect(m_combo, &QComboBox::activated, this, &ProfileBar::comboActivated);
  connect(m_actionNew, &QAction::triggered, this, &ProfileBar::newProfile);
  connect(m_actionDuplicate, &QAction::triggered, this, &ProfileBar::duplicateProfile);
  connect(m_actionRename, &QAction::triggered, this, &ProfileBar::renameProfile);
  connect(m_actionDelete, &QAction::triggered, this, &ProfileBar::deleteProfile);

  updateText();
  refresh();
}

void ProfileBar::refresh()
{
  const QSignalBlocker blocker(m_combo);
  const auto profiles = Profiles::list();
  const auto active = Profiles::active();

  m_combo->clear();
  m_combo->addItems(profiles);
  m_combo->setCurrentIndex(static_cast<int>(std::max<qsizetype>(0, profiles.indexOf(active))));

  m_actionDelete->setEnabled(profiles.size() > 1);
  m_actionRename->setEnabled(!active.isEmpty());
  m_actionDuplicate->setEnabled(!active.isEmpty());
}

void ProfileBar::changeEvent(QEvent *e)
{
  QWidget::changeEvent(e);
  if (e->type() == QEvent::LanguageChange)
    updateText();
}

void ProfileBar::updateText()
{
  m_label->setText(tr("&Profile:"));
  m_combo->setToolTip(tr("Each profile keeps its own mode, server address and computer layout"));
  m_btnMenu->setToolTip(tr("Manage profiles"));
  m_actionNew->setText(tr("&New profile..."));
  m_actionDuplicate->setText(tr("D&uplicate current profile..."));
  m_actionRename->setText(tr("&Rename..."));
  m_actionDelete->setText(tr("&Delete..."));
}

void ProfileBar::comboActivated(int index)
{
  const auto name = m_combo->itemText(index);
  if (name.isEmpty() || name == Profiles::active())
    return;

  // Switching refreshes this combo box, so leave its signal handler first
  QMetaObject::invokeMethod(this, [this, name] { Q_EMIT switchRequested(name); }, Qt::QueuedConnection);
}

QString ProfileBar::uniqueName(const QString &base)
{
  if (Profiles::isValidName(base) && !Profiles::exists(base))
    return base;
  for (int i = 2; i < 1000; ++i) {
    const auto candidate = QStringLiteral("%1 %2").arg(base).arg(i);
    if (Profiles::isValidName(candidate) && !Profiles::exists(candidate))
      return candidate;
  }
  return {};
}

QString ProfileBar::askName(const QString &title, const QString &initial, const QString &current)
{
  auto name = initial;
  while (true) {
    bool ok = false;
    name = QInputDialog::getText(this, title, tr("Profile name:"), QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || name.isEmpty())
      return {};

    const bool sameProfile = !current.isEmpty() && name.compare(current, Qt::CaseInsensitive) == 0;

    if (!Profiles::isValidName(name)) {
      QMessageBox::warning(
          this, title,
          tr("Use up to %1 letters, digits, spaces, '-', '_' or '.'. "
             "The name must start and end with a letter or digit.")
              .arg(Profiles::MaxNameLength)
      );
    } else if (!sameProfile && Profiles::exists(name)) {
      QMessageBox::warning(this, title, tr("A profile named \"%1\" already exists.").arg(name));
    } else {
      return name;
    }
  }
}

void ProfileBar::newProfile()
{
  const auto name = askName(tr("New profile"), uniqueName(tr("New profile")));
  if (name.isEmpty())
    return;

  if (!Profiles::create(name, false)) {
    QMessageBox::warning(this, tr("New profile"), tr("Could not create the profile \"%1\".").arg(name));
    return;
  }

  Q_EMIT profilesChanged();
  Q_EMIT switchRequested(name);
}

void ProfileBar::duplicateProfile()
{
  const auto active = Profiles::active();
  //: %1 is the name of the profile being copied
  const auto name = askName(tr("Duplicate profile"), uniqueName(tr("%1 copy").arg(active)));
  if (name.isEmpty())
    return;

  // The live settings are the active profile, copy them as they are now
  if (!Profiles::create(name, true)) {
    QMessageBox::warning(this, tr("Duplicate profile"), tr("Could not create the profile \"%1\".").arg(name));
    return;
  }

  Q_EMIT profilesChanged();
  Q_EMIT switchRequested(name);
}

void ProfileBar::renameProfile()
{
  const auto active = Profiles::active();
  const auto name = askName(tr("Rename profile"), active, active);
  if (name.isEmpty() || name == active)
    return;

  if (!Profiles::rename(active, name)) {
    QMessageBox::warning(this, tr("Rename profile"), tr("Could not rename the profile to \"%1\".").arg(name));
    return;
  }

  refresh();
  Q_EMIT profilesChanged();
}

void ProfileBar::deleteProfile()
{
  const auto profiles = Profiles::list();
  if (profiles.size() <= 1)
    return;

  const auto target = m_combo->currentText();
  const auto answer = QMessageBox::question(
      this, tr("Delete profile"),
      tr("Delete the profile \"%1\"? Its server address and computer layout will be lost.").arg(target),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No
  );
  if (answer != QMessageBox::Yes)
    return;

  // The active profile can't be deleted, move to another one first
  if (target == Profiles::active()) {
    const auto next = profiles.at(profiles.indexOf(target) == 0 ? 1 : 0);
    Q_EMIT switchRequested(next);
    if (Profiles::active() == target)
      return; // switch failed, nothing to delete
  }

  if (!Profiles::remove(target)) {
    QMessageBox::warning(this, tr("Delete profile"), tr("Could not delete the profile \"%1\".").arg(target));
  }

  refresh();
  Q_EMIT profilesChanged();
}
