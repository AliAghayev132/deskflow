/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "Diagnostic.h"
#include "StyleUtils.h"
#include "Theme.h"

#include "dialogs/AboutDialog.h"
#include "dialogs/ClientConfigDialog.h"
#include "dialogs/FingerprintDialog.h"
#include "dialogs/HelpDialog.h"
#include "dialogs/OnboardingDialog.h"
#include "dialogs/ServerConfigDialog.h"
#include "dialogs/SettingsDialog.h"

#include "common/PlatformInfo.h"
#include "common/Profiles.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "gui/Messages.h"
#include "gui/TlsUtility.h"
#include "gui/core/ClipboardHistory.h"
#include "gui/core/CoreProcess.h"
#include "gui/core/FileTransfer.h"
#include "gui/core/PairingCode.h"
#include "gui/core/ScreenLock.h"
#include "gui/core/ServerDiscovery.h"
#include "gui/core/Updater.h"
#include "gui/core/WakeOnLan.h"
#include "gui/ipc/DaemonIpcClient.h"
#include "gui/widgets/LogDock.h"
#include "net/FingerprintDatabase.h"
#include "widgets/ConnectionsPanel.h"
#include "widgets/ProfileBar.h"
#include "widgets/ScreenDimmer.h"
#include "widgets/StatusBar.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardPaths>

#include <algorithm>
#include <memory>
#include <utility>

#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

using namespace deskflow::gui;

MainWindow::MainWindow()
    : ui{std::make_unique<Ui::MainWindow>()},
      m_coreProcess(m_serverConfig),
      m_trayIcon{new QSystemTrayIcon(this)},
      m_guiDupeChecker{new QLocalServer(this)},
      m_daemonIpcClient{new ipc::DaemonIpcClient(this)},
      m_logDock{new LogDock(this)},
      m_statusBar{new StatusBar(this)},
      m_menuFile{new QMenu(this)},
      m_menuEdit{new QMenu(this)},
      m_menuView{new QMenu(this)},
      m_menuHelp{new QMenu(this)},
      m_menuProfiles{new QMenu(this)},
      m_actionAbout{new QAction(this)},
      m_actionMinimize{new QAction(this)},
      m_actionQuit{new QAction(this)},
      m_actionTrayQuit{new QAction(this)},
      m_actionRestore{new QAction(this)},
      m_actionSettings{new QAction(this)},
      m_actionStartCore{new QAction(this)},
      m_actionRestartCore{new QAction(this)},
      m_actionStopCore{new QAction(this)},
      m_actionShowHelp{new QAction(this)},
      m_networkMonitor{new NetworkMonitor(this)},
      m_watchTimer{new QTimer(this)},
      m_actionSetupWizard{new QAction(this)},
      m_clipboardHistory{new ClipboardHistory(QGuiApplication::clipboard(), this)},
      m_menuClipboard{new QMenu(this)},
      m_menuWake{new QMenu(this)},
      m_fileReceiver{new FileReceiver(this)},
      m_actionSendFiles{new QAction(this)},
      m_updater{new Updater(QUrl(Settings::value(Settings::Gui::UpdateCheckUrl).toString()), this)},
      m_actionCheckUpdates{new QAction(this)},
      m_actionTestBuilds{new QAction(this)},
      m_updateTimer{new QTimer(this)},
      m_dimmer{new ScreenDimmer(this)},
      m_screenLock{new ScreenLock(this)},
      m_actionDimInactive{new QAction(this)},
      m_actionLockTogether{new QAction(this)},
      m_actionPasteFiles{new QAction(this)}
{
  ui->setupUi(this);

  setWindowIcon(QIcon::fromTheme(kRevFqdnName));

  addDockWidget(Qt::BottomDockWidgetArea, m_logDock);

  // Setup Actions
  m_actionAbout->setMenuRole(QAction::AboutRole);
  m_actionAbout->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::HelpAbout));

  m_actionMinimize->setIcon(QIcon::fromTheme(QStringLiteral("window-minimize-pip")));
  m_actionRestore->setIcon(QIcon::fromTheme(QStringLiteral("window-restore-pip")));

  if (!deskflow::platform::isWindows()) {
    m_actionQuit->setShortcut(QKeySequence::Quit);
    m_actionTrayQuit->setShortcut(QKeySequence::Quit);
  }

  m_actionQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionQuit->setMenuRole(QAction::QuitRole);

  m_actionTrayQuit->setIcon(QIcon::fromTheme("application-exit"));
  m_actionTrayQuit->setMenuRole(QAction::NoRole);

  m_actionSettings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  m_actionSettings->setMenuRole(QAction::PreferencesRole);
  m_actionSettings->setShortcut(QKeySequence::Preferences);

  m_actionStartCore->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
  m_actionStartCore->setMenuRole(QAction::NoRole);

  m_actionRestartCore->setVisible(false);
  m_actionRestartCore->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
  m_actionRestartCore->setMenuRole(QAction::NoRole);

  m_actionStopCore->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop));
  m_actionStopCore->setMenuRole(QAction::NoRole);

  m_actionShowHelp->setIcon(QIcon::fromTheme(QStringLiteral("question")));
  m_actionShowHelp->setMenuRole(QAction::NoRole);
  m_actionShowHelp->setShortcut(QKeySequence::HelpContents);

  // Setup the Instance Checking
  // In case of a previous crash remove first
  QLocalServer::removeServer(m_guiSocketName);
  m_guiDupeChecker->listen(m_guiSocketName);

  Profiles::ensureInitialized();

  m_clipboardHistory->setEnabled(Settings::value(Settings::Gui::ClipboardHistory).toBool());
  m_menuClipboard->setToolTipsVisible(true);
  connect(m_menuClipboard, &QMenu::aboutToShow, this, &MainWindow::refreshClipboardMenu);
  m_menuWake->setToolTipsVisible(true);

  m_actionSendFiles->setIcon(QIcon::fromTheme(QStringLiteral("document-send")));
  m_actionSendFiles->setMenuRole(QAction::NoRole);
  m_actionSendFiles->setEnabled(false);
  connect(m_actionSendFiles, &QAction::triggered, this, [this] { sendFiles(); });
  connect(m_fileReceiver, &FileReceiver::offered, this, &MainWindow::fileOffered);
  connect(m_fileReceiver, &FileReceiver::finished, this, &MainWindow::filesReceived);
  connect(m_fileReceiver, &FileReceiver::progress, this, [this](int id, qint64 received, qint64 total) {
    //: %1 is a computer name, %2 and %3 are sizes like "3.1 MB"
    m_statusBar->showMessage(
        tr("Receiving files from %1: %2 of %3")
            .arg(m_offerSenders.value(id), filetransfer::sizeText(received), filetransfer::sizeText(total))
    );
  });
  connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this] {
    if (m_notificationAction)
      m_notificationAction();
    else if (!m_notifiedFolder.isEmpty())
      QDesktopServices::openUrl(QUrl::fromLocalFile(m_notifiedFolder));
  });

  m_actionDimInactive->setCheckable(true);
  m_actionDimInactive->setChecked(Settings::value(Settings::Gui::DimInactive).toBool());
  connect(m_actionDimInactive, &QAction::toggled, this, [this](bool on) {
    Settings::setValue(Settings::Gui::DimInactive, on);
    if (!on)
      m_dimmer->setDimmed(false);
  });
  m_actionLockTogether->setCheckable(true);
  m_actionLockTogether->setChecked(Settings::value(Settings::Gui::LockTogether).toBool());
  connect(m_actionLockTogether, &QAction::toggled, this, [](bool on) {
    Settings::setValue(Settings::Gui::LockTogether, on);
  });
  m_actionPasteFiles->setIcon(QIcon::fromTheme(QStringLiteral("edit-paste")));
  connect(m_actionPasteFiles, &QAction::triggered, this, &MainWindow::pasteRemoteFiles);
  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::clipboardChanged);
  connect(m_screenLock, &ScreenLock::locked, this, &MainWindow::screenLocked);
  m_fileReceiver->setRequestHandler([this](const PeerRequest &request) { return handlePeerRequest(request); });
  setAcceptDrops(true);

  m_actionCheckUpdates->setMenuRole(QAction::ApplicationSpecificRole);
  m_actionCheckUpdates->setIcon(QIcon::fromTheme(QStringLiteral("software-updates-release")));
  connect(m_actionCheckUpdates, &QAction::triggered, this, [this] {
    if (m_update)
      showUpdate();
    else
      checkForUpdates(true);
  });
  m_actionTestBuilds->setCheckable(true);
  m_actionTestBuilds->setChecked(Settings::value(Settings::Gui::UpdateTestBuilds).toBool());
  connect(m_actionTestBuilds, &QAction::toggled, this, [this](bool on) {
    Settings::setValue(Settings::Gui::UpdateTestBuilds, on);
    m_update.reset();
    checkForUpdates(false);
  });
  connect(m_updater, &Updater::updateAvailable, this, &MainWindow::updateAvailable);
  connect(m_updater, &Updater::upToDate, this, [this] {
    m_update.reset();
    if (!std::exchange(m_manualUpdateCheck, false))
      return;
    m_statusBar->clearMessage();
    //: %1 is the app name, %2 its version
    QMessageBox::information(
        this, tr("No updates"), tr("You have the latest version of %1 (%2).").arg(kAppName, kDisplayVersion)
    );
  });
  connect(m_updater, &Updater::checkFailed, this, [this](const QString &error) {
    if (!std::exchange(m_manualUpdateCheck, false))
      return;
    m_statusBar->clearMessage();
    //: %1 is the reason
    QMessageBox::warning(this, tr("No updates"), tr("Could not check for updates: %1").arg(error));
  });
  connect(m_updater, &Updater::downloadProgress, this, [this](qint64 received, qint64 total) {
    if (!m_updateProgress)
      return;
    // In KiB so large files fit in an int
    m_updateProgress->setMaximum(static_cast<int>(std::max<qint64>(total / 1024, 1)));
    m_updateProgress->setValue(static_cast<int>(std::min(received, total) / 1024));
  });
  connect(m_updater, &Updater::downloaded, this, &MainWindow::installUpdate);
  connect(m_updater, &Updater::downloadFailed, this, [this](const QString &error) {
    if (m_updateProgress)
      m_updateProgress->close();
    //: %1 is the reason
    QMessageBox::warning(this, tr("Update"), tr("The update could not be downloaded: %1").arg(error));
  });
  // Look again now and then, the app often runs for days
  m_updateTimer->setInterval(6 * 60 * 60 * 1000);
  connect(m_updateTimer, &QTimer::timeout, this, [this] { checkForUpdates(false); });
  connect(m_menuWake, &QMenu::aboutToShow, this, &MainWindow::refreshWakeMenu);

  createMenuBar();
  setupControls();
  updateText();
  connectSlots();
  setupTrayIcon();
  // macOS builds the tray menu natively, so it needs items before it is first opened.
  // Only now, the menus look at widgets that setupControls creates.
  refreshClipboardMenu();
  refreshWakeMenu();
  m_screenLock->watch(static_cast<quintptr>(winId()));
  updateComputerName();
  setHelpFilePath();

  qDebug().noquote() << "active settings path:" << Settings::settingsPath();

  // Force generation of a certificate on the host
  if (TlsUtility::isEnabled()) {
    if (Settings::value(Settings::Security::KeySize).toInt() < 2048) {
      QMessageBox::information(
          this, kAppName,
          tr("Your current TLS key is smaller than the minimum allowed size, A new key 2048-bit key will be generated.")
      );
      Settings::setValue(Settings::Security::KeySize, 2048);
    }
    if (!TlsUtility::isCertValid()) {
      generateCertificate();
    } else {
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    }
  }

  applyConfig();
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
  restoreWindow();

#ifdef Q_OS_MACOS
  // Route native quits (Cmd+Q / Apple menu Quit / Dock "Quit") to the usual close-to-tray decision instead.
  installQuitHandler([this] { return !maybeHideToTray(); });
