/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QRegularExpression>
#include <QSystemTrayIcon>
#include <QUrl>

#include "VersionChecker.h"
#include "config/ServerConfig.h"
#include "gui/core/CoreProcess.h"
#include "gui/core/NetworkMonitor.h"
#include "net/Fingerprint.h"

#ifdef Q_OS_MACOS
#include "gui/OSXHelpers.h"
#endif

class QAction;
class QMenu;
class QTimer;
class QLocalServer;

class DeskflowApplication;
class LogDock;
namespace deskflow::gui {
class ClipboardHistory;
class ServerDiscovery;
struct DiscoveredServer;
struct LinkPeer;
} // namespace deskflow::gui
class ConnectionsPanel;
class ProfileBar;
class StatusBar;

namespace Ui {
class MainWindow;
}

namespace deskflow::gui::ipc {
class DaemonIpcClient;
}

class MainWindow : public QMainWindow
{
  using ConnectionState = deskflow::core::ConnectionState;
  using CoreMode = Settings::CoreMode;
  using CoreProcess = deskflow::gui::CoreProcess;
  using NetworkMonitor = deskflow::gui::NetworkMonitor;
  using ProcessState = deskflow::core::ProcessState;

  Q_OBJECT

public:
  enum class LogLevel
  {
    Error,
    Info
  };

public:
  explicit MainWindow();
  ~MainWindow() override;

  [[nodiscard]] CoreMode coreMode() const
  {
    return m_coreProcess.mode();
  }
  void open();
  ServerConfig &serverConfig()
  {
    return m_serverConfig;
  }

  void hide();

protected:
  void changeEvent(QEvent *e) override;
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  /**
   * @brief updateText Update all text not in the UI
   */
  void updateText();
  void toggleLogVisible(bool visible);

  void settingsChanged(const QString &key = QString());
  void serverConfigSaving();
  void coreProcessError(CoreProcess::Error error);
  void coreConnectionStateChanged(ConnectionState state);
  void coreProcessStateChanged(ProcessState state);

  void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
  void serverConnectionConfigureClient(const QString &clientName);

  void clearSettings();
  void openAboutDialog();
  void openGetNewVersionUrl() const;
  void openSettings();
  void startCore();
  void stopCore();
  bool saveServerConfig();
  void resetCore();

  void showMyFingerprint();
  void updateSecurityIcon(bool visible);
  void updateNetworkInfo();

  void coreModeToggled(bool checked);
  void updateModeControls();
  void updateModeControlLabels();
  std::unique_ptr<Ui::MainWindow> ui;

  void createMenuBar();
  void setupTrayIcon();
  void applyConfig();
  void setTrayIcon();
  void handleUnrecognisedClient(const QString &clientName);
  void handleConnectionRefused(deskflow::core::ConnectionRefusal reason);
  void handlePeerFingerprint(const QString &fingerprint);
  void handleMissingKeyboardLayouts(const QString &layouts);
  void closeEvent(QCloseEvent *event) override;
  bool maybeHideToTray();
  void secureSocket(bool secureSocket);
  void connectSlots();
  void handleLogLine(const QString &line);
  void updateFingerprintButton();
  void updateComputerName();
  void saveSettings() const;
  void showConfigureServer(const QString &message);
  void showConfigureClient();
  void restoreWindow();
  void setupControls();
  void showFirstConnectedMessage();
  void updateStatus();
  void showAndActivate();
  void showHostNameEditor();
  void setHostName();
  void daemonIpcClientConnectionFailed();
  void toggleCanRunCore(bool enableButtons);
  void remoteHostChanged(const QString &newRemoteHost);
  void updateIpLabel(const QStringList &addresses);
  void updateTimeoutDelay(int newDelay);
  void setHelpFilePath();
  void showHelpViewer() const;

  /**
   * @brief Make another profile active: stops the core, loads the profile into the UI and restarts the core if it was
   * running
   */
  void switchProfile(const QString &name);

  /**
   * @brief Update the profile selector and the tray profile menu
   */
  void refreshProfiles();

  /**
   * @brief Fill the clipboard history menu, called each time it opens
   */
  void refreshClipboardMenu();

  /**
   * @brief Draw the mode card icons in the colors of the current theme
   */
  void updateModeIcons();

  /**
   * @brief Answer discovery queries while this computer runs as a discoverable server
   */
  void updateDiscovery();

  /**
   * @brief Look for servers on the network
   * @param automatic true when started by the app rather than the Find button, the result is applied silently
   */
  void findServers(bool automatic = false);
  void serversFound(const QList<deskflow::gui::DiscoveredServer> &servers);

