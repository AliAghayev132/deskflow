/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Profiles.h"

#include "Settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace {

// Keys under these prefixes describe a connection setup and belong to a profile.
const QStringList kProfilePrefixes = {
    QStringLiteral("client/"), QStringLiteral("server/"), QStringLiteral("internalConfig/"), QStringLiteral("computer_")
};

// Single keys outside the prefixes above that also belong to a profile.
const QStringList kProfileKeys = {Settings::Core::CoreMode, Settings::Core::Port, Settings::Core::Interface};

// Details stored in a profile file that describe the profile, not settings to apply
const auto kMetaPrefix = QStringLiteral("profile/");
const auto kNetworksKey = QStringLiteral("profile/networks");

// Keys that match a profile prefix but are per-machine state, never part of a profile.
const QStringList kExcludedKeys = {
    Settings::Client::XdpClipboardRetried, Settings::Client::XdpRestoreToken, Settings::Server::XdpClipboardRetried,
    Settings::Server::XdpRestoreToken
};

} // namespace

QString Profiles::profilesDir()
{
  return QStringLiteral("%1/profiles").arg(QFileInfo(Settings::settingsFile()).absolutePath());
}

QString Profiles::profileFile(const QString &name)
{
  return QStringLiteral("%1/%2.%3").arg(profilesDir(), name, FileExtension);
}

QStringList Profiles::list()
{
  const QDir dir(profilesDir());
  QStringList names;
  const auto files = dir.entryInfoList({QStringLiteral("*.%1").arg(FileExtension)}, QDir::Files);
  for (const auto &file : files) {
    const auto name = file.completeBaseName();
    if (isValidName(name))
      names.append(name);
  }
  std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  return names;
}

QString Profiles::active()
{
  return Settings::value(Settings::Gui::ActiveProfile).toString();
}

QString Profiles::canonicalName(const QString &name)
{
  // File systems on Windows and macOS are case-insensitive, so are profile names.
  const auto names = list();
  for (const auto &existing : names) {
    if (existing.compare(name, Qt::CaseInsensitive) == 0)
      return existing;
  }
  return {};
}

bool Profiles::exists(const QString &name)
{
  return !canonicalName(name).isEmpty();
}

bool Profiles::isValidName(const QString &name)
{
  // Letters (any language), digits, spaces, '-', '_' and '.'; must start and end with a letter or digit,
  // so the name is also a valid file name on Windows, macOS and Linux.
  static const auto nameRegex = QRegularExpression(
      QStringLiteral(R"(^[\p{L}\p{N}](?:[\p{L}\p{N} _.\-]*[\p{L}\p{N}])?$)"),
      QRegularExpression::UseUnicodePropertiesOption
  );

  if (name.isEmpty() || name.length() > MaxNameLength)
    return false;

  // Names reserved for devices on Windows
  static const auto reservedRegex = QRegularExpression(
      QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM\d|LPT\d)(\..*)?$)"), QRegularExpression::CaseInsensitiveOption
  );
  if (reservedRegex.match(name).hasMatch())
    return false;

  return nameRegex.match(name).hasMatch();
}

bool Profiles::isProfileKey(const QString &key)
{
  if (kExcludedKeys.contains(key))
    return false;
  if (kProfileKeys.contains(key))
    return true;
  return std::any_of(kProfilePrefixes.cbegin(), kProfilePrefixes.cend(), [&key](const QString &prefix) {
    return key.startsWith(prefix);
  });
}

bool Profiles::writeSnapshot(const QString &fileName)
{
  if (!QDir().mkpath(QFileInfo(fileName).absolutePath())) {
    qWarning().noquote() << "unable to create profiles folder:" << profilesDir();
    return false;
  }

  QSettings profile(fileName, QSettings::IniFormat);

  // Details about the profile itself (e.g. its networks) are not settings, keep them
  QMap<QString, QVariant> meta;
  const auto existingKeys = profile.allKeys();
  for (const auto &key : existingKeys) {
    if (key.startsWith(kMetaPrefix))
      meta.insert(key, profile.value(key));
  }

  profile.clear();
  for (const auto [key, value] : meta.asKeyValueRange())
    profile.setValue(key, value);
  const auto values = Settings::values(&Profiles::isProfileKey);
  for (const auto [key, value] : values.asKeyValueRange())
    profile.setValue(key, value);
  profile.sync();

  if (profile.status() != QSettings::NoError) {
    qWarning().noquote() << "unable to write profile:" << fileName;
    return false;
  }

  // QSettings writes nothing when there are no values, but an empty profile still needs its file
  if (!QFile::exists(fileName)) {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
      qWarning().noquote() << "unable to write profile:" << fileName;
      return false;
    }
  }
  return true;
}

