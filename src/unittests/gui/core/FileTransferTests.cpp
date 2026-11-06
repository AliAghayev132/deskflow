/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferTests.h"

#include "net/SecureUtils.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslKey>

using deskflow::gui::FileOffer;
using deskflow::gui::FileReceiver;
using deskflow::gui::FileSender;
namespace filetransfer = deskflow::gui::filetransfer;

namespace {
QString writeFile(const QString &path, const QByteArray &data)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
    return {};
  return path;
}

QByteArray readFile(const QString &path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray randomBytes(qsizetype size)
{
  QByteArray data(size, Qt::Uninitialized);
  for (auto &byte : data)
    byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
  return data;
}
} // namespace

void FileTransferTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  m_receiverCert = m_dir.filePath("receiver.pem");
  m_senderCert = m_dir.filePath("sender.pem");
  deskflow::generatePemSelfSignedCert(m_receiverCert, 2048);
  deskflow::generatePemSelfSignedCert(m_senderCert, 2048);
  QVERIFY(!fingerprintOf(m_receiverCert).isEmpty());
  QVERIFY(fingerprintOf(m_receiverCert) != fingerprintOf(m_senderCert));
  QVERIFY(!filetransfer::sslConfiguration(m_senderCert).privateKey().isNull());
}

QByteArray FileTransferTests::fingerprintOf(const QString &certificate) const
{
  const auto certificates = QSslCertificate::fromPath(certificate);
  return certificates.isEmpty() ? QByteArray() : certificates.first().digest(QCryptographicHash::Sha256);
}

void FileTransferTests::offerRoundTrip()
{
  const auto line = filetransfer::encodeOffer("laptop", {"a.txt", "photo.jpg"}, {12, 3000000000LL});
  QVERIFY(line.endsWith('\n'));
  const auto offer = filetransfer::parseOffer(line.trimmed());
  QVERIFY(offer.has_value());
  QCOMPARE(offer->from, QStringLiteral("laptop"));
  QCOMPARE(offer->names, (QStringList{"a.txt", "photo.jpg"}));
  QCOMPARE(offer->sizes, (QList<qint64>{12, 3000000000LL}));
  QCOMPARE(offer->totalSize(), 3000000012LL);

  QCOMPARE(filetransfer::messageType(filetransfer::encodeMessage("accept")), QStringLiteral("accept"));
}

void FileTransferTests::rejectsBadOffers()
{
  QVERIFY(!filetransfer::parseOffer("nonsense"));
  QVERIFY(!filetransfer::parseOffer(R"({"app":"deskflow","v":1,"type":"offer","files":[]})"));
  QVERIFY(!filetransfer::parseOffer(R"({"app":"deskflow","v":1,"type":"offer","files":[{"name":"a","size":-1}]})"));
  QVERIFY(!filetransfer::parseOffer(R"({"app":"other","v":1,"type":"offer","files":[{"name":"a","size":1}]})"));
  QVERIFY(!filetransfer::parseOffer(filetransfer::encodeMessage("accept").trimmed()));

  // Names that try to leave the download folder are cut down to the file name
  const auto offer = filetransfer::parseOffer(
      R"({"app":"deskflow","v":1,"type":"offer","files":[{"name":"../../.ssh/authorized_keys","size":1}]})"
  );
  QVERIFY(offer.has_value());
  QCOMPARE(offer->names.first(), QStringLiteral("authorized_keys"));
}

void FileTransferTests::safeFileNames()
{
  QCOMPARE(filetransfer::safeFileName("report.pdf"), QStringLiteral("report.pdf"));
  QCOMPARE(filetransfer::safeFileName("C:\\Users\\x\\evil.exe"), QStringLiteral("evil.exe"));
  QCOMPARE(filetransfer::safeFileName("/etc/passwd"), QStringLiteral("passwd"));
  QCOMPARE(filetransfer::safeFileName(".."), QStringLiteral("file"));
  QCOMPARE(filetransfer::safeFileName(""), QStringLiteral("file"));
  QCOMPARE(filetransfer::safeFileName("a<b>c:d|e?f*g\".txt"), QStringLiteral("a_b_c_d_e_f_g_.txt"));
  QCOMPARE(filetransfer::safeFileName("run.exe. . "), QStringLiteral("run.exe"));
  QCOMPARE(filetransfer::safeFileName("CON"), QStringLiteral("_CON"));
  QCOMPARE(filetransfer::safeFileName("nul.txt"), QStringLiteral("_nul.txt"));
  QCOMPARE(filetransfer::safeFileName("line\nbreak.txt"), QStringLiteral("line_break.txt"));
  QCOMPARE(filetransfer::safeFileName(QStringLiteral(u"\u015E\u0259kil.png")), QStringLiteral(u"\u015E\u0259kil.png"));

  const auto longName = filetransfer::safeFileName(QString(300, 'x') + ".pdf");
  QCOMPARE(longName.size(), 200);
  QVERIFY(longName.endsWith(".pdf"));
}

void FileTransferTests::uniquePaths()
{
  QTemporaryDir dir;
  const QDir folder(dir.path());
  QCOMPARE(filetransfer::uniquePath(folder, "report.pdf"), folder.filePath("report.pdf"));
  writeFile(folder.filePath("report.pdf"), "x");
  QCOMPARE(filetransfer::uniquePath(folder, "report.pdf"), folder.filePath("report (2).pdf"));
  writeFile(folder.filePath("report (2).pdf"), "x");
  QCOMPARE(filetransfer::uniquePath(folder, "report.pdf"), folder.filePath("report (3).pdf"));

  writeFile(folder.filePath("archive.tar.gz"), "x");
  QCOMPARE(filetransfer::uniquePath(folder, "archive.tar.gz"), folder.filePath("archive (2).tar.gz"));
  writeFile(folder.filePath(".bashrc"), "x");
  QCOMPARE(filetransfer::uniquePath(folder, ".bashrc"), folder.filePath(".bashrc (2)"));
}

