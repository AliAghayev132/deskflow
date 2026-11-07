/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Updater.h"

#include "common/Constants.h"
#include "common/VersionInfo.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef DESKFLOW_BUILD_ARCHITECTURE
#define DESKFLOW_BUILD_ARCHITECTURE ""
#endif

namespace deskflow::gui {

namespace {
const auto kSumsName = QStringLiteral("sums.txt");
constexpr qint64 kMaxSumsSize = 1024 * 1024;

QVersionNumber fourParts(const QVersionNumber &version)
{
  auto segments = version.segments();
  while (segments.size() < 4)
    segments.append(0);
  segments.resize(4);
  return QVersionNumber(segments);
}

QString downloadFolder()
{
  return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
      .filePath(QStringLiteral("%1-update").arg(kAppId));
}

#if defined(Q_OS_MACOS)
// Waits for the app to quit, swaps in the app from the disk image and starts it again.
// The old copy is put back when anything goes wrong.
const auto kMacScript = QStringLiteral(R"(#!/bin/sh
app="$1"; dmg="$2"; pid="$3"
exec >>"${TMPDIR:-/tmp}/deskflow-update.log" 2>&1
i=0
while kill -0 "$pid" 2>/dev/null && [ $i -lt 120 ]; do sleep 0.5; i=$((i+1)); done
mnt=$(mktemp -d)
if hdiutil attach -nobrowse -noautoopen -quiet -mountpoint "$mnt" "$dmg"; then
  new=$(find "$mnt" -maxdepth 1 -name '*.app' | head -n 1)
  if [ -n "$new" ]; then
    rm -rf "$app.old"
    if mv "$app" "$app.old" && ditto "$new" "$app"; then
      rm -rf "$app.old"
    else
      rm -rf "$app"; mv "$app.old" "$app"
    fi
  fi
  hdiutil detach -quiet "$mnt"
fi
rmdir "$mnt" 2>/dev/null
xattr -cr "$app" 2>/dev/null
open "$app"
)");
#endif
} // namespace

Updater::Updater(const QUrl &releasesUrl, QObject *parent)
    : QObject(parent),
      m_releasesUrl{releasesUrl},
      m_network{new QNetworkAccessManager(this)}
{
  m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

Updater::~Updater()
{
  cancelDownload();
}

QVersionNumber Updater::currentVersion()
{
  return fourParts(QVersionNumber::fromString(QString::fromLatin1(kVersion)));
}

QString Updater::installerSuffix()
{
  const auto architecture = QStringLiteral(DESKFLOW_BUILD_ARCHITECTURE);
  if (architecture.isEmpty())
    return {};

#if defined(Q_OS_WIN)
  // Only copies installed with the MSI can be updated by it, a portable copy would end up installed twice
  const auto appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
  for (const auto *variable : {"ProgramFiles", "ProgramW6432", "ProgramFiles(Arm)"}) {
    const auto programFiles = QDir::cleanPath(QDir::fromNativeSeparators(qEnvironmentVariable(variable)));
    if (!programFiles.isEmpty() && appDir.startsWith(programFiles + QLatin1Char('/'), Qt::CaseInsensitive))
      return QStringLiteral("-win-%1.msi").arg(architecture);
  }
  return {};
#elif defined(Q_OS_MACOS)
  if (!QCoreApplication::applicationDirPath().endsWith(QLatin1String(".app/Contents/MacOS")))
    return {};
  return QStringLiteral("-macos-%1.dmg").arg(architecture);
#else
  return {};
#endif
}

std::optional<QVersionNumber> Updater::versionIn(const QString &text)
{
  static const QRegularExpression pattern(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
  const auto match = pattern.match(text);
  if (!match.hasMatch())
    return std::nullopt;
  QList<int> segments;
  for (int i = 1; i <= 4; ++i)
    segments.append(match.captured(i).toInt());
  return QVersionNumber(segments);
}

std::optional<UpdateInfo>
Updater::pickUpdate(const QJsonArray &releases, const QVersionNumber &current, bool testBuilds, const QString &suffix)
{
  std::optional<UpdateInfo> best;
  for (const auto &value : releases) {
    const auto release = value.toObject();
    if (release.value(QStringLiteral("draft")).toBool())
      continue;
    const bool testBuild = release.value(QStringLiteral("prerelease")).toBool();
    if (testBuild && !testBuilds)
      continue;

    // CI names continuous builds "Continuous v1.27.0.150", tagged releases are named after the tag
    auto version = versionIn(release.value(QStringLiteral("name")).toString());
    if (!version)
      version = versionIn(release.value(QStringLiteral("tag_name")).toString());
    if (!version || *version <= current)
      continue;

    UpdateInfo update;
    update.version = *version;
    update.title = release.value(QStringLiteral("name")).toString();
    if (update.title.isEmpty())
      update.title = release.value(QStringLiteral("tag_name")).toString();
    update.notes = release.value(QStringLiteral("body")).toString();
    update.page = QUrl(release.value(QStringLiteral("html_url")).toString());
    update.testBuild = testBuild;

    const auto assets = release.value(QStringLiteral("assets")).toArray();
    for (const auto &assetValue : assets) {
      const auto asset = assetValue.toObject();
      const auto name = asset.value(QStringLiteral("name")).toString();
      const auto url = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
      if (name == kSumsName) {
        update.checksumsUrl = url;
      } else if (!suffix.isEmpty() && name.endsWith(suffix, Qt::CaseInsensitive) && update.assetName.isEmpty()) {
        update.assetName = name;
        update.assetUrl = url;
        update.assetSize = asset.value(QStringLiteral("size")).toInteger();
      }
    }

    // A release this computer can install wins over a newer one it can't, e.g. when a build failed
    const auto better = [&update](const UpdateInfo &other) {
      if (update.canInstall() != other.canInstall())
        return update.canInstall();
      return update.version > other.version;
    };
    if (!best || better(*best))
      best = update;
  }
  return best;
}

QByteArray Updater::expectedChecksum(const QByteArray &sums, const QString &fileName)
{
  const auto lines = sums.split('\n');
  for (const auto &rawLine : lines) {
    const auto line = rawLine.trimmed();
    // "<hash>  <name>", or "<hash> *<name>" for binary mode
    const auto space = line.indexOf(' ');
    if (space != 64)
      continue;
    auto name = line.mid(space).trimmed();
    if (name.startsWith('*'))
      name.remove(0, 1);
    if (QString::fromUtf8(name) == fileName)
      return line.left(64).toLower();
  }
  return {};
}

QNetworkReply *Updater::get(const QUrl &url)
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("%1/%2").arg(kAppName, kVersion));
  if (url.host() == QLatin1String("api.github.com"))
    request.setRawHeader("Accept", "application/vnd.github+json");
  request.setTransferTimeout(30000);
  return m_network->get(request);
}

void Updater::check(bool testBuilds)
{
  if (isChecking())
    return;
  m_testBuilds = testBuilds;
  qDebug().noquote() << "checking for updates at" << m_releasesUrl.toString();
  m_checkReply = get(m_releasesUrl);
  connect(m_checkReply, &QNetworkReply::finished, this, &Updater::checkFinished);
}

bool Updater::isChecking() const
{
  return m_checkReply != nullptr;
}

void Updater::checkFinished()
{
  auto reply = m_checkReply;
  m_checkReply = nullptr;
  if (!reply)
    return;
  reply->deleteLater();

  if (reply->error() != QNetworkReply::NoError) {
    qWarning().noquote() << "update check failed:" << reply->errorString();
    Q_EMIT checkFailed(reply->errorString());
    return;
  }

  const auto doc = QJsonDocument::fromJson(reply->readAll());
  if (!doc.isArray()) {
    Q_EMIT checkFailed(tr("The list of releases could not be read"));
    return;
  }

  const auto update = pickUpdate(doc.array(), currentVersion(), m_testBuilds, installerSuffix());
  if (!update) {
    qDebug().noquote() << "no update, this is version" << currentVersion().toString();
    Q_EMIT upToDate();
    return;
  }
  qInfo().noquote() << "update available:" << update->version.toString() << update->title;
  Q_EMIT updateAvailable(*update);
}

void Updater::download(const UpdateInfo &update)
{
  if (isDownloading() || !update.canInstall())
    return;

  m_downloading = update;
  QDir().mkpath(downloadFolder());
  m_file = new QFile(QDir(downloadFolder()).filePath(update.assetName), this);
  if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    failDownload(tr("Could not save the update: %1").arg(m_file->errorString()));
    return;
  }