#endif
}
MainWindow::~MainWindow()
{
  // Closing the window while it is destroyed hides the log dock, which must no longer
  // reach this half destroyed window
  if (m_logDock)
    disconnect(m_logDock->toggleViewAction(), nullptr, this, nullptr);
  m_menuClipboard->disconnect(this);
  m_menuWake->disconnect(this);

  // Stop network monitoring
  if (m_networkMonitor) {
    m_networkMonitor->stopMonitoring();
  }

  m_guiDupeChecker->close();
  m_coreProcess.cleanup();
}

void MainWindow::restoreWindow()
{
  auto windowGeometry = Settings::value(Settings::Gui::WindowGeometry).toRect();
  const auto totalGeometry = QGuiApplication::primaryScreen()->availableGeometry();
  if (!windowGeometry.isValid()) {
    adjustSize();
    windowGeometry = geometry();
  } else {
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setGeometry(windowGeometry);
    // The geometry may have been saved by a version with fewer controls, make sure everything fits
    if (const auto minHeight = minimumSizeHint().height(); height() < minHeight)
      resize(width(), minHeight);
  }
  m_expandedSize = geometry().size();

  if (!totalGeometry.contains(windowGeometry)) {
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    move(screenGeometry.center() - rect().center());
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool())
    fitToContents();
}

void MainWindow::fitToContents()
{
  m_fitPending = false;
  // With the log docked and open the window is resizable and Qt keeps it large enough
  if (Settings::value(Settings::Gui::LogExpanded).toBool() && !m_logDock->isFloating())
    return;

  setMinimumSize(0, 0);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  const auto minimum = minimumSizeHint();
  const auto targetWidth = std::max(width(), minimum.width());
  auto targetHeight = hasHeightForWidth() ? heightForWidth(targetWidth) : -1;
  if (targetHeight <= 0)
    targetHeight = sizeHint().height();
  const QSize target(targetWidth, std::max(targetHeight, minimum.height()));
  if (target != size())
    resize(target);
  setFixedSize(target);
}

void MainWindow::setupControls()
{
  secureSocket(false);

  ui->btnConfigureServer->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  ui->btnConfigureClient->setIcon(QIcon::fromTheme(QStringLiteral("configure")));

  if (Settings::value(Settings::Core::LastVersion).toString() != kVersion) {
    Settings::setValue(Settings::Core::LastVersion, kVersion);
  }

  if (!Settings::value(Settings::Gui::LogExpanded).toBool()) {
    m_logDock->hide();
  }

  ui->serverOptions->setVisible(false);
  ui->clientOptions->setVisible(false);

  const auto coreMode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  ui->rbModeClient->setChecked(coreMode == Settings::CoreMode::Client);
  ui->rbModeServer->setChecked(coreMode == Settings::CoreMode::Server);

  ui->lineEditName->setValidator(new QRegularExpressionValidator(m_nameRegEx, this));
  ui->lineEditName->setVisible(false);
  ui->lineEditName->installEventFilter(this);
  // Notices when panels show up or hide, see fitToContents
  centralWidget()->installEventFilter(this);

  if (deskflow::platform::isMac()) {
    ui->rbModeServer->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->rbModeClient->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->btnSaveServerConfig->setFixedWidth(ui->btnSaveServerConfig->height());
  } else {
    ui->btnSaveServerConfig->setIconSize(QSize(22, 22));
  }
  setStatusBar(m_statusBar);

  m_profileBar = new ProfileBar(this);
  ui->profileContainer->layout()->addWidget(m_profileBar);

  m_discovery = new ServerDiscovery(this);

  m_connectionsPanel = new ConnectionsPanel(this);
  if (auto layout = qobject_cast<QBoxLayout *>(ui->topLevelWidget->layout()))
    layout->insertWidget(layout->indexOf(ui->horizontalWidget), m_connectionsPanel);

  updateModeIcons();
}

void MainWindow::updateModeIcons()
{
  ui->rbModeServer->setIcon(theme::serverModeIcon());
  ui->rbModeClient->setIcon(theme::clientModeIcon());
}

//////////////////////////////////////////////////////////////////////////////
// Begin slots
//////////////////////////////////////////////////////////////////////////////

// remember: using queued connection allows the render loop to run before
// executing the slot. the default is to instantly call the slot when the
// signal is emitted from the thread that owns the receiver's object.
void MainWindow::connectSlots()
{
  connect(Settings::instance(), &Settings::serverSettingsChanged, this, &MainWindow::serverConfigSaving);
  connect(Settings::instance(), &Settings::settingsChanged, this, &MainWindow::settingsChanged);

  connect(&m_coreProcess, &CoreProcess::error, this, &MainWindow::coreProcessError);
  connect(&m_coreProcess, &CoreProcess::logLine, this, &MainWindow::handleLogLine);
  connect(
      &m_coreProcess, &CoreProcess::processStateChanged, this, &MainWindow::coreProcessStateChanged,
      Qt::QueuedConnection
  );
  connect(&m_coreProcess, &CoreProcess::connectionStateChanged, this, &MainWindow::coreConnectionStateChanged);
  connect(&m_coreProcess, &CoreProcess::secureSocket, this, &MainWindow::secureSocket);
  connect(
      &m_coreProcess, &CoreProcess::daemonIpcClientConnectionFailed, this, &MainWindow::daemonIpcClientConnectionFailed
  );
  connect(&m_coreProcess, &CoreProcess::securityLevelChanged, m_statusBar, &StatusBar::setSecurityLevel);

  connect(m_actionAbout, &QAction::triggered, this, &MainWindow::openAboutDialog);
  connect(m_actionMinimize, &QAction::triggered, this, &MainWindow::hide);

  connect(m_actionQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionTrayQuit, &QAction::triggered, this, &MainWindow::close);
  connect(m_actionRestore, &QAction::triggered, this, &MainWindow::showAndActivate);
  connect(m_actionSettings, &QAction::triggered, this, &MainWindow::openSettings);
  connect(m_actionStartCore, &QAction::triggered, this, &MainWindow::startCore);
  connect(m_actionRestartCore, &QAction::triggered, this, &MainWindow::resetCore);
  connect(m_actionStopCore, &QAction::triggered, this, &MainWindow::stopCore);
  connect(m_actionShowHelp, &QAction::triggered, this, &MainWindow::showHelpViewer);

  // Mac os tray will only show a menu
  if (!deskflow::platform::isMac())
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::trayIconActivated);

  connect(&m_coreProcess, &CoreProcess::connectedClientsChanged, this, &MainWindow::serverClientsChanged);
  connect(&m_coreProcess, &CoreProcess::unrecognisedClient, this, &MainWindow::handleUnrecognisedClient);
  connect(&m_coreProcess, &CoreProcess::connectionRefused, this, &MainWindow::handleConnectionRefused);
  connect(&m_coreProcess, &CoreProcess::retryIn, this, &MainWindow::updateTimeoutDelay);
  connect(&m_coreProcess, &CoreProcess::peerFingerprint, this, &MainWindow::handlePeerFingerprint);
  connect(&m_coreProcess, &CoreProcess::missingKeyboardLayouts, this, &MainWindow::handleMissingKeyboardLayouts);

  connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);

  connect(ui->btnRestartCore, &QPushButton::clicked, this, &MainWindow::resetCore);

  connect(ui->cbHostname->lineEdit(), &QLineEdit::returnPressed, ui->btnToggleCore, &QPushButton::click);
  connect(ui->cbHostname, &QComboBox::editTextChanged, this, &MainWindow::remoteHostChanged);

  connect(ui->btnSaveServerConfig, &QPushButton::clicked, this, &MainWindow::saveServerConfig);
  connect(ui->btnConfigureServer, &QPushButton::clicked, this, [this] { showConfigureServer(""); });
  connect(ui->btnConfigureClient, &QPushButton::clicked, this, [this] { showConfigureClient(); });
  connect(ui->lblComputerName, &QLabel::linkActivated, this, &MainWindow::openSettings);

  connect(ui->rbModeServer, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);
  connect(ui->rbModeClient, &QRadioButton::toggled, this, &MainWindow::coreModeToggled);

  connect(m_logDock->toggleViewAction(), &QAction::toggled, this, &MainWindow::toggleLogVisible);

  connect(m_statusBar, &StatusBar::requestShowMyFingerprints, this, &MainWindow::showMyFingerprint);
  connect(m_statusBar, &StatusBar::requestUpdateVersion, this, &MainWindow::showUpdate);

  connect(m_guiDupeChecker, &QLocalServer::newConnection, this, &MainWindow::showAndActivate);

  connect(ui->btnEditName, &QPushButton::clicked, this, &MainWindow::showHostNameEditor);

  connect(ui->lineEditName, &QLineEdit::editingFinished, this, &MainWindow::setHostName);

  connect(m_networkMonitor, &NetworkMonitor::ipAddressesChanged, this, &MainWindow::updateIpLabel);

  connect(m_profileBar, &ProfileBar::switchRequested, this, &MainWindow::switchProfile);
  connect(m_profileBar, &ProfileBar::profilesChanged, this, &MainWindow::refreshProfiles);

  connect(ui->btnFindServers, &QPushButton::clicked, this, [this] { findServers(); });
  connect(m_discovery, &ServerDiscovery::scanFinished, this, &MainWindow::serversFound);
  connect(m_discovery, &ServerDiscovery::clientSeen, this, &MainWindow::rememberClient);
  connect(m_discovery, &ServerDiscovery::latencyMeasured, m_connectionsPanel, &ConnectionsPanel::setLatency);

  connect(m_actionSetupWizard, &QAction::triggered, this, &MainWindow::showOnboarding);
  connect(m_watchTimer, &QTimer::timeout, this, &MainWindow::watchSystem);
  m_watchTimer->start(kWatchInterval);
  QTimer::singleShot(0, this, &MainWindow::watchSystem);
}

void MainWindow::toggleLogVisible(bool visible)
{
  // When the main window is hidden e.g. close to tray / minimized), this also triggers the log visibility toggle,
  // but we don't want to hide the log in this case since we would need to un-hide it when the window is shown again.
  if (!isVisible() || isMinimized()) {
    qDebug() << "not toggling log, window not visible";
    return;
  }

  setFixedSize(16777215, 16777215);
  Settings::setValue(Settings::Gui::LogExpanded, visible);
  if (visible) {
    if (m_logDock->isFloating()) {
      adjustSize();
      setFixedSize(size());
    } else {
      QTimer::singleShot(15, this, [&] { resize(m_expandedSize); });
    }
  } else {
    if (!m_logDock->isFloating()) {
      m_expandedSize = geometry().size();
    }
    m_logDock->hide();
    if (!m_logDock->isFloating()) {
      adjustSize();
    }
    setFixedSize(size());
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
}

void MainWindow::settingsChanged(const QString &key)
{
  if (key == Settings::Core::ComputerName || key == Settings::Core::Port || key == Settings::Server::Discoverable ||
      key == Settings::Security::TlsEnabled)
    updateDiscovery();

  if (key == Settings::Log::Level) {
    m_coreProcess.applyLogLevel();
    return;
  }

  if (key == Settings::Core::ComputerName)
    updateComputerName();

  if ((key == Settings::Security::Certificate) || (key == Settings::Security::KeySize) ||
      (key == Settings::Security::TlsEnabled) || (key == Settings::Security::CheckPeers)) {
    if (TlsUtility::isEnabled()) {
      if (!TlsUtility::isCertValid()) {
        qWarning() << tr("invalid certificate, generating a new one");
        TlsUtility::generateCertificate();
      }
      m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
    }
    updateFingerprintButton();
    updateFileReceiver();
    updateSecurityIcon(m_statusBar->securityIconVisible());
    return;
  }
}

void MainWindow::serverConfigSaving()
{
  m_serverConfig.commit();
}

void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
  if (reason != QSystemTrayIcon::Trigger)
    return;
  isVisible() ? hide() : showAndActivate();
}

