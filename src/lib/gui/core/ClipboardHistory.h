/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>

class QClipboard;
class QMimeData;

namespace deskflow::gui {

struct ClipboardEntry
{
  QString text;
  QImage image;
  QDateTime time;

  bool isImage() const
  {
    return !image.isNull();
  }
  bool sameContent(const ClipboardEntry &other) const;
};

/**
 * @brief Remembers recent clipboard contents, including what arrives from other computers.
 *
 * Entries are kept in memory only and are lost when the app quits. Items that password managers
 * mark as secret are never recorded.
 */
class ClipboardHistory : public QObject
{
  Q_OBJECT

public:
  inline static const int kMaxEntries = 20;
  inline static const qsizetype kMaxTextSize = 1024 * 1024;
  inline static const qint64 kMaxImagePixels = 4096LL * 4096LL;

  explicit ClipboardHistory(QClipboard *clipboard, QObject *parent = nullptr);

  bool isEnabled() const
  {
    return m_enabled;
  }
  /// Turning the history off also forgets what was recorded
  void setEnabled(bool enabled);

  const QList<ClipboardEntry> &entries() const
  {
    return m_entries;
  }

  /// Records clipboard data, returns false when nothing was recorded
  bool add(const QMimeData *data, const QDateTime &time = QDateTime::currentDateTime());

  /// Puts an entry back on the clipboard and moves it to the top
  bool restore(int index);

  void clear();

  /// One line that fits in a menu, for example "Hello world" or "Image 1920 × 1080"
  static QString summary(const ClipboardEntry &entry, int maxLength = 48);

  /// True when a password manager asked clipboard tools not to record the data
  static bool isSensitive(const QMimeData *data);

Q_SIGNALS:
  void changed();

private:
  void clipboardChanged();

  QPointer<QClipboard> m_clipboard;
  QList<ClipboardEntry> m_entries;
  bool m_enabled = true;
};

} // namespace deskflow::gui