  m_assetReply = get(update.assetUrl);
  m_assetReply->setReadBufferSize(1024 * 1024);
  connect(m_assetReply, &QNetworkReply::readyRead, this, [this] {
    if (m_assetReply && m_file)
      m_file->write(m_assetReply->readAll());
  });
  connect(m_assetReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
    Q_EMIT downloadProgress(received, total > 0 ? total : m_downloading.assetSize);
  });
  connect(m_assetReply, &QNetworkReply::finished, this, &Updater::assetFinished);
}

bool Updater::isDownloading() const
{
  return m_assetReply != nullptr || m_sumsReply != nullptr;
}

void Updater::cancelDownload()
{
  for (auto reply : {m_assetReply, m_sumsReply}) {
    if (reply) {
      reply->disconnect(this);
      reply->abort();
      reply->deleteLater();
    }
  }
  m_assetReply = nullptr;
  m_sumsReply = nullptr;
  if (m_file) {
    m_file->close();
    m_file->remove();
    m_file->deleteLater();
    m_file = nullptr;
  }
}

void Updater::failDownload(const QString &error)
{
  cancelDownload();
  qWarning().noquote() << "update download failed:" << error;
  Q_EMIT downloadFailed(error);
}

void Updater::assetFinished()
{
  auto reply = m_assetReply;
  if (!reply)
    return;
  if (reply->error() != QNetworkReply::NoError) {
    failDownload(reply->errorString());
    return;
  }
  m_file->write(reply->readAll());
  m_file->close();
  reply->deleteLater();
  m_assetReply = nullptr;

  m_sumsReply = get(m_downloading.checksumsUrl);
  connect(m_sumsReply, &QNetworkReply::finished, this, &Updater::checksumsFinished);
}

