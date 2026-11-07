/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OnboardingDialog.h"

#include "common/Constants.h"
#include "gui/Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using deskflow::gui::DiscoveredServer;
using deskflow::gui::ServerDiscovery;

namespace {
enum Page
{
  ModePage,
  ServerPage,
  ClientPage
};

QLabel *makeLabel(const QString &text, const QString &role, QWidget *parent)
{
  auto label = new QLabel(text, parent);
  label->setWordWrap(true);
  if (!role.isEmpty())
    label->setProperty("role", role);
  return label;
}

QRadioButton *makeModeCard(const QString &text, const QIcon &icon, QWidget *parent)
{
  auto card = new QRadioButton(text, parent);
  card->setProperty("card", true);
  card->setIcon(icon);
  card->setIconSize(QSize(40, 40));
  card->setCursor(Qt::PointingHandCursor);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return card;
}
} // namespace

OnboardingDialog::OnboardingDialog(QWidget *parent)
    : QDialog(parent),
      m_pages{new QStackedWidget(this)},
      m_discovery{new ServerDiscovery(this)}
{
  setWindowTitle(tr("Set up %1").arg(kAppName));
  setMinimumWidth(620);

  // Page 1: how is this computer used?
  auto modePage = new QWidget(this);
  {
    auto layout = new QVBoxLayout(modePage);
    layout->setSpacing(10);
    layout->addWidget(makeLabel(tr("Welcome to %1").arg(kAppName), QStringLiteral("title"), modePage));
    layout->addWidget(makeLabel(
        tr("Use one keyboard and mouse across your computers. How will this computer be used?"), QStringLiteral("hint"),
        modePage
    ));
    layout->addSpacing(6);
    m_rbServer = makeModeCard(
        tr("Use this computer's keyboard and mouse\n(make this computer the server)"),
        deskflow::gui::theme::serverModeIcon(), modePage
    );
    m_rbClient = makeModeCard(
        tr("Use another computer’s mouse and keyboard\n(make this computer the client)"),
        deskflow::gui::theme::clientModeIcon(), modePage
    );
    auto group = new QButtonGroup(modePage);
    group->addButton(m_rbServer);
    group->addButton(m_rbClient);
    layout->addWidget(m_rbServer);
    layout->addWidget(m_rbClient);
    layout->addStretch();
    connect(group, &QButtonGroup::buttonToggled, this, &OnboardingDialog::updateButtons);
  }

  // Page 2a: server
  m_serverPage = new QWidget(this);
  {
    auto layout = new QVBoxLayout(m_serverPage);
    layout->setSpacing(10);
    layout->addWidget(makeLabel(tr("This computer is the server"), QStringLiteral("title"), m_serverPage));
    layout->addWidget(makeLabel(tr("1. Install %1 on your other computers.").arg(kAppName), QString(), m_serverPage));
    layout->addWidget(makeLabel(
        tr("2. On each of them choose \"client\". They find this computer on the network by themselves."), QString(),
        m_serverPage
    ));
    layout->addWidget(makeLabel(
        tr("3. When they connect, use Configure Server to drag them where they are on your desk."), QString(),
        m_serverPage
    ));
    layout->addSpacing(6);
    m_cbStartServer = new QCheckBox(tr("Start sharing now"), m_serverPage);
    m_cbStartServer->setChecked(true);
    layout->addWidget(m_cbStartServer);
    layout->addStretch();
  }

  // Page 2b: client
  m_clientPage = new QWidget(this);
  {
    auto layout = new QVBoxLayout(m_clientPage);
    layout->setSpacing(8);
    layout->addWidget(makeLabel(tr("Which computer shares its keyboard?"), QStringLiteral("title"), m_clientPage));
    layout->addWidget(makeLabel(
        tr("Make sure %1 is running as the server on that computer.").arg(kAppName), QStringLiteral("hint"),
        m_clientPage
    ));

    m_serverList = new QListWidget(m_clientPage);
    m_serverList->setMinimumHeight(110);
    layout->addWidget(m_serverList);

    auto scanRow = new QHBoxLayout();
    m_scanStatus = makeLabel(QString(), QStringLiteral("hint"), m_clientPage);
    m_btnScan = new QPushButton(tr("Search again"), m_clientPage);
    m_btnScan->setIcon(QIcon::fromTheme(QStringLiteral("system-search")));
    scanRow->addWidget(m_scanStatus, 1);
    scanRow->addWidget(m_btnScan);
    layout->addLayout(scanRow);

    layout->addWidget(makeLabel(tr("Or type its name or IP address:"), QString(), m_clientPage));
    m_host = new QLineEdit(m_clientPage);
    m_host->setPlaceholderText(tr("e.g. 192.168.1.10"));
    layout->addWidget(m_host);

    m_cbConnect = new QCheckBox(tr("Connect now"), m_clientPage);
    m_cbConnect->setChecked(true);
    layout->addWidget(m_cbConnect);

    connect(m_btnScan, &QPushButton::clicked, this, &OnboardingDialog::scan);
    connect(m_serverList, &QListWidget::itemSelectionChanged, this, &OnboardingDialog::selectServer);
    connect(m_host, &QLineEdit::textChanged, this, &OnboardingDialog::updateButtons);
    connect(m_discovery, &ServerDiscovery::scanFinished, this, &OnboardingDialog::serversFound);
  }

  m_pages->addWidget(modePage);
  m_pages->addWidget(m_serverPage);
  m_pages->addWidget(m_clientPage);

  // Buttons
  auto skip = new QPushButton(tr("Skip"), this);
  m_btnBack = new QPushButton(tr("Back"), this);
  m_btnNext = new QPushButton(this);
  m_btnNext->setProperty("primary", true);
  m_btnNext->setDefault(true);

  auto buttons = new QHBoxLayout();
  buttons->addWidget(skip);
  buttons->addStretch();
  buttons->addWidget(m_btnBack);
  buttons->addWidget(m_btnNext);

  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 18, 20, 16);
  layout->addWidget(m_pages, 1);
  layout->addLayout(buttons);

  connect(skip, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_btnBack, &QPushButton::clicked, this, &OnboardingDialog::showModePage);
  connect(m_btnNext, &QPushButton::clicked, this, [this] {
    if (m_pages->currentIndex() == ModePage)
      showDetailsPage();
    else
      accept();
  });

  showModePage();
}

