/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProfilesTests.h"

#include <QDir>
#include <QFile>

namespace {
const auto kHomeHost = QStringLiteral("192.168.1.10");
const auto kOfficeHost = QStringLiteral("10.0.0.25");
const auto kLayoutKey = QStringLiteral("internalConfig/screens/1/name");
const auto kComputerKey = QStringLiteral("computer_laptop/name");
} // namespace

void ProfilesTests::initTestCase()
{
  QDir(m_settingsPath).removeRecursively();
  QVERIFY(QDir().mkpath(m_settingsPath));
  Settings::setSettingsFile(m_settingsFile);
  Settings::setStateFile(m_stateFile);
}

void ProfilesTests::validNames()
{
  QVERIFY(Profiles::isValidName("Home"));
  QVERIFY(Profiles::isValidName("Ofis 2"));
  QVERIFY(Profiles::isValidName("Çayxana-Şəki_1.0"));
  QVERIFY(Profiles::isValidName("A"));

  QVERIFY(!Profiles::isValidName(""));
  QVERIFY(!Profiles::isValidName(" Home"));
  QVERIFY(!Profiles::isValidName("Home "));
  QVERIFY(!Profiles::isValidName("Home."));
  QVERIFY(!Profiles::isValidName(".hidden"));
  QVERIFY(!Profiles::isValidName("a/b"));
  QVERIFY(!Profiles::isValidName("a\\b"));
  QVERIFY(!Profiles::isValidName("a:b"));
  QVERIFY(!Profiles::isValidName("CON"));
  QVERIFY(!Profiles::isValidName("com1"));
  QVERIFY(!Profiles::isValidName(QString(Profiles::MaxNameLength + 1, 'x')));
  QVERIFY(Profiles::isValidName(QString(Profiles::MaxNameLength, 'x')));
}

void ProfilesTests::firstRunWithNoSettings()
{
  // A fresh install has no profile settings at all, the default profile must still be created
  QVERIFY(Settings::values(&Profiles::isProfileKey).isEmpty());
  Profiles::ensureInitialized();
  QCOMPARE(Profiles::active(), Profiles::DefaultName);
  QCOMPARE(Profiles::list(), QStringList{Profiles::DefaultName});

  // Start the next tests from a clean slate
  QVERIFY(QFile::remove(Profiles::profileFile(Profiles::DefaultName)));
  Settings::setValue(Settings::Gui::ActiveProfile);
  QVERIFY(Profiles::active().isEmpty());
}

void ProfilesTests::profileKeys()
{
  QVERIFY(Profiles::isProfileKey(Settings::Client::RemoteHost));
  QVERIFY(Profiles::isProfileKey(Settings::Client::RecentRemoteHosts));
  QVERIFY(Profiles::isProfileKey(Settings::Core::CoreMode));
  QVERIFY(Profiles::isProfileKey(Settings::Core::Port));
  QVERIFY(Profiles::isProfileKey(Settings::Server::GridWidth));
  QVERIFY(Profiles::isProfileKey(kLayoutKey));
  QVERIFY(Profiles::isProfileKey(kComputerKey));

  QVERIFY(!Profiles::isProfileKey(Settings::Core::ComputerName));
  QVERIFY(!Profiles::isProfileKey(Settings::Core::Language));
  QVERIFY(!Profiles::isProfileKey(Settings::Gui::ActiveProfile));
  QVERIFY(!Profiles::isProfileKey(Settings::Security::TlsEnabled));
  QVERIFY(!Profiles::isProfileKey(Settings::Log::Level));
  QVERIFY(!Profiles::isProfileKey(Settings::Client::XdpRestoreToken));
  QVERIFY(!Profiles::isProfileKey(Settings::Server::XdpRestoreToken));
}

void ProfilesTests::firstRunCreatesDefault()
{
  Settings::setValue(Settings::Client::RemoteHost, kHomeHost);
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Client);
  Settings::setValue(kLayoutKey, QStringLiteral("home-desktop"));

  QVERIFY(Profiles::list().isEmpty());
  QVERIFY(Profiles::active().isEmpty());

  Profiles::ensureInitialized();

  QCOMPARE(Profiles::list(), QStringList{Profiles::DefaultName});
  QCOMPARE(Profiles::active(), Profiles::DefaultName);
  QVERIFY(QFile::exists(Profiles::profileFile(Profiles::DefaultName)));

  // The live settings are untouched
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), kHomeHost);

  // Calling again changes nothing
  Profiles::ensureInitialized();
  QCOMPARE(Profiles::list(), QStringList{Profiles::DefaultName});
}

void ProfilesTests::createCopyOfCurrent()
{
  QVERIFY(Profiles::create("Office", true));
  QCOMPARE(Profiles::list(), (QStringList{Profiles::DefaultName, "Office"}));

  QSettings office(Profiles::profileFile("Office"), QSettings::IniFormat);
  QCOMPARE(office.value(Settings::Client::RemoteHost).toString(), kHomeHost);
  QCOMPARE(office.value(kLayoutKey).toString(), QStringLiteral("home-desktop"));
  QVERIFY(!office.contains(Settings::Core::ComputerName));
  QVERIFY(!office.contains(Settings::Gui::ActiveProfile));

  // Creating doesn't switch
  QCOMPARE(Profiles::active(), Profiles::DefaultName);
}

void ProfilesTests::createEmpty()
{
  QVERIFY(Profiles::create("Empty", false));
  QSettings empty(Profiles::profileFile("Empty"), QSettings::IniFormat);
  QVERIFY(empty.allKeys().isEmpty());
}