void MainWindow::coreProcessError(CoreProcess::Error error)
{
  if (error == CoreProcess::Error::AddressMissing) {
    QMessageBox::warning(
        this, tr("Address missing"), tr("Please enter the hostname or IP address of the other computer.")
    );
  } else if (error == CoreProcess::Error::StartFailed) {
    show();
    auto message = tr("The Core executable could not be started.\n"
                      "Please check if you have sufficient permissions to run %1.")
                       .arg(kCoreBinName);

    if (Settings::value(Settings::Core::CoreMode) == Settings::CoreMode::Server) {
      const auto mode =
          Settings::value(Settings::Server::ExternalConfigFile).toBool() ? tr("read") : tr("read and write");
      message.append(tr("\nAdditionally, check you are able to %1 the server config file: %2")
                         .arg(mode, Settings::serverConfigFile()));
    }
    QMessageBox::warning(this, kAppName, message);
  }
}

void MainWindow::startCore()
{
  // No Mode Set
  if (m_coreProcess.mode() == CoreMode::None) {
    qDebug().noquote() << "skipping core start, no mode set";
    return;
  }

  // Invalid client settings
  if (m_coreProcess.mode() == CoreMode::Client && Settings::value(Settings::Client::RemoteHost).toString().isEmpty()) {
    qDebug().noquote() << "skipping core start, undefined remotehost";
    return;
  }

  // Save current IP state when server starts
  if (m_coreProcess.mode() == CoreMode::Server && Settings::value(Settings::Core::Interface).toString().isEmpty()) {
    m_serverStartIPs = NetworkMonitor::validAddresses();
    m_serverStartSuggestedIP = m_serverStartIPs.isEmpty() ? "" : m_serverStartIPs.first();
  }

  m_actionStartCore->setVisible(false);
  m_actionRestartCore->setVisible(true);
  m_coreProcess.start();
}

void MainWindow::stopCore()
{
  qDebug() << "stopping core process";
  m_coreProcess.stop();
  m_actionStartCore->setVisible(true);
  m_actionRestartCore->setVisible(false);
}

void MainWindow::clearSettings()
{
  qDebug() << "clearing settings";

  m_networkMonitor->stopMonitoring();

  disconnect(&m_coreProcess, nullptr, this, nullptr);
  disconnect(m_guiDupeChecker, nullptr, this, nullptr);
  disconnect(m_trayIcon, nullptr, this, nullptr);
  disconnect(m_logDock->toggleViewAction(), nullptr, this, nullptr);

  m_coreProcess.stop();
  m_coreProcess.clearSettings();

  m_saveOnExit = false;
  diagnostic::clearSettings(true);
}

bool MainWindow::saveServerConfig()
{
  QString fileName = QFileDialog::getSaveFileName(this, tr("Save server configuration as..."));

  if (!fileName.isEmpty() && !m_serverConfig.save(fileName)) {
    QMessageBox::warning(this, tr("Save failed"), tr("Could not save server configuration to file."));
    return true;
  }

  return false;
}

void MainWindow::openAboutDialog()
{
  AboutDialog about(this);
  about.exec();
}

void MainWindow::openSettings()
{
  auto dialog = SettingsDialog(this, m_serverConfig);

  connect(&dialog, &SettingsDialog::requestRemoveAllSettings, this, &MainWindow::clearSettings, Qt::UniqueConnection);
  if (dialog.exec() == QDialog::Accepted) {
    Settings::save();
    disconnect(&dialog, &SettingsDialog::requestRemoveAllSettings, nullptr, nullptr);

    applyConfig();

    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
  }
}

void MainWindow::resetCore()
{
  m_coreProcess.restart();
}

void MainWindow::showMyFingerprint()
{
  FingerprintDialog fingerprintDialog(this, m_fingerprint);
  fingerprintDialog.exec();
}

void MainWindow::coreModeToggled(bool checked)
{
  // this method is called when rbClient or rbServer toggles
  // with both being in the same group one must be turned on if the other is turned off
  // only react to toggle on to avoid calling everything twice when the user switches modes
  if (!checked)
    return;

  Settings::CoreMode mode = Settings::CoreMode::None;

  if (ui->rbModeServer->isChecked())
    mode = Settings::CoreMode::Server;
  if (ui->rbModeClient->isChecked())
    mode = Settings::CoreMode::Client;

  qDebug() << QStringLiteral("change mode to: %1").arg(QVariant::fromValue(mode).toString());

  if (m_coreProcess.isStarted() && m_coreProcess.mode() != mode)
    m_coreProcess.stop();
  m_coreProcess.setMode(mode);

  Settings::setValue(Settings::Core::CoreMode, mode);
  Settings::save();

  updateModeControls();
  updateDiscovery();

  // Save typing the server address when there is one to find
  if (mode == Settings::CoreMode::Client && ui->cbHostname->currentText().isEmpty())
    findServers(true);
}

void MainWindow::updateModeControls()
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;

  ui->serverOptions->setVisible(isServer);
  ui->clientOptions->setVisible(isClient);
  ui->lblNoMode->setVisible(!isServer && !isClient);
  toggleCanRunCore(canRunCore());

  ui->lblIpAddresses->setVisible(
      (isClient && !Settings::value(Settings::Core::Interface).toString().isEmpty()) || isServer
  );

  if (ui->lblIpAddresses->isVisible())
    updateNetworkInfo();

  if (isServer) {
    m_networkMonitor->startMonitoring();
  } else {
    m_networkMonitor->stopMonitoring();
  }

  if (isServer || isClient)
    updateModeControlLabels();
}

void MainWindow::updateModeControlLabels()
{
  const bool isServer = m_coreProcess.mode() == CoreMode::Server;
  const bool isStarted = m_coreProcess.isStarted();

  QString startText;
  QString stopText;
  QIcon startIcon;
  QIcon stopIcon;

  if (isServer) {
    startText = tr("Start");
    stopText = tr("Stop");
    startIcon = QIcon::fromTheme(QStringLiteral("system-run"));
    stopIcon = QIcon::fromTheme(QIcon::ThemeIcon::ProcessStop);
  } else {
    startText = tr("Connect");
    stopText = tr("Disconnect");
    startIcon = QIcon::fromTheme(QStringLiteral("network-connect"));
    stopIcon = QIcon::fromTheme(QStringLiteral("network-disconnect"));
  }

  m_actionStartCore->setText(startText);
  m_actionStartCore->setIcon(startIcon);
  m_actionStopCore->setText(stopText);
  m_actionStopCore->setIcon(stopIcon);

  // Lets the style sheet show the button as a stop action while the core runs
  if (ui->btnToggleCore->property("running").toBool() != isStarted) {
    ui->btnToggleCore->setProperty("running", isStarted);
    ui->btnToggleCore->style()->unpolish(ui->btnToggleCore);
    ui->btnToggleCore->style()->polish(ui->btnToggleCore);
  }

  // The main button is styled by color, the icons are kept for menus only
  ui->btnToggleCore->setText(isStarted ? stopText : startText);
}

void MainWindow::updateSecurityIcon(bool visible)
{
  m_statusBar->setSecurityIconVisible(visible);
  if (!visible)
    return;
  m_statusBar->setSecurityIcon(TlsUtility::isEnabled());
}

void MainWindow::updateNetworkInfo()
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  if (mode == CoreMode::Server)
    updateIpLabel(NetworkMonitor::validAddresses());
  else
    updateIpLabel({Settings::value(Settings::Core::Interface).toString()});
}

void MainWindow::serverConnectionConfigureClient(const QString &clientName)
{
  m_serverConfigDialogVisible = true;
  ServerConfigDialog dialog(this, m_serverConfig);
  if (dialog.addClient(clientName) && dialog.exec() == QDialog::Accepted) {
    m_coreProcess.restart();
  }
  m_serverConfigDialogVisible = false;
}

//////////////////////////////////////////////////////////////////////////////
// End slots
//////////////////////////////////////////////////////////////////////////////

void MainWindow::open()
{
  if (!Settings::value(Settings::Gui::Autohide).toBool())
    showAndActivate();
  else if (deskflow::platform::isMac())
    // macOS to call hide after this function ends
    QTimer::singleShot(1, this, &MainWindow::hide);
  else
    hide();

  // if a critical error was shown just before the main window (i.e. on app
  // load), it will be hidden behind the main window. therefore we need to raise
  // it up in front of the main window.
  // HACK: because the `onShown` event happens just as the window is shown, the
  // message box has a chance of being raised under the main window. to solve
  // this we delay the error dialog raise by a split second. this seems a bit
  // hacky and fragile, so maybe there's a better approach.
  const auto kCriticalDialogDelay = 100;
  QTimer::singleShot(kCriticalDialogDelay, this, &messages::raiseCriticalDialog);

  if (!Settings::value(Settings::Gui::AutoUpdateCheck).isValid()) {
    Settings::setValue(Settings::Gui::AutoUpdateCheck, messages::showUpdateCheckOption(this));
  }

  if (Settings::value(Settings::Gui::AutoUpdateCheck).toBool()) {
    checkForUpdates(false);
    m_updateTimer->start();
  } else {
    qDebug() << "skipping check for new version, disabled";
  }

  if (Settings::value(Settings::Gui::AutoStartCore).toBool()) {
    startCore();
  }

  maybeShowOnboarding();
}

void MainWindow::createMenuBar()
{
  m_menuFile->addAction(m_actionStartCore);
  m_menuFile->addAction(m_actionRestartCore);
  m_menuFile->addAction(m_actionStopCore);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionSendFiles);
  m_menuFile->addMenu(m_menuWake);
  m_menuFile->addSeparator();
  m_menuFile->addAction(m_actionQuit);

  m_menuEdit->addAction(m_actionSettings);
  m_menuEdit->addSeparator();
  m_menuEdit->addAction(m_actionPasteFiles);
  m_menuEdit->addMenu(m_menuClipboard);
  m_menuEdit->addSeparator();
  m_menuEdit->addAction(m_actionDimInactive);
  m_menuEdit->addAction(m_actionLockTogether);

  m_menuView->addAction(m_logDock->toggleViewAction());

  m_menuHelp->addAction(m_actionAbout);
  m_menuHelp->addAction(m_actionShowHelp);
  m_menuHelp->addAction(m_actionSetupWizard);
  m_menuHelp->addSeparator();
  m_menuHelp->addAction(m_actionCheckUpdates);
  m_menuHelp->addAction(m_actionTestBuilds);

  auto menuBar = new QMenuBar(this);
  menuBar->addMenu(m_menuFile);
  menuBar->addMenu(m_menuEdit);
  menuBar->addMenu(m_menuView);
  menuBar->addMenu(m_menuHelp);

  setMenuBar(menuBar);
}

void MainWindow::setupTrayIcon()
{
  auto trayMenu = new QMenu(this);
  trayMenu->addActions(
      {m_actionStartCore, m_actionRestartCore, m_actionStopCore, m_actionMinimize, m_actionRestore, m_actionTrayQuit}
  );
  trayMenu->insertSeparator(m_actionMinimize);
  trayMenu->insertMenu(m_actionMinimize, m_menuProfiles);
  trayMenu->insertMenu(m_actionMinimize, m_menuClipboard);
  trayMenu->insertAction(m_actionMinimize, m_actionSendFiles);
  trayMenu->insertAction(m_actionMinimize, m_actionPasteFiles);
  trayMenu->insertMenu(m_actionMinimize, m_menuWake);
  trayMenu->insertSeparator(m_actionMinimize);
  trayMenu->insertSeparator(m_actionTrayQuit);
  m_trayIcon->setContextMenu(trayMenu);
  refreshProfiles();

  setTrayIcon();
  m_trayIcon->show();
}

void MainWindow::applyConfig()
{
  if (Settings::value(Settings::Gui::ShowVersionInTitle).toBool()) {
    setWindowTitle(QStringLiteral("%1 - %2").arg(kAppName, kDisplayVersion));
  } else {
    setWindowTitle(kAppName);
  }

  const auto host = Settings::value(Settings::Client::RemoteHost).toString();
  QSignalBlocker hostnameBlocker(ui->cbHostname);
  ui->cbHostname->clear();
  ui->cbHostname->addItems(Settings::value(Settings::Client::RecentRemoteHosts).toStringList());
  if (!host.isEmpty())
    ui->cbHostname->setCurrentText(host);

  updateFingerprintButton();
  setTrayIcon();

  if (const auto ip = Settings::value(Settings::Core::Interface).toString(); !ip.isEmpty()) {
    m_serverStartIPs = {ip};
    m_serverStartSuggestedIP = ip;
  }

  coreModeToggled(true);
}

