/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistory.h"

#include <QClipboard>
#include <QMimeData>

namespace deskflow::gui {

bool ClipboardEntry::sameContent(const ClipboardEntry &other) const
{
  if (isImage() != other.isImage())
    return false;
  return isImage() ? image == other.image : text == other.text;
}

ClipboardHistory::ClipboardHistory(QClipboard *clipboard, QObject *parent) : QObject(parent), m_clipboard{clipboard}
{
  if (m_clipboard)
    connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardHistory::clipboardChanged);
}

void ClipboardHistory::setEnabled(bool enabled)
{
  if (m_enabled == enabled)
    return;
  m_enabled = enabled;
  if (!enabled)
    clear();
}

void ClipboardHistory::clipboardChanged()
{
  if (m_enabled && m_clipboard)
    add(m_clipboard->mimeData(QClipboard::Clipboard));
}

bool ClipboardHistory::isSensitive(const QMimeData *data)
{
  if (!data)
    return false;

  // KDE (Klipper, KeePassXC), macOS (NSPasteboard convention) and Windows clipboard history hints
  if (data->data(QStringLiteral("x-kde-passwordManagerHint")) == QByteArrayLiteral("secret"))
    return true;

  const auto formats = data->formats();
  for (const auto &format : formats) {
    if (format.contains(QLatin1String("org.nspasteboard.ConcealedType")) ||
        format.contains(QLatin1String("org.nspasteboard.TransientType")) ||
        format.contains(QLatin1String("ExcludeClipboardContentFromMonitorProcessing")))
      return true;
    if (format.contains(QLatin1String("CanIncludeInClipboardHistory"))) {
      const auto value = data->data(format);
      if (!value.isEmpty() && value.count('\0') == value.size())
        return true;
    }
  }
  return false;
}

bool ClipboardHistory::add(const QMimeData *data, const QDateTime &time)
{
  if (!m_enabled || !data || isSensitive(data))
    return false;

  ClipboardEntry entry;
  entry.time = time;

  // Text wins when both are there, spreadsheets for example also put a picture of the cells on the clipboard
  if (data->hasText() && !data->text().trimmed().isEmpty()) {
    entry.text = data->text();
    if (entry.text.size() > kMaxTextSize)
      return false;
  } else if (data->hasImage()) {
    entry.image = qvariant_cast<QImage>(data->imageData());
    if (entry.image.isNull() || qint64(entry.image.width()) * entry.image.height() > kMaxImagePixels)
      return false;
  } else {
    return false;
  }

  for (qsizetype i = 0; i < m_entries.size(); ++i) {
    if (m_entries.at(i).sameContent(entry)) {
      m_entries.removeAt(i);
      break;
    }
  }

  m_entries.prepend(entry);
  while (m_entries.size() > kMaxEntries)
    m_entries.removeLast();

  Q_EMIT changed();
  return true;
}

bool ClipboardHistory::restore(int index)
{
  if (index < 0 || index >= m_entries.size())
    return false;

  auto entry = m_entries.takeAt(index);
  entry.time = QDateTime::currentDateTime();
  m_entries.prepend(entry);

  if (m_clipboard) {
    // Setting the clipboard triggers dataChanged, which only moves this entry to the top again
    if (entry.isImage())
      m_clipboard->setImage(entry.image, QClipboard::Clipboard);
    else
      m_clipboard->setText(entry.text, QClipboard::Clipboard);
  }

  Q_EMIT changed();
  return true;
}

void ClipboardHistory::clear()
{
  if (m_entries.isEmpty())
    return;
  m_entries.clear();
  Q_EMIT changed();
}

QString ClipboardHistory::summary(const ClipboardEntry &entry, int maxLength)
{
  if (entry.isImage()) {
    //: %1 and %2 are the width and height in pixels
    return tr("Image %1 × %2").arg(entry.image.width()).arg(entry.image.height());
  }

  auto text = entry.text.simplified();
  if (text.size() > maxLength)
    text = text.left(maxLength - 1).trimmed() + QChar(0x2026);
  // Keep '&' visible in menus instead of turning it into a shortcut
  text.replace(QLatin1Char('&'), QLatin1String("&&"));
  return text;
}

} // namespace deskflow::gui