Settings::CoreMode OnboardingDialog::mode() const
{
  if (m_rbServer->isChecked())
    return Settings::CoreMode::Server;
  if (m_rbClient->isChecked())
    return Settings::CoreMode::Client;
  return Settings::CoreMode::None;
}

QString OnboardingDialog::serverHost() const
{
  return m_host->text().trimmed();
}

QString OnboardingDialog::serverMac() const
{
  const auto host = serverHost();
  const auto port = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
  for (const auto &server : m_servers) {
    if (server.host(port) == host)
      return server.mac;
  }
  return {};
}

bool OnboardingDialog::startNow() const
{
  return mode() == Settings::CoreMode::Server ? m_cbStartServer->isChecked() : m_cbConnect->isChecked();
}

void OnboardingDialog::showModePage()
{
  m_pages->setCurrentIndex(ModePage);
  updateButtons();
}

void OnboardingDialog::showDetailsPage()
{
  if (mode() == Settings::CoreMode::Server) {
    m_pages->setCurrentIndex(ServerPage);
  } else {
    m_pages->setCurrentIndex(ClientPage);
    if (m_servers.isEmpty() && !m_discovery->isScanning())
      scan();
  }
  updateButtons();
}

void OnboardingDialog::updateButtons()
{
  const auto page = m_pages->currentIndex();
  m_btnBack->setVisible(page != ModePage);
  m_btnNext->setText(page == ModePage ? tr("Next") : tr("Finish"));

  bool canContinue = mode() != Settings::CoreMode::None;
  if (page == ClientPage)
    canContinue = !serverHost().isEmpty();
  m_btnNext->setEnabled(canContinue);
}

void OnboardingDialog::scan()
{
  m_btnScan->setEnabled(false);
  m_scanStatus->setText(tr("Looking for servers on your network..."));
  m_discovery->scan();
}

void OnboardingDialog::serversFound(const QList<DiscoveredServer> &servers)
{
  m_servers = servers;
  m_btnScan->setEnabled(true);
  m_serverList->clear();

  const auto port = static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
  for (const auto &server : servers) {
    //: %1 is a computer name, %2 its address
    auto item = new QListWidgetItem(tr("%1  (%2)").arg(server.name, server.host(port)), m_serverList);
    item->setData(Qt::UserRole, server.host(port));
    item->setIcon(QIcon::fromTheme(QStringLiteral("video-display")));
  }

  if (servers.isEmpty()) {
    m_scanStatus->setText(tr("No servers found yet. Start %1 as the server on the other computer.").arg(kAppName));
  } else {
    m_scanStatus->setText(tr("Found %n server(s).", "", static_cast<int>(servers.size())));
    // Pick the only choice so one click on Finish is enough
    if (servers.size() == 1 && serverHost().isEmpty())
      m_serverList->setCurrentRow(0);
  }
}

void OnboardingDialog::selectServer()
{
  const auto item = m_serverList->currentItem();
  if (item)
    m_host->setText(item->data(Qt::UserRole).toString());
}