void MainWindow::saveSettings() const
{
  if (ui->rbModeClient->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  } else if (ui->rbModeServer->isChecked()) {
    Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
  }
  if (!ui->cbHostname->currentText().isEmpty()) {
    Settings::setValue(Settings::Client::RemoteHost, ui->cbHostname->currentText());
  }
  Settings::save();
}

void MainWindow::setTrayIcon()
{
  static const auto fallbackPath = QStringLiteral(":/icons/%1-%2/apps/64/%3");

  QString themeIcon = kRevFqdnName;
  if (!Settings::value(Settings::Gui::SymbolicTrayIcon).toBool()) {
    if (deskflow::platform::isMac())
      m_trayIcon->setIcon(QIcon::fromTheme(themeIcon));
    else
      m_trayIcon->setIcon(QIcon(fallbackPath.arg(kAppId, QStringLiteral("dark"), themeIcon)));
    return;
  }

  themeIcon.append(QStringLiteral("-symbolic"));

  if (deskflow::platform::isWindows()) {
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QString theme = settings.value(QStringLiteral("SystemUsesLightTheme"), 1).toBool() ? QStringLiteral("light")
                                                                                             : QStringLiteral("dark");
    m_trayIcon->setIcon(QIcon(fallbackPath.arg(kAppId, theme, themeIcon)));
    return;
  }

  auto icon = QIcon::fromTheme(themeIcon, QIcon(fallbackPath.arg(kAppId, iconMode(), themeIcon)));
  icon.setIsMask(true);
  m_trayIcon->setIcon(icon);
}

void MainWindow::handleLogLine(const QString &line)
{
  m_logDock->appendLine(line);

  // Where the cursor is, to dim the computer not in use
  if (m_coreProcess.mode() == CoreMode::Client) {
    if (line.contains(QLatin1String("cursor entered this screen")))
      setActiveHere(true);
    else if (line.contains(QLatin1String("cursor left this screen")))
      setActiveHere(false);
    return;
  }

  static const QRegularExpression switchPattern(QStringLiteral(R"re(switch from "[^"]*" to "([^"]*)")re"));
  const auto match = switchPattern.match(line);
  if (match.hasMatch())
    setActiveHere(match.captured(1) == Settings::value(Settings::Core::ComputerName).toString());
}

void MainWindow::setActiveHere(bool active)
{
  m_dimmer->setDimmed(!active && m_actionDimInactive->isChecked() && m_coreProcess.isStarted());
}

void MainWindow::handleUnrecognisedClient(const QString &clientName)
{
  if (m_ignoredClients.contains(clientName)) {
    qDebug("ignoring %s:", qPrintable(clientName));
    return;
  }

  if (m_newClientPromptShowing || m_serverConfigDialogVisible)
    return;

  if (Settings::value(Settings::Server::ExternalConfig).toBool())
    return;

  if (m_serverConfig.isFull() || m_serverConfig.computerExists(clientName))
    return;

  m_newClientPromptShowing = true;

  showAndActivate();

  if (deskflow::gui::messages::showNewClientPrompt(this, clientName)) {
    serverConnectionConfigureClient(clientName);
  } else {
    m_ignoredClients.insert(clientName);
  }

  m_newClientPromptShowing = false;
}

void MainWindow::handleConnectionRefused(deskflow::core::ConnectionRefusal reason)
{
  if (reason != deskflow::core::ConnectionRefusal::AlreadyConnected)
    return;

  if (!isVisible() || m_clientErrorVisible)
    return;

  m_clientErrorVisible = true;
  showAndActivate();

  const auto address = Settings::value(Settings::Client::RemoteHost).toString();
  QMessageBox::warning(
      this, tr("%1 Connection Error").arg(kAppName),
      tr("<p>Failed to connect to the server '%1'.</p>"
         "<p>A Client with your name is already connected to the server.</p>"
         "Please ensure that you're using a unique name and that only a "
         "single instance of the client process is running.</p>")
          .arg(address)
  );

  m_clientErrorVisible = false;
}

void MainWindow::handleMissingKeyboardLayouts(const QString &layouts)
{
  if (Settings::value(Settings::Gui::IgnoreMissingKeyboardLayouts).toBool())
    return;

  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(tr("Missing Keyboard Layouts"));
  msgBox.setText(tr("<p>Keyboard layout support requires matching layouts on all computers. "
                    "The following layouts from the other computer are not installed on this computer:</p>"
                    "<p><b>%1</b></p>"
                    "<p>Please install them to enable support for these layouts.</p>")
                     .arg(layouts));

  auto *checkBox = new QCheckBox(tr("Don't show this again"), &msgBox);
  msgBox.setCheckBox(checkBox);
  msgBox.exec();

  if (checkBox->isChecked()) {
    Settings::setValue(Settings::Gui::IgnoreMissingKeyboardLayouts, true);
  }
}

void MainWindow::handlePeerFingerprint(const QString &fingerprint)
{
  const auto sha256Text = QString(fingerprint).remove(':');
  const Fingerprint sha256 = {QCryptographicHash::Sha256, QByteArray::fromHex(sha256Text.toLatin1())};

  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  if ((isClient && m_checkedServers.contains(sha256Text)) || (!isClient && m_checkedClients.contains(sha256Text))) {
    qDebug("ignoring fingerprint, already handled");
    return;
  }

  FingerprintDatabase db;
  db.read(trustedFingerprintDatabase());

  if (db.isTrusted(sha256)) {
    qDebug("fingerprint is trusted");
    return;
  }

  if (isClient) {
    m_checkedServers.append(sha256Text);
    m_coreProcess.stop();
  } else {
    m_checkedClients.append(sha256Text);
  }

  auto mode = isClient ? FingerprintDialogMode::Client : FingerprintDialogMode::Server;
  FingerprintDialog fingerprintDialog(this, m_fingerprint, mode, sha256);

  if (fingerprintDialog.exec() == QDialog::Accepted) {
    db.addTrusted(sha256);
    if (!db.write(trustedFingerprintDatabase())) {
      qCritical().noquote() << "unable to write fingerprint to db:" << trustedFingerprintDatabase();
      m_coreProcess.stop();
      return;
    }
    if (isClient) {
      m_checkedServers.removeAll(sha256Text);
      m_coreProcess.start();
    } else {
      m_checkedClients.removeAll(sha256Text);
    }
  }
}

bool MainWindow::maybeHideToTray()
{
  if (!Settings::value(Settings::Gui::CloseToTray).toBool()) {
    return false;
  }

  if (Settings::value(Settings::Gui::CloseReminder).toBool()) {
    messages::showCloseReminder(this);
    Settings::setValue(Settings::Gui::CloseReminder, false);
  }
  Settings::setValue(Settings::Gui::WindowGeometry, geometry());
  qDebug() << "hiding to tray";
  hide();
  return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (event->spontaneous() && maybeHideToTray()) {
    event->ignore();
    return;
  }

  if (m_saveOnExit) {
    Settings::setValue(Settings::Gui::WindowGeometry, geometry());
    Settings::setValue(Settings::Gui::AutoStartCore, m_coreProcess.isStarted());
  }
  qDebug() << "quitting application";

  // any connected dock view acitons will be triggered
  // disconnect them before accepting the event
  disconnect(m_logDock->toggleViewAction(), &QAction::toggled, nullptr, nullptr);

  event->accept();
  QApplication::quit();
}

void MainWindow::showFirstConnectedMessage()
{
  if (Settings::value(Settings::Gui::ShownFirstConnectedMessage).toBool())
    return;
  Settings::setValue(Settings::Gui::ShownFirstConnectedMessage, true);
  messages::showFirstConnectedMessage(this);
}

void MainWindow::updateStatus()
{
  using enum ProcessState;
  const auto connection = m_coreProcess.connectionState();
  const auto process = m_coreProcess.processState();
  const bool isServer = (m_coreProcess.mode() == CoreMode::Server);
  if (process == Stopped || process == Started) {
    updateNetworkInfo();
    ui->btnEditName->setVisible(process == Stopped);
  }
  m_statusBar->setStatus(connection, process, isServer);
}

void MainWindow::coreProcessStateChanged(ProcessState state)
{
  using enum ProcessState;
  updateStatus();
  updateDiscovery();
  updateLink();
  updateFileReceiver();
  updatePasteAction();
  if (!m_coreProcess.isStarted())
    m_dimmer->setDimmed(false);

  if (state == Stopped || state == Stopping) {
    m_connectionsPanel->clear();
  } else if (state == Started) {
    if (m_coreProcess.mode() == CoreMode::Server)
      m_connectionsPanel->showServer({});
    else if (m_coreProcess.mode() == CoreMode::Client)
      m_connectionsPanel->showClient(ConnectionState::Connecting, ui->cbHostname->currentText());
  }
  if (state == Started) {
    qDebug() << "recording that core has started";
    Settings::setValue(Settings::Gui::AutoStartCore, true);
    if (m_coreProcess.mode() == CoreMode::Server &&
        !Settings::value(Settings::Gui::ShownServerFirstStartMessage).toBool()) {
      qDebug() << "starting core server for first time";
      messages::showFirstServerStartMessage(this);
      Settings::setValue(Settings::Gui::ShownServerFirstStartMessage, true);
    }
  }

  if (state == Started || state == Starting || state == RetryPending) {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(true);
    m_actionStartCore->setVisible(false);
    m_actionRestartCore->setVisible(true);
    m_actionStopCore->setEnabled(true);

    if (state == Starting) {
      saveSettings();
    }

  } else {
    disconnect(ui->btnToggleCore, &QPushButton::clicked, m_actionStopCore, &QAction::trigger);
    connect(ui->btnToggleCore, &QPushButton::clicked, m_actionStartCore, &QAction::trigger, Qt::UniqueConnection);

    ui->btnRestartCore->setEnabled(false);
    m_actionStartCore->setVisible(true);
    m_actionRestartCore->setVisible(false);
    m_actionStopCore->setEnabled(false);
  }
  updateModeControlLabels();
}

void MainWindow::coreConnectionStateChanged(ConnectionState state)
{
  qDebug() << "core connection state changed:" << static_cast<int>(state);

  updateStatus();

  if (m_coreProcess.mode() == CoreMode::Client && m_coreProcess.isStarted())
    m_connectionsPanel->showClient(state, ui->cbHostname->currentText());
  updateLink();
  // A client that just connected starts without the cursor, anything else is not dimmed
  if (m_coreProcess.mode() == CoreMode::Client && state == ConnectionState::Connected)
    setActiveHere(false);
  else if (state != ConnectionState::Connected)
    m_dimmer->setDimmed(false);
  updatePasteAction();
  m_actionSendFiles->setEnabled(TlsUtility::isEnabled() && m_coreProcess.isStarted());

  // always assume connection is not secure when connection changes
  // to anything except connected. the only way the padlock shows is
  // when the correct TLS version string is detected.
  if (state != ConnectionState::Connected) {
    secureSocket(false);
  } else {
    if (m_coreProcess.mode() == CoreMode::Client) {
      if (const auto host = ui->cbHostname->currentText(); !host.isEmpty()) {
        QStringList recentHosts = Settings::value(Settings::Client::RecentRemoteHosts).toStringList();
        if (recentHosts.isEmpty() || recentHosts.first() != host) {
          recentHosts.removeAll(host);
          recentHosts.prepend(host);
          while (recentHosts.size() > 10) {
            recentHosts.removeLast();
          }
          Settings::setValue(Settings::Client::RecentRemoteHosts, recentHosts);
          QSignalBlocker hostnameBlocker(ui->cbHostname);
          ui->cbHostname->clear();
          ui->cbHostname->addItems(recentHosts);
          ui->cbHostname->setCurrentText(host);
        }
      }
    }
    if (isVisible()) {
      showFirstConnectedMessage();
    }
  }
}

