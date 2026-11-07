/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UpdaterTests.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

using deskflow::gui::UpdateInfo;
using deskflow::gui::Updater;

namespace {
const auto kSuffix = QStringLiteral("-win-x64.msi");

QJsonObject asset(const QString &name, const QString &url = {}, qint64 size = 10)
{
  return {
      {"name", name},
      {"browser_download_url", url.isEmpty() ? QStringLiteral("https://example.invalid/") + name : url},
      {"size", size}
  };
}

QJsonObject release(const QString &name, const QString &tag, bool prerelease, const QJsonArray &assets)
{
  return {
      {"name", name},
      {"tag_name", tag},
      {"prerelease", prerelease},
      {"draft", false},
      {"html_url", "https://example.invalid/release/" + tag},
      {"body", "notes for " + tag},
      {"assets", assets}
  };
}

QByteArray sumsFor(const QString &name, const QByteArray &data)
{
  return "Build: 9.0.0.0\n" + QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() + "  " +
         name.toUtf8() + "\n";
}
} // namespace

void UpdaterTests::initTestCase()
{
  // A tiny web server that hands out m_files, enough for QNetworkAccessManager
  m_server = new QTcpServer(this);
  QVERIFY(m_server->listen(QHostAddress::LocalHost));
  connect(m_server, &QTcpServer::newConnection, this, [this] {
    while (auto socket = m_server->nextPendingConnection()) {
      connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
        const auto request = socket->readAll();
        const auto path = QString::fromLatin1(request.split(' ').value(1));
        QByteArray response;
        if (m_files.contains(path)) {
          const auto &body = m_files[path];
          response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                     "\r\nConnection: close\r\n\r\n" + body;
        } else {
          response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        }
        socket->write(response);
        socket->disconnectFromHost();
      });
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
  });
}

QUrl UpdaterTests::serve(const QString &path) const
{
  return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server->serverPort()).arg(path));
}

void UpdaterTests::versionIn()
{
  QCOMPARE(
      Updater::versionIn("Continuous v1.27.0.150"), std::optional<QVersionNumber>(QVersionNumber({1, 27, 0, 150}))
  );
  QCOMPARE(Updater::versionIn("v2.0.0"), std::optional<QVersionNumber>(QVersionNumber({2, 0, 0, 0})));
  QCOMPARE(Updater::versionIn("2.1.3-rc1"), std::optional<QVersionNumber>(QVersionNumber({2, 1, 3, 0})));
  QVERIFY(!Updater::versionIn("continuous"));
}

void UpdaterTests::currentVersionHasFourParts()
{
  QCOMPARE(Updater::currentVersion().segmentCount(), 4);
  QVERIFY(Updater::currentVersion() > QVersionNumber(0));
}

void UpdaterTests::picksNewestInstallable()
{
  const QJsonArray releases{
      release(
          "Continuous v2.0.0.12", "continuous", true, {asset("deskflow-continuous-win-x64.msi"), asset("sums.txt")}
      ),
      release("v2.0.0", "v2.0.0", false, {asset("deskflow-2.0.0-win-x64.msi"), asset("sums.txt")}),
      // Newer, but its Windows build is missing
      release(
          "Continuous v2.0.0.20", "continuous2", true, {asset("deskflow-continuous-macos-arm64.dmg"), asset("sums.txt")}
      ),
  };

  const auto update = Updater::pickUpdate(releases, QVersionNumber({1, 27, 0, 100}), true, kSuffix);
  QVERIFY(update.has_value());
  QCOMPARE(update->version, QVersionNumber({2, 0, 0, 12}));
  QVERIFY(update->testBuild);
  QVERIFY(update->canInstall());
  QCOMPARE(update->assetName, QStringLiteral("deskflow-continuous-win-x64.msi"));
  QCOMPARE(update->notes, QStringLiteral("notes for continuous"));

  // Without an installer for this system the newest release is still reported, for its page
  const auto anyUpdate = Updater::pickUpdate(releases, QVersionNumber({1, 27, 0, 100}), true, QString());
  QVERIFY(anyUpdate.has_value());
  QVERIFY(!anyUpdate->canInstall());
  QCOMPARE(anyUpdate->version, QVersionNumber({2, 0, 0, 20}));
}

void UpdaterTests::skipsTestBuildsWhenAsked()
{
  const QJsonArray releases{
      release(
          "Continuous v2.0.0.12", "continuous", true, {asset("deskflow-continuous-win-x64.msi"), asset("sums.txt")}
      ),
      release("v2.0.0", "v2.0.0", false, {asset("deskflow-2.0.0-win-x64.msi"), asset("sums.txt")}),
  };
  const auto update = Updater::pickUpdate(releases, QVersionNumber({1, 27, 0, 100}), false, kSuffix);
  QVERIFY(update.has_value());
  QCOMPARE(update->version, QVersionNumber({2, 0, 0, 0}));
  QVERIFY(!update->testBuild);
}