void FileTransferTests::sendsFiles()
{
  QTemporaryDir source;
  QTemporaryDir target;
  const auto big = randomBytes(3 * filetransfer::kChunkSize + 1234);
  const QStringList paths{
      writeFile(source.filePath("big.bin"), big), writeFile(source.filePath("empty.txt"), {}),
      writeFile(source.filePath("note.txt"), "salam")
  };
  // A file with the same name is already there and must not be overwritten
  writeFile(target.filePath("note.txt"), "old");

  FileReceiver receiver;
  QVERIFY(receiver.listen(filetransfer::sslConfiguration(m_receiverCert), 0));
  QSignalSpy offered(&receiver, &FileReceiver::offered);
  QSignalSpy received(&receiver, &FileReceiver::finished);
  connect(&receiver, &FileReceiver::offered, this, [&receiver, &target](int id, const FileOffer &offer) {
    QCOMPARE(offer.from, QStringLiteral("laptop"));
    receiver.accept(id, target.path());
  });

  FileSender sender("laptop", paths);
  QVERIFY(sender.checkFiles().isEmpty());
  QCOMPARE(sender.totalSize(), big.size() + 5);
  QSignalSpy waiting(&sender, &FileSender::waiting);
  QSignalSpy sent(&sender, &FileSender::finished);
  const auto receiverFingerprint = fingerprintOf(m_receiverCert);
  sender.start(
      QHostAddress::LocalHost, filetransfer::sslConfiguration(m_senderCert),
      [receiverFingerprint](const QByteArray &fingerprint) { return fingerprint == receiverFingerprint; },
      receiver.port()
  );

  QVERIFY(sent.wait(15000));
  QVERIFY2(sent.first().at(0).toBool(), qPrintable(sent.first().at(1).toString()));
  QCOMPARE(waiting.count(), 1);
  if (received.isEmpty())
    QVERIFY(received.wait(5000));
  QCOMPARE(offered.count(), 1);

  // The receiver knows who sent the files
  const auto offer = offered.first().at(1).value<FileOffer>();
  QCOMPARE(offer.fingerprint, fingerprintOf(m_senderCert));
  QCOMPARE(offer.names, (QStringList{"big.bin", "empty.txt", "note.txt"}));

  QVERIFY(received.first().at(1).toBool());
  const auto files = received.first().at(3).toStringList();
  QCOMPARE(files.size(), 3);
  QCOMPARE(readFile(target.filePath("big.bin")), big);
  QVERIFY(QFileInfo::exists(target.filePath("empty.txt")));
  QCOMPARE(readFile(target.filePath("note.txt")), QByteArray("old"));
  QCOMPARE(readFile(target.filePath("note (2).txt")), QByteArray("salam"));
}

void FileTransferTests::declinedFiles()
{
  QTemporaryDir source;
  QTemporaryDir target;
  const QStringList paths{writeFile(source.filePath("secret.txt"), "no")};

  FileReceiver receiver;
  QVERIFY(receiver.listen(filetransfer::sslConfiguration(m_receiverCert), 0));
  connect(&receiver, &FileReceiver::offered, &receiver, &FileReceiver::decline);

  FileSender sender("laptop", paths);
  QSignalSpy sent(&sender, &FileSender::finished);
  sender.start(
      QHostAddress::LocalHost, filetransfer::sslConfiguration(m_senderCert), [](const QByteArray &) { return true; },
      receiver.port()
  );
  QVERIFY(sent.wait(15000));
  QVERIFY(!sent.first().at(0).toBool());
  QVERIFY(sent.first().at(1).toString().contains("declined"));
  QVERIFY(QDir(target.path()).isEmpty());
}

void FileTransferTests::untrustedReceiver()
{
  QTemporaryDir source;
  const QStringList paths{writeFile(source.filePath("a.txt"), "a")};

  FileReceiver receiver;
  QVERIFY(receiver.listen(filetransfer::sslConfiguration(m_receiverCert), 0));
  QSignalSpy offered(&receiver, &FileReceiver::offered);

  FileSender sender("laptop", paths);
  QSignalSpy sent(&sender, &FileSender::finished);
  sender.start(
      QHostAddress::LocalHost, filetransfer::sslConfiguration(m_senderCert), [](const QByteArray &) { return false; },
      receiver.port()
  );
  QVERIFY(sent.wait(15000));
  QVERIFY(!sent.first().at(0).toBool());
  // Nothing about the files reached a computer that couldn't be verified
  QTest::qWait(200);
  QCOMPARE(offered.count(), 0);
}

void FileTransferTests::refusesFolders()
{
  QTemporaryDir source;
  FileSender folder("laptop", {source.path()});
  QVERIFY(!folder.checkFiles().isEmpty());
  FileSender none("laptop", {});
  QVERIFY(!none.checkFiles().isEmpty());
  FileSender missing("laptop", {source.filePath("missing.txt")});
  QVERIFY(!missing.checkFiles().isEmpty());
}

QTEST_MAIN(FileTransferTests)