void MainWindow::updateFingerprintButton()
{
  const bool show = TlsUtility::isEnabled() && !m_fingerprint.data.isEmpty();
  m_statusBar->setBtnFingerprintVisible(show);

  // Shown all the time so it can be compared when another computer connects for the first time
  const auto code = show ? pairingCode(m_fingerprint.data) : QString();
  ui->lblPairingCode->setVisible(!code.isEmpty());
  //: %1 is a code like "482 913 077 251"
  ui->lblPairingCode->setText(tr("Pairing code: %1").arg(code));
  ui->lblPairingCode->setToolTip(
      tr("Other computers show this code the first time they connect. Only accept them if the codes match.")
  );
}

void MainWindow::hide()
{
#ifdef Q_OS_MACOS
  macOSNativeHide();
#else
  QMainWindow::hide();
#endif
  m_actionRestore->setVisible(true);
  m_actionMinimize->setVisible(false);
}

void MainWindow::changeEvent(QEvent *e)
{
  QMainWindow::changeEvent(e);
  if (e->type() == QEvent::PaletteChange) {
    updateIconTheme();
    setWindowIcon(QIcon::fromTheme(kRevFqdnName));
    setTrayIcon();
    updateModeIcons();
  } else if (e->type() == QEvent::LanguageChange) {
    ui->retranslateUi(this);
    updateModeControlLabels();
    updateNetworkInfo();
    updateStatus();
    serverClientsChanged({});
    updateText();
  }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (obj == centralWidget()) {
    if (event->type() == QEvent::LayoutRequest && !m_fitPending) {
      m_fitPending = true;
      QTimer::singleShot(0, this, &MainWindow::fitToContents);
    }
    return false;
  }

  if (obj != ui->lineEditName || event->type() != QEvent::KeyPress)
    return false;
  if (const auto keyEvent = static_cast<QKeyEvent *>(event); keyEvent->key() != Qt::Key_Escape)
    return false;
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  ui->lineEditName->setText(Settings::value(Settings::Core::ComputerName).toString());
  toggleCanRunCore(canRunCore());
  return true;
}

void MainWindow::updateText()
{
  m_menuFile->setTitle(tr("&File"));
  m_menuEdit->setTitle(tr("&Edit"));
  m_menuView->setTitle(tr("&View"));
  m_menuHelp->setTitle(tr("&Help"));
  m_menuProfiles->setTitle(tr("&Profiles"));
  m_menuClipboard->setTitle(tr("&Clipboard history"));
  m_menuWake->setTitle(tr("&Wake up"));
  m_actionDimInactive->setText(tr("&Dim this computer when the cursor is on another"));
  m_actionLockTogether->setText(tr("&Lock all computers together"));
  m_actionLockTogether->setToolTip(tr("When one connected computer is locked, the others lock too"));
  updatePasteAction();
  m_actionSendFiles->setText(tr("Send &files..."));
  m_actionSendFiles->setToolTip(tr("Send files to a connected computer, or drop them on this window"));

  m_actionMinimize->setText(tr("&Minimize to tray"));
  m_actionQuit->setText(tr("&Quit"));
  m_actionTrayQuit->setText(tr("&Quit"));
  //: %1 will be the replaced with the appname
  m_actionRestore->setText(tr("&Open %1").arg(kAppName));
  m_actionSettings->setText(tr("&Preferences"));
  m_actionStartCore->setText(tr("&Start"));
  m_actionRestartCore->setText(tr("Rest&art"));
  m_actionStopCore->setText(tr("S&top"));
  //: %1 will be the replaced with the appname
  m_actionAbout->setText(tr("About %1...").arg(kAppName));

  m_actionShowHelp->setText(tr("View &Help"));
  m_actionSetupWizard->setText(tr("&Setup Wizard..."));
  m_actionCheckUpdates->setText(tr("Check for &Updates..."));
  m_actionTestBuilds->setText(tr("Include &test builds in updates"));
  m_actionTestBuilds->setToolTip(tr("Also update to builds made from the latest changes, not only to releases"));

  //: start / restart core shortcut
  m_actionStartCore->setShortcut(QKeySequence(tr("Ctrl+S")));
  m_actionRestartCore->setShortcut(QKeySequence(tr("Ctrl+S")));

  //: stop core shortcut
  m_actionStopCore->setShortcut(QKeySequence(tr("Ctrl+T")));

  if (deskflow::platform::isWindows()) {
    //: Quit shortcut
    m_actionQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
    m_actionTrayQuit->setShortcut(QKeySequence(tr("Ctrl+Q")));
  }
}

void MainWindow::showConfigureServer(const QString &message)
{
  ServerConfigDialog dialog(this, serverConfig());
  dialog.message(message);
  if ((dialog.exec() == QDialog::Accepted) && m_coreProcess.isStarted()) {
    m_coreProcess.restart();
  }
}

void MainWindow::showConfigureClient()
{
  ClientConfigDialog dialog(this);
  if (dialog.exec() == QDialog::Accepted) {
    applyConfig();
    if (m_coreProcess.isStarted()) {
      m_coreProcess.restart();
    }
  }
}

void MainWindow::secureSocket(bool secureSocket)
{
  m_secureSocket = secureSocket;
  updateSecurityIcon(m_statusBar->securityIconVisible());
}

void MainWindow::updateComputerName()
{
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();
  ui->lblComputerName->setText(computerName);
  ui->lineEditName->setText(computerName);
  m_serverConfig.updateServerName();
}

void MainWindow::showAndActivate()
{
  const auto wasVisible = isVisible();
#ifdef Q_OS_MACOS
  forceAppActive();
#endif
  showNormal();
  raise();
  activateWindow();
  m_actionRestore->setVisible(false);
  m_actionMinimize->setVisible(true);
  if (!wasVisible)
    restoreWindow();
}

void MainWindow::showHostNameEditor()
{
  ui->lineEditName->show();
  ui->lblComputerName->hide();
  ui->btnEditName->hide();
  toggleCanRunCore(false);
  ui->lineEditName->setFocus();
}

void MainWindow::setHostName()
{
  ui->lineEditName->hide();
  ui->lblComputerName->show();
  ui->btnEditName->show();
  toggleCanRunCore(canRunCore());

  QString text = ui->lineEditName->text();
  const auto computerName = Settings::value(Settings::Core::ComputerName).toString();

  if (text == computerName)
    return;

  const bool isServer = ui->rbModeServer->isChecked();
  bool existingComputer = false;
  if (isServer)
    existingComputer = serverConfig().computerExists(text);

  if (!ui->lineEditName->hasAcceptableInput() || text.isEmpty() || existingComputer) {
    blockSignals(true);
    ui->lineEditName->setText(computerName);
    blockSignals(false);

    const auto title = tr("Invalid Computer Name");
    QString body;
    if (existingComputer) {
      body = tr("Computer name already exists");
    } else {
      body =
          tr("The name you have chosen is invalid.\n\n"
             "Valid names:\n"
             "• Use letters and numbers\n"
             "• May also use _ or -\n"
             "• Are between 1 and 255 characters");
    }
    QMessageBox::information(this, title, body);
    return;
  }

  ui->lblComputerName->setText(ui->lineEditName->text());
  Settings::setValue(Settings::Core::ComputerName, ui->lineEditName->text());
  if (isServer)
    serverConfig().updateServerName();
  applyConfig();
}

QString MainWindow::trustedFingerprintDatabase() const
{
  const bool isClient = m_coreProcess.mode() == CoreMode::Client;
  return isClient ? Settings::tlsTrustedServersDb() : Settings::tlsTrustedClientsDb();
}

bool MainWindow::generateCertificate()
{
  const auto certificate = Settings::value(Settings::Security::Certificate).toString();
  if (!QFile::exists(certificate) && !TlsUtility::generateCertificate()) {
    return false;
  }

  m_fingerprint = {QCryptographicHash::Sha256, TlsUtility::certFingerprint()};
  updateFingerprintButton();
  return true;
}

void MainWindow::serverClientsChanged(const QStringList &clients)
{
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted())
    return;
  m_statusBar->setServerClients(clients);
  m_connectionsPanel->showServer(clients);
}

void MainWindow::daemonIpcClientConnectionFailed()
{
  if (deskflow::gui::messages::showDaemonOffline(this)) {
    m_coreProcess.retryDaemon();
  }
}

void MainWindow::toggleCanRunCore(bool enableButtons)
{
  const bool isStarted = m_coreProcess.isStarted();
  ui->btnToggleCore->setEnabled(enableButtons);
  ui->btnRestartCore->setEnabled(enableButtons && isStarted);
  m_actionStartCore->setEnabled(enableButtons);
  m_actionStopCore->setEnabled(enableButtons && isStarted);
}

void MainWindow::remoteHostChanged(const QString &newRemoteHost)
{
  m_coreProcess.setAddress(newRemoteHost);
  toggleCanRunCore(!newRemoteHost.isEmpty() && ui->rbModeClient->isChecked());
  if (newRemoteHost.isEmpty()) {
    Settings::setValue(Settings::Client::RemoteHost);
  } else {
    Settings::setValue(Settings::Client::RemoteHost, newRemoteHost);
  }
}

void MainWindow::updateIpLabel(const QStringList &addresses)
{
  const auto mode = m_coreProcess.mode();
  if (mode == CoreMode::None) {
    return;
  }

  static const auto colorText = QStringLiteral(R"(<span style="color:%1;">%2</span>)");
  const bool serverStarted = m_coreProcess.isStarted();
  const bool fixedIP = !Settings::value(Settings::Core::Interface).toString().isEmpty();

  if (!fixedIP && addresses.isEmpty() && !serverStarted || (serverStarted && m_serverStartSuggestedIP.isEmpty())) {
    ui->lblIpAddresses->setText(colorText.arg(palette().linkVisited().color().name(), tr("No IP Detected")));
    ui->lblIpAddresses->setToolTip(tr("Unable to detect an IP address. Check your network connection is active."));
    return;
  }

  QString labelText = fixedIP ? tr("Using IP: ") : tr("Suggested IP: ");
  QString toolTipText = tr("<p>If connecting via the hostname fails, try %1</p>");

  const bool filterIpList = (serverStarted || fixedIP);
  const QRegularExpression ipListFilter(filterIpList ? QStringLiteral("(%1)").arg(m_serverStartIPs.join("|")) : "");
  const QStringList ipList = addresses.filter(ipListFilter);

  bool IPValid = true;
  if (filterIpList && (m_serverStartSuggestedIP != m_currentIpAddress) || !ipList.contains(m_serverStartSuggestedIP)) {
    IPValid = !ipList.isEmpty();
  }

  if (IPValid) {
    m_currentIpAddress = ipList.first();
    labelText.append(m_currentIpAddress);
  } else {
    labelText.append(colorText.arg(palette().linkVisited().color().name(), m_serverStartSuggestedIP));
    toolTipText.append(tr("\nA bound IP is now invalid, you may need to restart the server."));
  }

  if (ipList.count() < 2 || fixedIP) {
    toolTipText = toolTipText.arg(tr("the suggested IP."));
  } else {
    toolTipText = toolTipText.arg(tr("one of the following IPs:<br/>%1").arg(ipList.join("<br/>")));
  }

  ui->lblIpAddresses->setText(labelText);
  ui->lblIpAddresses->setToolTip(mode == CoreMode::Server ? toolTipText : QString());
}

void MainWindow::updateTimeoutDelay(int newDelay)
{
  m_statusBar->setConnectionInterval(newDelay);
}

void MainWindow::setHelpFilePath()
{
  const QString appPath = QCoreApplication::applicationDirPath();
  auto buildPath = QString("%1/../docs/HelpMain.md").arg(appPath);
  auto installPath = QString("%1/../share/doc/%2/HelpMain.md").arg(appPath, kAppId);
  if (deskflow::platform::isMac()) {
    installPath = QString("%1/../Resources/docs/HelpMain.md").arg(appPath);
    buildPath = QString("%1/../../../../docs/HelpMain.md").arg(appPath);
  } else if (deskflow::platform::isWindows()) {
    installPath = QString("%1/docs/HelpMain.md").arg(appPath);
  }

  buildPath = QDir::cleanPath(buildPath);
  installPath = QDir::cleanPath(installPath);

  if (QFile::exists(installPath))
    m_helpPath = QUrl::fromLocalFile(installPath);
  else if (QFile::exists(buildPath))
    m_helpPath = QUrl::fromLocalFile(buildPath);
  else
    m_helpPath = kUrlWiki;
}