  /**
   * @brief Remember the MAC of the server the user picked so it can be woken up later
   */
  void rememberServer(const deskflow::gui::DiscoveredServer &server);
  void wakeServer();

  /**
   * @brief Remember the MAC of a connected client so the server can wake it up later
   */
  void rememberClient(const deskflow::gui::LinkPeer &peer);

  /**
   * @brief Fill the wake up menu with the computers we know a MAC for, called each time it opens
   */
  void refreshWakeMenu();
  void wakeComputer(const QString &name, const QString &mac);

  /**
   * @brief Say hello to the server while connected, for the round trip time and Wake-on-LAN
   */
  void updateLink();

  /**
   * @brief Runs every few seconds: switches profile when the network changes and reconnects a client after
   * sleep or a network change
   */
  void watchSystem();

  /**
   * @brief Show the setup wizard on first run
   */
  void maybeShowOnboarding();
  void showOnboarding();

  /**
   * @brief Check the mode buttons to match the stored mode, both unchecked when none was chosen
   */
  void loadModeFromSettings();

  bool canRunCore() const;

  /**
   * @brief trustedFingerprintDatabase get the FingerprintDatabase for the trusted clients or trusted servers.
   * @return The path to the trusted fingerprint file
   */
  QString trustedFingerprintDatabase() const;

  /**
   * @brief generateCertificate Generate a new certificate
   * @return true when successful
   */
  bool generateCertificate();

  Fingerprint m_fingerprint;

  void serverClientsChanged(const QStringList &clients);

  inline static const auto m_guiSocketName = QStringLiteral("deskflow-gui");
  static constexpr int kWatchInterval = 5000; ///< ms between network and sleep checks
  static constexpr int kSleepGap = 30000;     ///< ms a check may be late before we assume the computer slept
  inline static const auto m_nameRegEx = QRegularExpression(QStringLiteral("^[\\w\\-_\\.]{0,255}$"));

  VersionChecker m_versionChecker;
  bool m_secureSocket = false;
  bool m_saveOnExit = true;
  bool m_clientErrorVisible = false;
  ServerConfig m_serverConfig;
  deskflow::gui::CoreProcess m_coreProcess;
  QSet<QString> m_ignoredClients;
  bool m_newClientPromptShowing = false;
  bool m_serverConfigDialogVisible = false;
  QSize m_expandedSize = QSize();
  QStringList m_checkedClients;
  QStringList m_checkedServers;
  QSystemTrayIcon *m_trayIcon = nullptr;
  QLocalServer *m_guiDupeChecker = nullptr;
  deskflow::gui::ipc::DaemonIpcClient *m_daemonIpcClient = nullptr;

  LogDock *m_logDock;
  StatusBar *m_statusBar = nullptr;
  ProfileBar *m_profileBar = nullptr;
  deskflow::gui::ServerDiscovery *m_discovery = nullptr;
  bool m_automaticDiscovery = false;
  ConnectionsPanel *m_connectionsPanel = nullptr;
  qint64 m_lastWatch = 0;
  QStringList m_lastNetworks;
  bool m_watchStarted = false;

  // Window Menu
  QMenu *m_menuFile = nullptr;
  QMenu *m_menuEdit = nullptr;
  QMenu *m_menuView = nullptr;
  QMenu *m_menuHelp = nullptr;
  QMenu *m_menuProfiles = nullptr;

  // Window Actions
  QAction *m_actionAbout = nullptr;
  QAction *m_actionMinimize = nullptr;
  QAction *m_actionQuit = nullptr;
  QAction *m_actionTrayQuit = nullptr;
  QAction *m_actionRestore = nullptr;
  QAction *m_actionSettings = nullptr;
  QAction *m_actionStartCore = nullptr;
  QAction *m_actionRestartCore = nullptr;
  QAction *m_actionStopCore = nullptr;
  QAction *m_actionShowHelp = nullptr;

  // Network monitoring
  NetworkMonitor *m_networkMonitor = nullptr;
  QTimer *m_watchTimer = nullptr;
  QAction *m_actionSetupWizard = nullptr;
  deskflow::gui::ClipboardHistory *m_clipboardHistory = nullptr;
  QMenu *m_menuClipboard = nullptr;
  QMenu *m_menuWake = nullptr;
  QString m_currentIpAddress;

  // Server IP strategy optimization
  QStringList m_serverStartIPs;
  QString m_serverStartSuggestedIP;
  QUrl m_helpPath;
};
