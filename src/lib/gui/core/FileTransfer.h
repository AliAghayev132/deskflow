/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDir>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QSslConfiguration>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <optional>

class QFile;
class QSslServer;
class QSslSocket;
class QTcpSocket;
class QTimer;

namespace deskflow::gui {

/**
 * @brief Files another computer wants to send to this one
 */
struct FileOffer
{
  QString from;           ///< computer name the sender gave
  QStringList names;      ///< file names, already made safe to save
  QList<qint64> sizes;    ///< size of each file in bytes
  QByteArray fingerprint; ///< SHA-256 of the sender's certificate, empty when it sent none
  QHostAddress address;   ///< where the offer came from

  [[nodiscard]] qint64 totalSize() const;
};

/**
 * @brief Sending files between connected computers.
 *
 * The receiver listens on TCP kPort while the core runs. A sender connects with TLS using
 * the same certificate as the core, so both sides can check each other's fingerprint, and
 * offers a list of files. Nothing is written until the person on the receiving computer
 * accepts. The protocol is one JSON line each way, then the raw bytes of each file in order:
 *
 *   sender:   {"app":"deskflow","v":1,"type":"offer","from":"laptop","files":[{"name":"a.pdf","size":123}]}
 *   receiver: {"app":"deskflow","v":1,"type":"accept"} or {"type":"decline"}
 *   sender:   <bytes of every file, back to back>
 *   receiver: {"app":"deskflow","v":1,"type":"done"}
 */
namespace filetransfer {
inline constexpr quint16 kPort = 24803;
inline constexpr int kMaxFiles = 1000;
inline constexpr qsizetype kMaxLine = 256 * 1024;
/// How long the person on the other computer has to answer
inline constexpr int kAnswerTimeout = 120000;
/// Data is written in pieces of this size so big files don't end up in memory
inline constexpr qint64 kChunkSize = 256 * 1024;

QByteArray encodeOffer(const QString &from, const QStringList &names, const QList<qint64> &sizes);
std::optional<FileOffer> parseOffer(const QByteArray &line);
QByteArray encodeMessage(const QString &type);
/// The type of a valid one line message, empty for anything else
QString messageType(const QByteArray &line);

/**
 * @brief A name that is safe to create in the download folder: no folders, no reserved names
 */
QString safeFileName(const QString &name);

/**
 * @brief A path in dir for name that doesn't exist yet, "report (2).pdf" if "report.pdf" does
 */
QString uniquePath(const QDir &dir, const QString &name);

/**
 * @brief TLS settings with this computer's certificate, the one the core uses too
 */
QSslConfiguration sslConfiguration(const QString &certificatePath);

/**
 * @brief Size for people, e.g. "512 bytes", "3.4 MB"
 */
QString sizeText(qint64 bytes);
} // namespace filetransfer

/**
 * @brief Waits for other computers to offer files and saves the ones that are accepted
 */
class FileReceiver : public QObject
{
  Q_OBJECT

public:
  explicit FileReceiver(QObject *parent = nullptr);
  ~FileReceiver() override;

  bool listen(const QSslConfiguration &config, quint16 port = filetransfer::kPort);
  void close();
  [[nodiscard]] bool isListening() const;
  [[nodiscard]] quint16 port() const;

  /**
   * @brief Save the offered files in directory
   */
  void accept(int id, const QString &directory);
  void decline(int id);

Q_SIGNALS:
  void offered(int id, const deskflow::gui::FileOffer &offer);
  void progress(int id, qint64 received, qint64 total);
  /**
   * @brief The transfer ended, files holds the saved paths when it worked
   */
  void finished(int id, bool ok, const QString &error, const QStringList &files);

private:
  struct Incoming;

  void newConnection();
  void readFrom(int id);
  void receive(Incoming &incoming);
  bool openNextFile(Incoming &incoming);
  void fail(int id, const QString &error);
  void succeed(int id);
  Incoming *find(int id);

  QSslServer *m_server = nullptr;
  std::map<int, std::unique_ptr<Incoming>> m_incoming;
  int m_nextId = 1;
};

/**
 * @brief Sends files to one computer
 */
class FileSender : public QObject
{
  Q_OBJECT

public:
  using TrustCheck = std::function<bool(const QByteArray &fingerprint)>;

  FileSender(const QString &from, const QStringList &paths, QObject *parent = nullptr);
  ~FileSender() override;

  /**
   * @brief Problem with the chosen files, e.g. a folder, empty when they can be sent
   */
  [[nodiscard]] QString checkFiles() const;
  [[nodiscard]] qint64 totalSize() const
  {
    return m_total;
  }
  [[nodiscard]] int fileCount() const
  {
    return static_cast<int>(m_paths.size());
  }

  /**
   * @param trusted decides whether the receiving computer's certificate is one we know
   */
  void start(
      const QHostAddress &address, const QSslConfiguration &config, const TrustCheck &trusted,
      quint16 port = filetransfer::kPort
  );
  void cancel();

Q_SIGNALS:
  /// The offer is out, the other computer is asking its user
  void waiting();
  void progress(qint64 sent, qint64 total);
  void finished(bool ok, const QString &error);

private:
  void encrypted();
  void readReply();
  void sendMore();
  void finish(bool ok, const QString &error);

  QString m_from;
  QStringList m_paths;
  QList<qint64> m_sizes;
  qint64 m_total = 0;
  qint64 m_sent = 0;
  qint64 m_fileSent = 0;
  int m_fileIndex = 0;
  bool m_streaming = false;
  bool m_done = false;
  QByteArray m_buffer;
  TrustCheck m_trusted;
  QSslSocket *m_socket = nullptr;
  QFile *m_file = nullptr;
  QTimer *m_answerTimer = nullptr;
};

} // namespace deskflow::gui
