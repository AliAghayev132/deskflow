/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Settings.h"
#include "gui/core/ServerDiscovery.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QStackedWidget;

/**
 * @brief First run setup: choose server or client and get connected.
 *
 * Page 1 asks how this computer is used. A server is told what to do on the
 * other computers; a client is shown the servers found on the network or can
 * type an address. Nothing is saved until the user finishes.
 */
class OnboardingDialog : public QDialog
{
  Q_OBJECT

public:
  explicit OnboardingDialog(QWidget *parent = nullptr);

  [[nodiscard]] Settings::CoreMode mode() const;

  /**
   * @brief Address of the server to connect to, client mode only
   */
  [[nodiscard]] QString serverHost() const;

  /**
   * @brief The chosen server's MAC address if discovery reported one, for Wake-on-LAN
   */
  [[nodiscard]] QString serverMac() const;

  /**
   * @brief Whether to start sharing or connecting right after the dialog closes
   */
  [[nodiscard]] bool startNow() const;

private:
  void showModePage();
  void showDetailsPage();
  void updateButtons();
  void scan();
  void serversFound(const QList<deskflow::gui::DiscoveredServer> &servers);
  void selectServer();

  QStackedWidget *m_pages = nullptr;
  QRadioButton *m_rbServer = nullptr;
  QRadioButton *m_rbClient = nullptr;

  QWidget *m_serverPage = nullptr;
  QCheckBox *m_cbStartServer = nullptr;

  QWidget *m_clientPage = nullptr;
  QListWidget *m_serverList = nullptr;
  QLabel *m_scanStatus = nullptr;
  QPushButton *m_btnScan = nullptr;
  QLineEdit *m_host = nullptr;
  QCheckBox *m_cbConnect = nullptr;

  QPushButton *m_btnBack = nullptr;
  QPushButton *m_btnNext = nullptr;

  deskflow::gui::ServerDiscovery *m_discovery = nullptr;
  QList<deskflow::gui::DiscoveredServer> m_servers;
};