void MainWindow::showHelpViewer() const
{
  if (m_helpPath.isLocalFile()) {
    HelpDialog dialogHelp(this->centralWidget(), m_helpPath);
    dialogHelp.exec();
  } else {
    QDesktopServices::openUrl(m_helpPath);
  }
}

bool MainWindow::canRunCore() const
{
  const auto mode = m_coreProcess.mode();
  const bool isServer = mode == Settings::CoreMode::Server;
  const bool isClient = mode == Settings::CoreMode::Client;
  return ((isServer || isClient) && (isClient && !ui->cbHostname->currentText().isEmpty()) || isServer);
}

void MainWindow::switchProfile(const QString &name)
{
  if (name.compare(Profiles::active(), Qt::CaseInsensitive) == 0) {
    refreshProfiles();
    return;
  }

  const bool wasStarted = m_coreProcess.isStarted();

  // Make sure what is on screen is stored in the profile we are leaving
  saveSettings();

  if (!Profiles::switchTo(name)) {
    QMessageBox::warning(this, tr("Profile"), tr("Could not load the profile \"%1\".").arg(name));
    refreshProfiles();
    return;
  }

  m_serverConfig.reload();
  m_serverConfig.updateServerName();

  loadModeFromSettings();

  m_serverStartIPs.clear();
  m_serverStartSuggestedIP.clear();
  applyConfig();
  refreshProfiles();

  //: %1 is the name of a profile
  m_statusBar->showMessage(tr("Profile \"%1\" is now active").arg(Profiles::active()), 5000);

  // Keep running with the new profile, restart waits for the old core to exit first
  if (wasStarted) {
    if (m_coreProcess.mode() == CoreMode::None)
      stopCore();
    else
      m_coreProcess.restart();
  }
}

