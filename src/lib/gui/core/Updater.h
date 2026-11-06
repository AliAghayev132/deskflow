/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonArray>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace deskflow::gui {

/**
 * @brief A newer build found on the releases page
 */
struct UpdateInfo
{
  QVersionNumber version; ///< always four parts, e.g. 2.0.1.0
  QString title;          ///< name of the release
  QString notes;          ///< release notes, markdown
  QUrl page;              ///< release page in the browser
  bool testBuild = false; ///< a pre-release such as the continuous build
  QString assetName;      ///< installer for this computer, empty when there is none
  QUrl assetUrl;
  qint64 assetSize = 0;
  QUrl checksumsUrl; ///< sums.txt with the SHA-256 of every file

  [[nodiscard]] bool canInstall() const
  {
    return !assetUrl.isEmpty() && !checksumsUrl.isEmpty();
  }
};

/**
 * @brief Finds, downloads and starts installing new versions from GitHub releases.
 *
 * Every release made by CI has the installers and a sums.txt with their SHA-256. A download
 * is only installed when its checksum matches. On Windows the MSI installer is started, on
 * macOS the app in the disk image replaces this one and starts again. Other systems get the
 * release page.
 */
class Updater : public QObject
{
  Q_OBJECT

public:
  enum class InstallResult
  {
    Started,      ///< the installer runs, quit so it can replace the app
    OpenedManual, ///< the downloaded file was opened for the user to install
    Failed
  };

  /**
   * @param releasesUrl GitHub API url listing the releases
   */
  explicit Updater(const QUrl &releasesUrl, QObject *parent = nullptr);
  ~Updater() override;

  /**
   * @param testBuilds also offer pre-releases such as the continuous build
   */
  void check(bool testBuilds);
  [[nodiscard]] bool isChecking() const;

  void download(const UpdateInfo &update);
  void cancelDownload();
  [[nodiscard]] bool isDownloading() const;

  /**
   * @brief Start installing a verified download, see InstallResult
   */
  static InstallResult install(const QString &file, QString *error = nullptr);

  /// The version of this build, four parts
  static QVersionNumber currentVersion();

  /// End of the installer's file name for this computer, e.g. "-win-x64.msi", empty when updates can't be installed
  static QString installerSuffix();

  // Helpers, public for testing

  /// The first a.b.c or a.b.c.d in text, padded to four parts
  static std::optional<QVersionNumber> versionIn(const QString &text);

  static std::optional<UpdateInfo>
  pickUpdate(const QJsonArray &releases, const QVersionNumber &current, bool testBuilds, const QString &suffix);

  /// The SHA-256 for fileName in a sha256sum style list, empty when it isn't listed
  static QByteArray expectedChecksum(const QByteArray &sums, const QString &fileName);

Q_SIGNALS:
  void updateAvailable(const deskflow::gui::UpdateInfo &update);
  void upToDate();
  void checkFailed(const QString &error);

  void downloadProgress(qint64 received, qint64 total);
  /// The file is complete and its checksum matched
  void downloaded(const QString &file);
  void downloadFailed(const QString &error);

private:
  void checkFinished();
  void assetFinished();
  void checksumsFinished();
  void failDownload(const QString &error);
  QNetworkReply *get(const QUrl &url);

  QUrl m_releasesUrl;
  bool m_testBuilds = true;
  QNetworkAccessManager *m_network = nullptr;
  QPointer<QNetworkReply> m_checkReply;
  QPointer<QNetworkReply> m_assetReply;
  QPointer<QNetworkReply> m_sumsReply;
  QFile *m_file = nullptr;
  UpdateInfo m_downloading;
};

} // namespace deskflow::gui