void Updater::checksumsFinished()
{
  auto reply = m_sumsReply;
  if (!reply)
    return;
  if (reply->error() != QNetworkReply::NoError) {
    failDownload(reply->errorString());
    return;
  }
  const auto sums = reply->read(kMaxSumsSize);
  reply->deleteLater();
  m_sumsReply = nullptr;

  const auto expected = expectedChecksum(sums, m_downloading.assetName);
  if (expected.isEmpty()) {
    failDownload(tr("The update could not be verified, it has no checksum"));
    return;
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!m_file->open(QIODevice::ReadOnly) || !hash.addData(m_file)) {
    failDownload(tr("Could not read the downloaded update"));
    return;
  }
  m_file->close();
  if (hash.result().toHex() != expected) {
    failDownload(tr("The downloaded update is damaged, its checksum doesn't match"));
    return;
  }

  const auto path = m_file->fileName();
  m_file->deleteLater();
  m_file = nullptr;
  qInfo().noquote() << "update downloaded and verified:" << path;
  Q_EMIT downloaded(path);
}

Updater::InstallResult Updater::install(const QString &file, QString *error)
{
  const auto fail = [error](const QString &message) {
    if (error)
      *error = message;
    return InstallResult::Failed;
  };

#if defined(Q_OS_WIN)
  if (file.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive)) {
    // The installer closes the running app, replaces it and offers to start it again
    if (!QProcess::startDetached(QStringLiteral("msiexec"), {QStringLiteral("/i"), QDir::toNativeSeparators(file)}))
      return fail(tr("The installer could not be started"));
    return InstallResult::Started;
  }
#elif defined(Q_OS_MACOS)
  if (file.endsWith(QLatin1String(".dmg"), Qt::CaseInsensitive)) {
    const auto bundle = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    if (bundle.endsWith(QLatin1String(".app")) && QFileInfo(QFileInfo(bundle).absolutePath()).isWritable()) {
      const auto script = QDir(downloadFolder()).filePath(QStringLiteral("install-update.sh"));
      QFile scriptFile(script);
      if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate) && scriptFile.write(kMacScript.toUtf8()) > 0) {
        scriptFile.close();
        const QStringList arguments{script, bundle, file, QString::number(QCoreApplication::applicationPid())};
        if (QProcess::startDetached(QStringLiteral("/bin/sh"), arguments))
          return InstallResult::Started;
      }
    }
    // Can't replace the app ourselves, e.g. it isn't in a folder we may write to
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(file)))
      return InstallResult::OpenedManual;
    return fail(tr("The disk image could not be opened"));
  }
#endif

  if (QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath())))
    return InstallResult::OpenedManual;
  return fail(tr("This update can't be installed automatically"));
}

} // namespace deskflow::gui