void MainWindow::refreshClipboardMenu()
{
  // Actions are rebuilt when the menu opens, never while one of them is being triggered
  m_menuClipboard->clear();

  const auto &entries = m_clipboardHistory->entries();
  if (!m_clipboardHistory->isEnabled()) {
    m_menuClipboard->addAction(tr("History is turned off"))->setEnabled(false);
  } else if (entries.isEmpty()) {
    m_menuClipboard->addAction(tr("Nothing copied yet"))->setEnabled(false);
  }

  const auto locale = QLocale();
  for (int i = 0; i < entries.size(); ++i) {
    const auto &entry = entries.at(i);
    auto action = m_menuClipboard->addAction(ClipboardHistory::summary(entry));
    const auto time = locale.toString(entry.time.time(), QLocale::ShortFormat);
    if (entry.isImage()) {
      action->setIcon(QPixmap::fromImage(entry.image.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      //: %1 is the time it was copied
      action->setToolTip(tr("Copied at %1").arg(time));
    } else {
      //: %1 is the time it was copied, %2 the start of the copied text
      action->setToolTip(tr("Copied at %1\n\n%2").arg(time, entry.text.left(300)));
    }
    connect(action, &QAction::triggered, this, [this, i] { m_clipboardHistory->restore(i); });
  }

  m_menuClipboard->addSeparator();
  auto keep = m_menuClipboard->addAction(tr("&Remember copied items"));
  keep->setCheckable(true);
  keep->setChecked(m_clipboardHistory->isEnabled());
  keep->setToolTip(tr("Copied items are kept in memory only and are forgotten when %1 quits. "
                      "Passwords that password managers mark as secret are never remembered.")
                       .arg(kAppName));
  connect(keep, &QAction::toggled, this, [this](bool on) {
    m_clipboardHistory->setEnabled(on);
    Settings::setValue(Settings::Gui::ClipboardHistory, on);
  });

  auto clear = m_menuClipboard->addAction(tr("C&lear history"));
  clear->setEnabled(!entries.isEmpty());
  connect(clear, &QAction::triggered, m_clipboardHistory, &ClipboardHistory::clear);
}

void MainWindow::refreshProfiles()
{
  if (m_profileBar)
    m_profileBar->refresh();

  // Old actions and their group are replaced; deleteLater as one of them may be the action being triggered
  const auto oldGroups = m_menuProfiles->findChildren<QActionGroup *>(QString(), Qt::FindDirectChildrenOnly);
  for (auto oldGroup : oldGroups)
    oldGroup->deleteLater();
  const auto oldActions = m_menuProfiles->actions();
  for (auto oldAction : oldActions) {
    m_menuProfiles->removeAction(oldAction);
    oldAction->deleteLater();
  }

  auto group = new QActionGroup(m_menuProfiles);
  group->setExclusive(true);
  const auto active = Profiles::active();
  const auto profiles = Profiles::list();
  for (const auto &name : profiles) {
    auto action = new QAction(name, group);
    m_menuProfiles->addAction(action);
    action->setCheckable(true);
    action->setChecked(name == active);
    connect(action, &QAction::triggered, this, [this, name] { switchProfile(name); }, Qt::QueuedConnection);
  }
  m_menuProfiles->setEnabled(!profiles.isEmpty());
}

void MainWindow::updateDiscovery()
{
  if (!m_discovery)
    return;

  // A running server always answers its clients' hellos, scans only while discoverable
  if (m_coreProcess.mode() != CoreMode::Server || !m_coreProcess.isStarted()) {
    m_discovery->stopResponding();
    return;
  }

  m_discovery->startResponding(
      Settings::value(Settings::Core::ComputerName).toString(),
      static_cast<quint16>(Settings::value(Settings::Core::Port).toInt()), TlsUtility::isEnabled(),
      Settings::value(Settings::Server::Discoverable).toBool()
  );
}

void MainWindow::updateLink()
{
  if (!m_discovery)
    return;

  const bool connected = m_coreProcess.mode() == CoreMode::Client && m_coreProcess.isStarted() &&
                         m_coreProcess.connectionState() == ConnectionState::Connected;
  if (!connected) {
    m_discovery->stopLink();
    return;
  }
  m_discovery->startLink(Settings::value(Settings::Core::ComputerName).toString(), ui->cbHostname->currentText());
}

void MainWindow::updateFileReceiver()
{
  const bool receive = m_coreProcess.isStarted() && TlsUtility::isEnabled() && !m_fingerprint.data.isEmpty();
  m_actionSendFiles->setEnabled(receive);
  if (!receive) {
    m_fileReceiver->close();
    return;
  }
  m_fileReceiver->listen(filetransfer::sslConfiguration(Settings::value(Settings::Security::Certificate).toString()));
}

QList<QPair<QString, QHostAddress>> MainWindow::fileTargets() const
{
  QList<QPair<QString, QHostAddress>> targets;
  if (!m_coreProcess.isStarted())
    return targets;

  if (m_coreProcess.mode() == CoreMode::Client) {
    if (m_coreProcess.connectionState() == ConnectionState::Connected && !m_discovery->linkAddress().isNull())
      targets.append({ui->cbHostname->currentText(), m_discovery->linkAddress()});
    return targets;
  }

  // Clients show up once they said hello, which also tells us their address
  const auto connected = m_connectionsPanel->connectedNames();
  const auto peers = m_discovery->linkPeers();
  for (const auto &peer : peers) {
    if (connected.contains(peer.name))
      targets.append({peer.name, peer.address});
  }
  std::ranges::sort(targets, [](const auto &a, const auto &b) {
    return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
  });
  return targets;
}

bool MainWindow::isTrustedComputer(const QByteArray &fingerprint) const
{
  if (fingerprint.isEmpty())
    return false;
  const Fingerprint wanted{QCryptographicHash::Sha256, fingerprint};
  for (const auto &path : {Settings::tlsTrustedServersDb(), Settings::tlsTrustedClientsDb()}) {
    FingerprintDatabase db;
    db.read(path);
    if (db.isTrusted(wanted))
      return true;
  }
  return false;
}

void MainWindow::sendFiles(QStringList paths)
{
  const auto title = tr("Send files");
  if (!TlsUtility::isEnabled()) {
    QMessageBox::information(this, title, tr("Turn on TLS encryption in Preferences to send files."));
    return;
  }

  const auto targets = fileTargets();
  if (targets.isEmpty()) {
    //: %1 is the app name
    QMessageBox::information(
        this, title,
        tr("Connect to another computer first. Files can be sent to computers that are connected and run this "
           "version of %1.")
            .arg(kAppName)
    );
    return;
  }

  auto target = targets.first();
  if (targets.size() > 1) {
    QStringList names;
    for (const auto &[name, address] : targets)
      names.append(name);
    bool ok = false;
    const auto chosen = QInputDialog::getItem(this, title, tr("Send to:"), names, 0, false, &ok);
    if (!ok)
      return;
    target = targets.at(names.indexOf(chosen));
  }

  if (paths.isEmpty()) {
    //: %1 is a computer name
    paths = QFileDialog::getOpenFileNames(this, tr("Choose files to send to %1").arg(target.first));
    if (paths.isEmpty())
      return;
  }

  startSending(target.first, target.second, paths, false);
}

void MainWindow::startSending(
    const QString &name, const QHostAddress &address, const QStringList &paths, bool requested
)
{
  auto sender = new FileSender(Settings::value(Settings::Core::ComputerName).toString(), paths, this, requested);
  if (const auto problem = sender->checkFiles(); !problem.isEmpty()) {
    if (requested)
      m_statusBar->showMessage(problem, 10000);
    else
      QMessageBox::warning(this, tr("Send files"), problem);
    sender->deleteLater();
    return;
  }

  connect(sender, &FileSender::waiting, this, [this, name] {
    //: %1 is a computer name
    m_statusBar->showMessage(tr("Waiting for %1 to accept the files...").arg(name));
  });
  connect(sender, &FileSender::progress, this, [this, name](qint64 sent, qint64 total) {
    //: %1 is a computer name, %2 and %3 are sizes like "3.1 MB"
    m_statusBar->showMessage(
        tr("Sending files to %1: %2 of %3").arg(name, filetransfer::sizeText(sent), filetransfer::sizeText(total))
    );
  });
  connect(sender, &FileSender::finished, this, [this, sender, name, requested](bool ok, const QString &error) {
    sender->deleteLater();
    if (ok) {
      //: %1 is a computer name
      const auto text = tr("%n file(s) sent to %1", "", sender->fileCount()).arg(name);
      m_statusBar->showMessage(text, 10000);
      // The person who asked for them is at the other computer
      if (!requested)
        notify(tr("Files sent"), text);
    } else {
      //: %1 is a computer name, %2 the reason
      const auto text = tr("Could not send files to %1: %2").arg(name, error);
      m_statusBar->showMessage(text, 15000);
      notify(tr("Files not sent"), text);
    }
  });

  //: %1 is a computer name
  m_statusBar->showMessage(tr("Connecting to %1...").arg(name));
  sender->start(
      address, filetransfer::sslConfiguration(Settings::value(Settings::Security::Certificate).toString()),
      [this](const QByteArray &fingerprint) { return isTrustedComputer(fingerprint); }
  );
}

void MainWindow::sendRequest(const QString &type, int count, const QHostAddress &skip)
{
  if (!TlsUtility::isEnabled())
    return;
  const auto request =
      filetransfer::encodeRequest(type, Settings::value(Settings::Core::ComputerName).toString(), count);
  const auto config = filetransfer::sslConfiguration(Settings::value(Settings::Security::Certificate).toString());
  const auto targets = fileTargets();
  for (const auto &target : targets) {
    if (target.second == skip)
      continue;
    auto message = new PeerMessage(request, this);
    connect(message, &PeerMessage::finished, message, &QObject::deleteLater);
    message->send(target.second, config, [this](const QByteArray &fingerprint) {
      return isTrustedComputer(fingerprint);
    });
  }
}

QString MainWindow::handlePeerRequest(const PeerRequest &request)
{
  // Only computers already accepted for keyboard and mouse sharing may ask anything
  if (!isTrustedComputer(request.fingerprint)) {
    qWarning().noquote() << "ignored a request from an unknown computer at" << request.address.toString();
    return QStringLiteral("decline");
  }
  const auto from = request.from.isEmpty() ? request.address.toString() : request.from;

  if (request.type == filetransfer::kRequestLock) {
    if (!m_actionLockTogether->isChecked())
      return QStringLiteral("decline");
    qInfo().noquote() << from << "was locked, locking this computer too";
    m_screenLock->lock();
    // The server passes it on, so every client locks and not only the server
    if (m_coreProcess.mode() == CoreMode::Server)
      sendRequest(filetransfer::kRequestLock, 0, request.address);
    return QStringLiteral("accept");
  }

  if (request.type == filetransfer::kRequestCopied) {
    m_remoteCopy.name = from;
    m_remoteCopy.address = request.address;
    m_remoteCopy.count = request.count;
    m_remoteCopy.age.start();
    updatePasteAction();
    notify(
        //: %1 is a computer name
        tr("Files copied on %1").arg(from),
        //: %1 is a computer name
        tr("%n file(s) can be pasted here. Click here or use Edit > Paste files copied on %1.", "", request.count)
            .arg(from)
    );
    m_notificationAction = [this] { pasteRemoteFiles(); };
    return QStringLiteral("done");
  }

  if (request.type == filetransfer::kRequestPull) {
    if (m_copiedFiles.isEmpty())
      return QStringLiteral("decline");
    startSending(from, request.address, m_copiedFiles, true);
    return QStringLiteral("accept");
  }

  return QStringLiteral("decline");
}

void MainWindow::clipboardChanged()
{
  if (m_settingClipboard)
    return;

  const auto data = QGuiApplication::clipboard()->mimeData(QClipboard::Clipboard);
  QStringList files;
  if (data && data->hasUrls()) {
    const auto urls = data->urls();
    for (const auto &url : urls) {
      // Folders can't be sent yet
      if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
        files.append(url.toLocalFile());
    }
  }

  if (files == m_copiedFiles)
    return;
  m_copiedFiles = files;
  if (!files.isEmpty() && m_coreProcess.isStarted())
    sendRequest(filetransfer::kRequestCopied, static_cast<int>(files.size()));
}

void MainWindow::updatePasteAction()
{
  // An offer from long ago is probably not what the person wants any more
  constexpr qint64 kMaxAge = 30 * 60 * 1000;
  const bool available = !m_remoteCopy.name.isEmpty() && m_remoteCopy.age.isValid() &&
                         m_remoteCopy.age.elapsed() < kMaxAge && m_coreProcess.isStarted();
  m_actionPasteFiles->setEnabled(available);
  if (available) {
    //: %1 is a computer name
    m_actionPasteFiles->setText(tr("&Paste files copied on %1").arg(m_remoteCopy.name));
  } else {
    m_actionPasteFiles->setText(tr("&Paste files copied on another computer"));
  }
  m_actionPasteFiles->setToolTip(tr("Copy files on another connected computer, then paste them here with this"));
}

void MainWindow::pasteRemoteFiles()
{
  updatePasteAction();
  if (!m_actionPasteFiles->isEnabled())
    return;

  const auto name = m_remoteCopy.name;
  m_pullFrom = m_remoteCopy.address;
  m_pullAge.start();
  //: %1 is a computer name
  m_statusBar->showMessage(tr("Asking %1 for the copied files...").arg(name));

  auto message = new PeerMessage(
      filetransfer::encodeRequest(filetransfer::kRequestPull, Settings::value(Settings::Core::ComputerName).toString()),
      this
  );
  connect(message, &PeerMessage::finished, this, [this, message, name](const QString &answer, const QString &error) {
    message->deleteLater();
    if (answer == QLatin1String("accept"))
      return;
    m_pullFrom.clear();
    if (answer == QLatin1String("decline")) {
      //: %1 is a computer name
      m_statusBar->showMessage(tr("%1 has no copied files any more").arg(name), 10000);
    } else {
      //: %1 is a computer name, %2 the reason
      m_statusBar->showMessage(tr("Could not get the files from %1: %2").arg(name, error), 15000);
    }
  });
  message->send(
      m_remoteCopy.address, filetransfer::sslConfiguration(Settings::value(Settings::Security::Certificate).toString()),
      [this](const QByteArray &fingerprint) { return isTrustedComputer(fingerprint); }
  );
}

void MainWindow::screenLocked()
{
  if (!m_actionLockTogether->isChecked() || !m_coreProcess.isStarted())
    return;
  qInfo() << "this computer was locked, locking the others";
  sendRequest(filetransfer::kRequestLock);
}

namespace {
QString downloadsFolder()
{
  const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  return downloads.isEmpty() ? QDir::homePath() : downloads;
}
} // namespace

void MainWindow::fileOffered(int id, const FileOffer &offer)
{
  const auto from = offer.from.isEmpty() ? offer.address.toString() : offer.from;
  m_offerSenders.insert(id, from);

  // Files this computer asked for a moment ago go straight to the clipboard
  constexpr qint64 kPullWindow = 60000;
  if (offer.requested && !m_pullFrom.isNull() && offer.address == m_pullFrom && m_pullAge.elapsed() < kPullWindow &&
      isTrustedComputer(offer.fingerprint)) {
    m_pullFrom.clear();
    m_pullIds.insert(id);
    m_fileReceiver->accept(id, downloadsFolder());
    return;
  }

  QStringList shown;
  for (qsizetype i = 0; i < offer.names.size() && i < 8; ++i)
    shown.append(QStringLiteral("• %1 (%2)").arg(offer.names.at(i), filetransfer::sizeText(offer.sizes.at(i))));
  if (offer.names.size() > 8)
    shown.append(tr("and %n more", "", static_cast<int>(offer.names.size() - 8)));

  auto box = new QMessageBox(this);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowTitle(tr("Receive files"));
  box->setIcon(QMessageBox::Question);
  //: %1 is a computer name, %2 a size like "3.1 MB"
  box->setText(tr("%1 wants to send you %n file(s), %2 in total.", "", static_cast<int>(offer.names.size()))
                   .arg(from, filetransfer::sizeText(offer.totalSize())));

  auto details = shown.join(QLatin1Char('\n'));
  if (!isTrustedComputer(offer.fingerprint)) {
    box->setIcon(QMessageBox::Warning);
    const auto code = pairingCode(offer.fingerprint);
    details += QStringLiteral("\n\n");
    if (code.isEmpty()) {
      details += tr("The sending computer could not be identified. Only accept if you expected these files.");
    } else {
      //: %1 is a pairing code like "482 913 077 251"
      details += tr("This is not a computer you have accepted before. Its pairing code is %1, only accept if "
                    "the sending computer shows the same code.")
                     .arg(code);
    }
  }
  box->setInformativeText(details);

  const auto folder = downloadsFolder();
  auto accept = box->addButton(tr("Save to Downloads"), QMessageBox::AcceptRole);
  box->addButton(tr("Decline"), QMessageBox::RejectRole);
  box->setDefaultButton(qobject_cast<QPushButton *>(box->buttons().last()));

  connect(box, &QMessageBox::buttonClicked, this, [this, id, accept, folder](QAbstractButton *button) {
    if (button == accept) {
      m_fileReceiver->accept(id, folder);
    } else {
      m_offerSenders.remove(id);
      m_fileReceiver->decline(id);
    }
  });
  // An offer that times out on the other side closes the question too
  connect(m_fileReceiver, &FileReceiver::finished, box, [box, id](int finishedId) {
    if (finishedId == id)
      box->close();
  });

  notify(tr("Receive files"), box->text());
  box->open();
  box->raise();
  box->activateWindow();
}

void MainWindow::filesReceived(int id, bool ok, const QString &error, const QStringList &files)
{
  const auto from = m_offerSenders.take(id);
  if (m_pullIds.remove(id) && ok) {
    // Put them on the clipboard as files, ready to paste in Explorer, Finder or a file manager
    auto data = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnomeList = "copy";
    for (const auto &file : files) {
      urls.append(QUrl::fromLocalFile(file));
      gnomeList += '\n' + QUrl::fromLocalFile(file).toEncoded();
    }
    data->setUrls(urls);
    data->setData(QStringLiteral("x-special/gnome-copied-files"), gnomeList);
    m_settingClipboard = true;
    QGuiApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    m_settingClipboard = false;
    m_copiedFiles = files;

    //: %1 is a computer name
    const auto text = tr("%n file(s) from %1 are ready to paste", "", static_cast<int>(files.size())).arg(from);
    m_statusBar->showMessage(text, 15000);
    notify(tr("Files received"), text, files.isEmpty() ? QString() : QFileInfo(files.first()).absolutePath());
    return;
  }
  if (ok) {
    const auto folder = files.isEmpty() ? QString() : QFileInfo(files.first()).absolutePath();
    //: %1 is a computer name, %2 a folder
    const auto text = tr("%n file(s) from %1 saved in %2", "", static_cast<int>(files.size()))
                          .arg(from, QDir::toNativeSeparators(folder));
    m_statusBar->showMessage(text, 15000);
    notify(tr("Files received"), text, folder);
  } else {
    //: %1 is a computer name, %2 the reason
    const auto text = tr("Receiving files from %1 failed: %2").arg(from, error);
    m_statusBar->showMessage(text, 15000);
    notify(tr("Files not received"), text);
  }
}

void MainWindow::notify(const QString &title, const QString &text, const QString &folder)
{
  m_notifiedFolder = folder;
  m_notificationAction = nullptr;
  if (m_trayIcon->isVisible() && QSystemTrayIcon::supportsMessages())
    m_trayIcon->showMessage(title, text, QSystemTrayIcon::Information, 8000);
}

void MainWindow::checkForUpdates(bool manual)
{
  m_manualUpdateCheck = m_manualUpdateCheck || manual;
  if (manual)
    m_statusBar->showMessage(tr("Checking for updates..."));
  m_updater->check(Settings::value(Settings::Gui::UpdateTestBuilds).toBool());
}

void MainWindow::updateAvailable(const UpdateInfo &update)
{
  const bool isNew = !m_update || m_update->version != update.version;
  m_update = std::make_unique<UpdateInfo>(update);
  m_statusBar->updateFound(update.version.toString());

  if (std::exchange(m_manualUpdateCheck, false)) {
    m_statusBar->clearMessage();
    showUpdate();
  } else if (isNew) {
    //: %1 is a version number, %2 the app name
    notify(tr("Update available"), tr("Version %1 of %2 is ready to install").arg(update.version.toString(), kAppName));
  }
}

void MainWindow::showUpdate()
{
  if (!m_update) {
    checkForUpdates(true);
    return;
  }
  if (m_updater->isDownloading()) {
    if (m_updateProgress)
      m_updateProgress->raise();
    return;
  }

  const auto update = *m_update;
  QMessageBox box(this);
  box.setWindowTitle(tr("Update"));
  box.setIcon(QMessageBox::Information);
  //: %1 is the app name, %2 the new version, %3 the version in use
  box.setText(tr("%1 %2 is available, you have %3.").arg(kAppName, update.version.toString(), kDisplayVersion));

  QStringList info;
  if (update.testBuild)
    info.append(tr("This is a test build made from the latest changes."));
  if (!update.canInstall())
    info.append(tr("It can't be installed from here, download it from the release page."));
#ifdef Q_OS_MACOS
  else
    info.append(tr("%1 restarts by itself. macOS may ask for Accessibility permission again afterwards.").arg(kAppName)
    );
#endif
  box.setInformativeText(info.join(QLatin1Char(' ')));
  if (!update.notes.trimmed().isEmpty())
    box.setDetailedText(update.notes.trimmed());

  QAbstractButton *install = nullptr;
  if (update.canInstall()) {
    //: %1 is a size like "34 MB"
    install =
        box.addButton(tr("Update now (%1)").arg(filetransfer::sizeText(update.assetSize)), QMessageBox::AcceptRole);
  }
  auto page = box.addButton(tr("Release page"), QMessageBox::ActionRole);
  box.addButton(tr("Later"), QMessageBox::RejectRole);
  if (install)
    box.setDefaultButton(qobject_cast<QPushButton *>(install));
  box.exec();

  if (box.clickedButton() == page) {
    QDesktopServices::openUrl(update.page);
    return;
  }
  if (!install || box.clickedButton() != install)
    return;

  auto progress = new QProgressDialog(this);
  progress->setAttribute(Qt::WA_DeleteOnClose);
  progress->setWindowTitle(tr("Update"));
  //: %1 is a version number
  progress->setLabelText(tr("Downloading version %1...").arg(update.version.toString()));
  progress->setCancelButtonText(tr("Cancel"));
  progress->setAutoClose(false);
  progress->setAutoReset(false);
  progress->setMinimumDuration(0);
  progress->setRange(0, 0);
  connect(progress, &QProgressDialog::canceled, m_updater, &Updater::cancelDownload);
  connect(progress, &QProgressDialog::canceled, progress, &QWidget::close);
  m_updateProgress = progress;
  progress->show();
  m_updater->download(update);
}

void MainWindow::installUpdate(const QString &file)
{
  if (m_updateProgress) {
    m_updateProgress->setLabelText(tr("Starting the installer..."));
    m_updateProgress->setCancelButton(nullptr);
  }

  QString error;
  const auto result = Updater::install(file, &error);
  if (m_updateProgress)
    m_updateProgress->close();

  switch (result) {
  case Updater::InstallResult::Started:
    qInfo() << "quitting so the update can be installed";
    QCoreApplication::quit();
    return;
  case Updater::InstallResult::OpenedManual:
    QMessageBox::information(
        this, tr("Update"),
        //: %1 is the app name
        tr("The update was downloaded and verified. Install it from the window that opened, then start %1 again.")
            .arg(kAppName)
    );
    return;
  case Updater::InstallResult::Failed:
    //: %1 is the reason
    QMessageBox::warning(this, tr("Update"), tr("The update could not be installed: %1").arg(error));
    return;
  }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
  const auto urls = event->mimeData()->urls();
  const bool files = !urls.isEmpty() && std::ranges::all_of(urls, [](const QUrl &url) { return url.isLocalFile(); });
  if (files && m_actionSendFiles->isEnabled())
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
  QStringList paths;
  const auto urls = event->mimeData()->urls();
  for (const auto &url : urls)
    paths.append(url.toLocalFile());
  event->acceptProposedAction();
  // Leave the drop handler before any dialog opens
  QTimer::singleShot(0, this, [this, paths] { sendFiles(paths); });
}

void MainWindow::rememberClient(const LinkPeer &peer)
{
  // Only computers the core accepted, a hello alone proves nothing
  if (peer.mac.isEmpty() || !m_connectionsPanel->connectedNames().contains(peer.name))
    return;

  auto entries = Settings::value(Settings::Server::KnownClients).toStringList();
  const auto entry = QStringLiteral("%1=%2").arg(peer.mac, peer.name);
  if (entries.contains(entry))
    return;
  entries.removeIf([&peer](const QString &existing) { return existing.section(QLatin1Char('='), 1) == peer.name; });
  entries.append(entry);
  Settings::setValue(Settings::Server::KnownClients, entries);
  qInfo().noquote() << "remembered" << peer.name << "for wake on lan";
}

void MainWindow::refreshWakeMenu()
{
  m_menuWake->clear();

  // Name and MAC of every computer we could wake: the remembered server and the clients seen
  QList<QPair<QString, QString>> targets;
  const auto serverMac = Settings::value(Settings::Client::WakeMac).toString();
  if (!serverMac.isEmpty())
    targets.append({Settings::value(Settings::Client::WakeHost).toString(), serverMac});
  const auto clients = Settings::value(Settings::Server::KnownClients).toStringList();
  for (const auto &entry : clients) {
    const auto mac = entry.section(QLatin1Char('='), 0, 0);
    const auto name = entry.section(QLatin1Char('='), 1);
    if (!name.isEmpty() && !ServerDiscovery::normalizedMac(mac).isEmpty())
      targets.append({name, mac});
  }

  if (targets.isEmpty()) {
    auto none = m_menuWake->addAction(tr("No computers to wake yet"));
    none->setEnabled(false);
    none->setToolTip(tr("Computers are added here once they have been connected"));
    return;
  }

  const auto connected = m_connectionsPanel ? m_connectionsPanel->connectedNames() : QStringList();
  for (const auto &[name, mac] : std::as_const(targets)) {
    const bool online = connected.contains(name);
    //: %1 is a computer name
    auto action = m_menuWake->addAction(online ? tr("%1 (connected)").arg(name) : name);
    action->setEnabled(!online);
    action->setToolTip(mac);
    connect(action, &QAction::triggered, this, [this, name, mac] { wakeComputer(name, mac); });
  }

  m_menuWake->addSeparator();
  auto forget = m_menuWake->addAction(tr("&Forget these computers"));
  connect(forget, &QAction::triggered, this, [] {
    Settings::setValue(Settings::Server::KnownClients, QStringList());
    Settings::setValue(Settings::Client::WakeMac, QString());
    Settings::setValue(Settings::Client::WakeHost, QString());
  });
}

void MainWindow::wakeComputer(const QString &name, const QString &mac)
{
  if (WakeOnLan::wake(mac)) {
    //: %1 is a computer name or address
    m_statusBar->showMessage(tr("Wake-up signal sent to %1, it may take a minute to start").arg(name), 10000);
  } else {
    m_statusBar->showMessage(tr("Could not send the wake-up signal"), 8000);
  }
}

void MainWindow::findServers(bool automatic)
{
  if (m_discovery->isScanning())
    return;

  m_automaticDiscovery = automatic;
  if (!automatic) {
    ui->btnFindServers->setEnabled(false);
    ui->btnFindServers->setText(tr("Searching..."));
  }
  m_discovery->scan();
}

void MainWindow::serversFound(const QList<DiscoveredServer> &servers)
{
  ui->btnFindServers->setEnabled(true);
  ui->btnFindServers->setText(tr("&Find"));

  const auto defaultPort = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());

  if (m_automaticDiscovery) {
    m_automaticDiscovery = false;
    // Only fill in the address when the choice is obvious and the user hasn't typed anything meanwhile
    if (servers.size() == 1 && m_coreProcess.mode() == CoreMode::Client && ui->cbHostname->currentText().isEmpty()) {
      ui->cbHostname->setCurrentText(servers.first().host(defaultPort));
      rememberServer(servers.first());
      //: %1 is the name of the server computer
      m_statusBar->showMessage(tr("Found the server \"%1\" on your network").arg(servers.first().name), 8000);
    }
    return;
  }

  QMenu menu(this);

  // A server that doesn't answer may be asleep, offer to wake the one we know
  const auto wakeHost = Settings::value(Settings::Client::WakeHost).toString();
  const auto wakeMac = Settings::value(Settings::Client::WakeMac).toString();
  const bool canWake = !wakeMac.isEmpty() && !wakeHost.isEmpty() &&
                       std::ranges::none_of(servers, [&wakeHost, defaultPort](const DiscoveredServer &server) {
                         return server.host(defaultPort) == wakeHost;
                       });

  if (servers.isEmpty()) {
    menu.addAction(tr("No servers found"))->setEnabled(false);
    menu.addAction(tr("Make sure the server is running and can be found on the network"))->setEnabled(false);
  } else {
    for (const auto &server : servers) {
      const auto host = server.host(defaultPort);
      //: %1 is a computer name, %2 its address
      auto action = menu.addAction(tr("%1  (%2)").arg(server.name, host));
      const auto icon = server.tls ? QStringLiteral("security-high") : QStringLiteral("network-connect");
      action->setIcon(QIcon::fromTheme(icon));
      connect(action, &QAction::triggered, this, [this, host, server] {
        ui->cbHostname->setCurrentText(host);
        rememberServer(server);
      });
    }
  }

  if (canWake) {
    menu.addSeparator();
    //: %1 is the server's address
    auto wake = menu.addAction(tr("Wake up %1").arg(wakeHost));
    wake->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
    connect(wake, &QAction::triggered, this, &MainWindow::wakeServer);
  }

  menu.exec(ui->btnFindServers->mapToGlobal(QPoint(0, ui->btnFindServers->height())));
}

void MainWindow::loadModeFromSettings()
{
  const auto mode = Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>();
  const QSignalBlocker serverBlocker(ui->rbModeServer);
  const QSignalBlocker clientBlocker(ui->rbModeClient);
  ui->rbModeServer->setAutoExclusive(false);
  ui->rbModeClient->setAutoExclusive(false);
  ui->rbModeServer->setChecked(mode == Settings::CoreMode::Server);
  ui->rbModeClient->setChecked(mode == Settings::CoreMode::Client);
  ui->rbModeServer->setAutoExclusive(true);
  ui->rbModeClient->setAutoExclusive(true);
}

void MainWindow::rememberServer(const DiscoveredServer &server)
{
  if (server.mac.isEmpty())
    return;
  const auto defaultPort = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
  Settings::setValue(Settings::Client::WakeHost, server.host(defaultPort));
  Settings::setValue(Settings::Client::WakeMac, server.mac);
}

void MainWindow::wakeServer()
{
  wakeComputer(
      Settings::value(Settings::Client::WakeHost).toString(), Settings::value(Settings::Client::WakeMac).toString()
  );
}

void MainWindow::watchSystem()
{
  const auto now = QDateTime::currentMSecsSinceEpoch();
  // The timer doesn't run while the computer sleeps, a long gap between two checks means it woke up
  const bool resumed = m_lastWatch != 0 && now - m_lastWatch > kWatchInterval + kSleepGap;
  m_lastWatch = now;

  const auto networks = NetworkMonitor::localNetworks();
  const bool firstCheck = !m_watchStarted;
  const bool networkChanged = !firstCheck && networks != m_lastNetworks;
  m_watchStarted = true;
  m_lastNetworks = networks;

  // Use the profile linked to the network we just joined, or started on
  if (firstCheck || networkChanged) {
    if (const auto profile = Profiles::profileForNetworks(networks);
        !profile.isEmpty() && profile != Profiles::active()) {
      switchProfile(profile);
      //: %1 is the name of a profile
      m_statusBar->showMessage(tr("Switched to the \"%1\" profile for this network").arg(Profiles::active()), 8000);
      return;
    }
  }

  // A client keeps a dead connection after sleep or a network change, start it again
  if ((resumed || networkChanged) && !networks.isEmpty() && m_coreProcess.mode() == CoreMode::Client &&
      m_coreProcess.isStarted()) {
    qInfo().noquote() << (resumed ? "computer woke up" : "network changed") << "reconnecting to the server";
    m_statusBar->showMessage(
        resumed ? tr("Reconnecting after sleep...") : tr("Network changed, reconnecting..."), 8000
    );
    m_coreProcess.restart();
  }

  m_connectionsPanel->refresh();
}

void MainWindow::maybeShowOnboarding()
{
  if (Settings::value(Settings::Gui::OnboardingDone).toBool())
    return;

  // People who already set Deskflow up don't need the wizard
  if (Settings::value(Settings::Core::CoreMode).value<Settings::CoreMode>() != Settings::CoreMode::None) {
    Settings::setValue(Settings::Gui::OnboardingDone, true);
    return;
  }

  QTimer::singleShot(0, this, &MainWindow::showOnboarding);
}

void MainWindow::showOnboarding()
{
  OnboardingDialog dialog(this);
  const bool accepted = dialog.exec() == QDialog::Accepted;
  Settings::setValue(Settings::Gui::OnboardingDone, true);
  if (!accepted || dialog.mode() == Settings::CoreMode::None)
    return;

  if (m_coreProcess.isStarted())
    stopCore();

  Settings::setValue(Settings::Core::CoreMode, dialog.mode());
  if (dialog.mode() == Settings::CoreMode::Client) {
    Settings::setValue(Settings::Client::RemoteHost, dialog.serverHost());
    if (const auto mac = dialog.serverMac(); !mac.isEmpty()) {
      Settings::setValue(Settings::Client::WakeHost, dialog.serverHost());
      Settings::setValue(Settings::Client::WakeMac, mac);
    }
  }
  Settings::save();

  loadModeFromSettings();
  applyConfig();

  if (dialog.startNow())
    startCore();
}
