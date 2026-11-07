/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Named connection profiles (e.g. "Home", "Office").
 *
 * A profile holds everything that describes *where* and *how* this computer
 * connects: server or client mode, the remote host, the port and interface,
 * the server options, the computer layout and the hotkeys.
 *
 * Settings that belong to the machine or the user (computer name, language,
 * TLS certificate, logging, window and tray preferences) are shared by all
 * profiles and never change when switching.
 *
 * The live settings file is always the source of truth for the active
 * profile. Each profile is stored as its own INI file in a "profiles" folder
 * next to the settings file; the active profile's file is refreshed from the
 * live settings whenever another profile is created or switched to.
 */
class Profiles
{
public:
  inline static const auto DefaultName = QStringLiteral("Default");
  inline static const auto FileExtension = QStringLiteral("conf");
  static constexpr int MaxNameLength = 64;

  /**
   * @brief Folder where profile files are stored
   */
  static QString profilesDir();

  /**
   * @brief Full path of the file that stores the named profile
   */
  static QString profileFile(const QString &name);

  /**
   * @brief Names of all saved profiles, sorted case-insensitively
   */
  static QStringList list();

  /**
   * @brief Name of the active profile (empty before ensureInitialized)
   */
  static QString active();

  /**
   * @brief Check if a profile name exists (case-insensitive)
   */
  static bool exists(const QString &name);

  /**
   * @brief Check a name can be used as a profile name and a file name on every platform
   */
  static bool isValidName(const QString &name);

  /**
   * @brief Check if a settings key belongs to a profile rather than to the whole app
   */
  static bool isProfileKey(const QString &key);

  /**
   * @brief Make sure at least one profile exists and one is active.
   * On first run the current settings become the "Default" profile.
   */
  static void ensureInitialized();

  /**
   * @brief Store the live settings into the active profile's file
   */
  static bool saveActive();

  /**
   * @brief Create a new profile
   * @param name the new profile's name
   * @param copyCurrent when true the profile starts as a copy of the live settings,
   *        otherwise it starts empty, which means all profile settings are defaults
   * @return false if the name is invalid, already used or the file can't be written
   */
  static bool create(const QString &name, bool copyCurrent);

  /**
   * @brief Make another profile active, saving the current one first.
   * Only profile keys of the live settings are replaced.
   * @return false if the profile doesn't exist or couldn't be read
   */
  static bool switchTo(const QString &name);

  /**
   * @brief Rename a profile, keeping it active if it was
   */
  static bool rename(const QString &oldName, const QString &newName);

  /**
   * @brief Delete a profile. The active profile and the last remaining profile can't be deleted.
   */
  static bool remove(const QString &name);

  /**
   * @brief Networks (e.g. "192.168.1.0/24") that make a profile active automatically
   */
  static QStringList networks(const QString &name);

  /**
   * @brief Set the networks of a profile. A network belongs to one profile only,
   * so it is removed from any other profile that had it.
   */
  static bool setNetworks(const QString &name, const QStringList &networks);

  /**
   * @brief The profile linked to one of the given networks, or an empty string
   */
  static QString profileForNetworks(const QStringList &currentNetworks);

private:
  static QString canonicalName(const QString &name);
  static bool writeSnapshot(const QString &fileName);
};