void Profiles::ensureInitialized()
{
  auto name = active();
  if (!name.isEmpty() && exists(name)) {
    // Fix the stored name's case if it differs from the file on disk
    if (const auto canonical = canonicalName(name); canonical != name)
      Settings::setValue(Settings::Gui::ActiveProfile, canonical);
    return;
  }

  // Either the first run with profiles, or the active profile's file is gone:
  // the live settings are kept and become that profile.
  if (!isValidName(name))
    name = DefaultName;

  if (!exists(name) && !writeSnapshot(profileFile(name)))
    return;

  Settings::setValue(Settings::Gui::ActiveProfile, canonicalName(name));
  qInfo().noquote() << "active profile:" << active();
}

bool Profiles::saveActive()
{
  const auto name = canonicalName(active());
  if (name.isEmpty())
    return false;
  return writeSnapshot(profileFile(name));
}

bool Profiles::create(const QString &name, bool copyCurrent)
{
  if (!isValidName(name) || exists(name))
    return false;

  if (copyCurrent)
    return writeSnapshot(profileFile(name));

  if (!QDir().mkpath(profilesDir()))
    return false;

  // An empty profile file means "all defaults"
  QFile file(profileFile(name));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  file.close();
  return true;
}

bool Profiles::switchTo(const QString &name)
{
  const auto target = canonicalName(name);
  if (target.isEmpty()) {
    qWarning().noquote() << "profile not found:" << name;
    return false;
  }

  // Keep the profile we are leaving up to date
  if (target != active())
    saveActive();

  QSettings profile(profileFile(target), QSettings::IniFormat);
  if (profile.status() != QSettings::NoError) {
    qWarning().noquote() << "unable to read profile:" << target;
    return false;
  }

  QMap<QString, QVariant> values;
  const auto keys = profile.allKeys();
  for (const auto &key : keys) {
    if (isProfileKey(key))
      values.insert(key, profile.value(key));
  }

  Settings::replaceValues(&Profiles::isProfileKey, values);
  Settings::setValue(Settings::Gui::ActiveProfile, target);
  qInfo().noquote() << "switched to profile:" << target;
  return true;
}

bool Profiles::rename(const QString &oldName, const QString &newName)
{
  const auto source = canonicalName(oldName);
  if (source.isEmpty() || !isValidName(newName))
    return false;

  // Allow changing only the case of a name
  const bool caseOnly = source.compare(newName, Qt::CaseInsensitive) == 0;
  if (!caseOnly && exists(newName))
    return false;
  if (source == newName)
    return true;

  const bool wasActive = source == active();
  if (wasActive)
    saveActive();

  bool renamed;
  if (caseOnly) {
    // Case-insensitive file systems need a two step rename
    const auto tempFile = profileFile(QStringLiteral("%1.renaming").arg(source));
    renamed = QFile::rename(profileFile(source), tempFile) && QFile::rename(tempFile, profileFile(newName));
  } else {
    renamed = QFile::rename(profileFile(source), profileFile(newName));
  }

  if (!renamed) {
    qWarning().noquote() << "unable to rename profile:" << source << "to" << newName;
    return false;
  }

  if (wasActive)
    Settings::setValue(Settings::Gui::ActiveProfile, newName);
  return true;
}

bool Profiles::remove(const QString &name)
{
  const auto target = canonicalName(name);
  if (target.isEmpty() || target == active() || list().size() <= 1)
    return false;
  return QFile::remove(profileFile(target));
}

QStringList Profiles::networks(const QString &name)
{
  const auto target = canonicalName(name);
  if (target.isEmpty())
    return {};
  const QSettings profile(profileFile(target), QSettings::IniFormat);
  return profile.value(kNetworksKey).toStringList();
}

bool Profiles::setNetworks(const QString &name, const QStringList &networks)
{
  const auto target = canonicalName(name);
  if (target.isEmpty())
    return false;

  auto unique = networks;
  unique.removeDuplicates();
  unique.removeAll(QString());

  // A network can only switch to one profile, take it away from the others
  const auto names = list();
  for (const auto &other : names) {
    if (other == target)
      continue;
    QSettings profile(profileFile(other), QSettings::IniFormat);
    auto otherNetworks = profile.value(kNetworksKey).toStringList();
    const auto before = otherNetworks.size();
    otherNetworks.removeIf([&unique](const QString &network) { return unique.contains(network); });
    if (otherNetworks.size() == before)
      continue;
    if (otherNetworks.isEmpty())
      profile.remove(kNetworksKey);
    else
      profile.setValue(kNetworksKey, otherNetworks);
    profile.sync();
  }

  QSettings profile(profileFile(target), QSettings::IniFormat);
  if (unique.isEmpty())
    profile.remove(kNetworksKey);
  else
    profile.setValue(kNetworksKey, unique);
  profile.sync();
  return profile.status() == QSettings::NoError;
}

QString Profiles::profileForNetworks(const QStringList &currentNetworks)
{
  if (currentNetworks.isEmpty())
    return {};

  const auto names = list();
  for (const auto &name : names) {
    const auto profileNetworks = networks(name);
    for (const auto &network : profileNetworks) {
      if (currentNetworks.contains(network))
        return name;
    }
  }
  return {};
}