void ProfilesTests::createRejectsDuplicates()
{
  QVERIFY(!Profiles::create("Office", true));
  QVERIFY(!Profiles::create("office", true));
  QVERIFY(!Profiles::create("bad/name", true));
  QVERIFY(Profiles::exists("OFFICE"));
}

void ProfilesTests::switchRestoresProfileSettings()
{
  QVERIFY(Profiles::switchTo("Office"));
  QCOMPARE(Profiles::active(), QStringLiteral("Office"));

  // Make Office different from Default
  Settings::setValue(Settings::Client::RemoteHost, kOfficeHost);
  Settings::setValue(Settings::Core::CoreMode, Settings::CoreMode::Server);
  Settings::setValue(kLayoutKey, QStringLiteral("office-desktop"));
  Settings::setValue(kComputerKey, QStringLiteral("laptop"));

  QVERIFY(Profiles::switchTo(Profiles::DefaultName));
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), kHomeHost);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).toInt(), int(Settings::CoreMode::Client));
  QCOMPARE(Settings::value(kLayoutKey).toString(), QStringLiteral("home-desktop"));
  // A computer that only exists in Office is gone
  QVERIFY(!Settings::values([](const QString &key) { return key == kComputerKey; }).contains(kComputerKey));

  // Office was saved when leaving it
  QVERIFY(Profiles::switchTo("office"));
  QCOMPARE(Profiles::active(), QStringLiteral("Office"));
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), kOfficeHost);
  QCOMPARE(Settings::value(Settings::Core::CoreMode).toInt(), int(Settings::CoreMode::Server));
  QCOMPARE(Settings::value(kLayoutKey).toString(), QStringLiteral("office-desktop"));
  QCOMPARE(Settings::value(kComputerKey).toString(), QStringLiteral("laptop"));

  QVERIFY(!Profiles::switchTo("Missing"));
  QCOMPARE(Profiles::active(), QStringLiteral("Office"));
}

void ProfilesTests::switchKeepsGlobalSettings()
{
  Settings::setValue(Settings::Core::ComputerName, "my-machine");
  Settings::setValue(Settings::Log::Level, "DEBUG");

  QVERIFY(Profiles::switchTo(Profiles::DefaultName));
  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("my-machine"));
  QCOMPARE(Settings::value(Settings::Log::Level).toString(), QStringLiteral("DEBUG"));

  QVERIFY(Profiles::switchTo("Office"));
  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("my-machine"));
}

void ProfilesTests::switchToEmptyUsesDefaults()
{
  QVERIFY(Profiles::switchTo("Empty"));
  QCOMPARE(Settings::value(Settings::Client::RemoteHost), Settings::defaultValue(Settings::Client::RemoteHost));
  QCOMPARE(Settings::value(Settings::Core::Port).toInt(), 24800);
  QVERIFY(Settings::values(&Profiles::isProfileKey).isEmpty());
  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), QStringLiteral("my-machine"));
}

void ProfilesTests::proxySeesSwitchedValues()
{
  // ServerConfig reads the layout through the settings proxy, it must see the switch
  QVERIFY(Profiles::switchTo("Office"));
  QCOMPARE(Settings::proxy().value(kLayoutKey).toString(), QStringLiteral("office-desktop"));
  QVERIFY(Profiles::switchTo(Profiles::DefaultName));
  QCOMPARE(Settings::proxy().value(kLayoutKey).toString(), QStringLiteral("home-desktop"));
}

void ProfilesTests::renameActive()
{
  QCOMPARE(Profiles::active(), Profiles::DefaultName);
  QVERIFY(Profiles::rename(Profiles::DefaultName, "Home"));
  QCOMPARE(Profiles::active(), QStringLiteral("Home"));
  QVERIFY(!Profiles::exists(Profiles::DefaultName));
  QVERIFY(Profiles::exists("Home"));

  QVERIFY(!Profiles::rename("Home", "Office"));
  QVERIFY(!Profiles::rename("Home", "bad:name"));
  QVERIFY(!Profiles::rename("Missing", "Other"));

  QVERIFY(Profiles::switchTo("Office"));
  QVERIFY(Profiles::switchTo("Home"));
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), kHomeHost);
}

void ProfilesTests::renameCaseOnly()
{
  QVERIFY(Profiles::rename("Home", "HOME"));
  QCOMPARE(Profiles::active(), QStringLiteral("HOME"));
  QVERIFY(Profiles::list().contains("HOME"));
  QVERIFY(!Profiles::list().contains("Home"));
  QVERIFY(Profiles::rename("HOME", "Home"));
}

void ProfilesTests::removeRules()
{
  QCOMPARE(Profiles::active(), QStringLiteral("Home"));
  QVERIFY(!Profiles::remove("Home")); // active
  QVERIFY(!Profiles::remove("Missing"));
  QVERIFY(Profiles::remove("empty"));
  QVERIFY(Profiles::remove("Office"));
  QCOMPARE(Profiles::list(), QStringList{"Home"});
  QVERIFY(!Profiles::remove("Home")); // last one
}

void ProfilesTests::missingActiveFileIsRecreated()
{
  QVERIFY(QFile::remove(Profiles::profileFile("Home")));
  QVERIFY(Profiles::list().isEmpty());

  Profiles::ensureInitialized();
  QCOMPARE(Profiles::active(), QStringLiteral("Home"));
  QSettings home(Profiles::profileFile("Home"), QSettings::IniFormat);
  QCOMPARE(home.value(Settings::Client::RemoteHost).toString(), kHomeHost);
}

QTEST_MAIN(ProfilesTests)