void UpdaterTests::nothingNewer()
{
  const QJsonArray releases{
      release("v2.0.0", "v2.0.0", false, {asset("deskflow-2.0.0-win-x64.msi"), asset("sums.txt")}),
      QJsonObject{{"name", "draft v9.0.0"}, {"draft", true}},
  };
  QVERIFY(!Updater::pickUpdate(releases, QVersionNumber({2, 0, 0, 0}), true, kSuffix));
  QVERIFY(!Updater::pickUpdate(releases, QVersionNumber({2, 0, 0, 3}), true, kSuffix));
  QVERIFY(!Updater::pickUpdate({}, QVersionNumber({1, 0, 0, 0}), true, kSuffix));
}

void UpdaterTests::checksums()
{
  const QByteArray sums = "Build: 2.0.0.12\n"
                          "0123456789abcdef0123456789abcdef0123456789abcdef0123456789ABCDEF  deskflow-a.msi\n"
                          "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210 *deskflow-b.dmg\n";
  QCOMPARE(
      Updater::expectedChecksum(sums, "deskflow-a.msi"),
      QByteArray("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
  );
  QCOMPARE(
      Updater::expectedChecksum(sums, "deskflow-b.dmg"),
      QByteArray("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210")
  );
  QVERIFY(Updater::expectedChecksum(sums, "deskflow-c.msi").isEmpty());
  QVERIFY(Updater::expectedChecksum(sums, "Build:").isEmpty());
}

void UpdaterTests::checkFindsUpdate()
{
  const QJsonArray releases{release("v99.0.0", "v99.0.0", false, {asset("sums.txt")})};
  m_files.insert("/releases", QJsonDocument(releases).toJson());

  Updater updater(serve("/releases"));
  QSignalSpy available(&updater, &Updater::updateAvailable);
  updater.check(true);
  QVERIFY(available.wait(5000));
  QCOMPARE(available.first().at(0).value<UpdateInfo>().version, QVersionNumber({99, 0, 0, 0}));

  m_files.insert("/old", QJsonDocument(QJsonArray{release("v0.1.0", "v0.1.0", false, {})}).toJson());
  Updater upToDate(serve("/old"));
  QSignalSpy none(&upToDate, &Updater::upToDate);
  upToDate.check(true);
  QVERIFY(none.wait(5000));

  Updater broken(serve("/missing"));
  QSignalSpy failed(&broken, &Updater::checkFailed);
  broken.check(true);
  QVERIFY(failed.wait(5000));
}

void UpdaterTests::downloadVerifiesChecksum()
{
  const QByteArray installer(300000, 'x');
  m_files.insert("/good.msi", installer);
  m_files.insert("/good-sums.txt", sumsFor("deskflow-good-win-x64.msi", installer));

  UpdateInfo update;
  update.assetName = "deskflow-good-win-x64.msi";
  update.assetUrl = serve("/good.msi");
  update.assetSize = installer.size();
  update.checksumsUrl = serve("/good-sums.txt");

  Updater updater(serve("/releases"));
  QSignalSpy done(&updater, &Updater::downloaded);
  QSignalSpy failed(&updater, &Updater::downloadFailed);
  updater.download(update);
  QVERIFY(updater.isDownloading());
  QVERIFY(done.wait(5000));
  QCOMPARE(failed.count(), 0);

  QFile file(done.first().at(0).toString());
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), installer);
  file.close();
  file.remove();
}

void UpdaterTests::downloadRejectsDamagedFile()
{
  m_files.insert("/bad.msi", QByteArray(1000, 'y'));
  m_files.insert("/bad-sums.txt", sumsFor("deskflow-bad-win-x64.msi", QByteArray(1000, 'z')));

  UpdateInfo update;
  update.assetName = "deskflow-bad-win-x64.msi";
  update.assetUrl = serve("/bad.msi");
  update.checksumsUrl = serve("/bad-sums.txt");

  Updater updater(serve("/releases"));
  QSignalSpy done(&updater, &Updater::downloaded);
  QSignalSpy failed(&updater, &Updater::downloadFailed);
  updater.download(update);
  QVERIFY(failed.wait(5000));
  QCOMPARE(done.count(), 0);
  QVERIFY(failed.first().at(0).toString().contains("checksum"));
}

QTEST_MAIN(UpdaterTests)
