/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardHistoryTests.h"

#include <QMimeData>
#include <QSignalSpy>

using deskflow::gui::ClipboardEntry;
using deskflow::gui::ClipboardHistory;

namespace {
std::unique_ptr<QMimeData> textData(const QString &text)
{
  auto data = std::make_unique<QMimeData>();
  data->setText(text);
  return data;
}

QImage testImage(int width, int height, QColor color = Qt::red)
{
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}
} // namespace

void ClipboardHistoryTests::recordsTextNewestFirst()
{
  ClipboardHistory history(nullptr);
  QSignalSpy changed(&history, &ClipboardHistory::changed);
  QVERIFY(history.add(textData("first").get()));
  QVERIFY(history.add(textData("second").get()));
  QCOMPARE(history.entries().size(), 2);
  QCOMPARE(history.entries().at(0).text, QStringLiteral("second"));
  QCOMPARE(history.entries().at(1).text, QStringLiteral("first"));
  QCOMPARE(changed.count(), 2);
}

void ClipboardHistoryTests::duplicatesMoveToTop()
{
  ClipboardHistory history(nullptr);
  history.add(textData("a").get());
  history.add(textData("b").get());
  history.add(textData("a").get());
  QCOMPARE(history.entries().size(), 2);
  QCOMPARE(history.entries().first().text, QStringLiteral("a"));
}

void ClipboardHistoryTests::ignoresBlankText()
{
  ClipboardHistory history(nullptr);
  QVERIFY(!history.add(textData("  \n\t").get()));
  QVERIFY(!history.add(nullptr));
  QMimeData empty;
  QVERIFY(!history.add(&empty));
  QVERIFY(history.entries().isEmpty());
}

void ClipboardHistoryTests::keepsAtMostMaxEntries()
{
  ClipboardHistory history(nullptr);
  for (int i = 0; i < ClipboardHistory::kMaxEntries + 5; ++i)
    history.add(textData(QString::number(i)).get());
  QCOMPARE(history.entries().size(), ClipboardHistory::kMaxEntries);
  QCOMPARE(history.entries().first().text, QString::number(ClipboardHistory::kMaxEntries + 4));
}

void ClipboardHistoryTests::recordsImages()
{
  ClipboardHistory history(nullptr);
  QMimeData data;
  data.setImageData(testImage(40, 30));
  QVERIFY(history.add(&data));
  QVERIFY(history.entries().first().isImage());
  QCOMPARE(history.entries().first().image.size(), QSize(40, 30));

  // The same picture again is not a new entry
  QVERIFY(history.add(&data));
  QCOMPARE(history.entries().size(), 1);

  QMimeData other;
  other.setImageData(testImage(40, 30, Qt::blue));
  QVERIFY(history.add(&other));
  QCOMPARE(history.entries().size(), 2);
}

void ClipboardHistoryTests::prefersTextOverImage()
{
  ClipboardHistory history(nullptr);
  QMimeData data;
  data.setText("cells");
  data.setImageData(testImage(10, 10));
  QVERIFY(history.add(&data));
  QVERIFY(!history.entries().first().isImage());
  QCOMPARE(history.entries().first().text, QStringLiteral("cells"));
}

void ClipboardHistoryTests::skipsSecrets()
{
  ClipboardHistory history(nullptr);

  auto kde = textData("hunter2");
  kde->setData("x-kde-passwordManagerHint", "secret");
  QVERIFY(ClipboardHistory::isSensitive(kde.get()));
  QVERIFY(!history.add(kde.get()));

  auto mac = textData("hunter2");
  mac->setData("org.nspasteboard.ConcealedType", "");
  QVERIFY(!history.add(mac.get()));

  auto windows = textData("hunter2");
  windows->setData(R"(application/x-qt-windows-mime;value="ExcludeClipboardContentFromMonitorProcessing")", "x");
  QVERIFY(!history.add(windows.get()));

  auto noHistory = textData("hunter2");
  noHistory->setData(R"(application/x-qt-windows-mime;value="CanIncludeInClipboardHistory")", QByteArray(4, '\0'));
  QVERIFY(!history.add(noHistory.get()));

  // Allowed explicitly
  auto allowed = textData("public");
  allowed->setData(
      R"(application/x-qt-windows-mime;value="CanIncludeInClipboardHistory")", QByteArray("\x01\0\0\0", 4)
  );
  QVERIFY(history.add(allowed.get()));
  QCOMPARE(history.entries().size(), 1);
}

void ClipboardHistoryTests::disabledRecordsNothing()
{
  ClipboardHistory history(nullptr);
  history.add(textData("kept").get());
  history.setEnabled(false);
  QVERIFY(history.entries().isEmpty());
  QVERIFY(!history.add(textData("ignored").get()));
  history.setEnabled(true);
  QVERIFY(history.add(textData("again").get()));
}

void ClipboardHistoryTests::restoreMovesToTop()
{
  ClipboardHistory history(nullptr);
  history.add(textData("old").get());
  history.add(textData("new").get());
  QVERIFY(history.restore(1));
  QCOMPARE(history.entries().first().text, QStringLiteral("old"));
  QCOMPARE(history.entries().size(), 2);
  QVERIFY(!history.restore(5));
  QVERIFY(!history.restore(-1));
}

void ClipboardHistoryTests::summaryIsShortAndMenuSafe()
{
  ClipboardEntry text{QStringLiteral("Tom & Jerry\n  second   line"), {}, {}};
  QCOMPARE(ClipboardHistory::summary(text), QStringLiteral("Tom && Jerry second line"));

  ClipboardEntry longText{QString(100, QLatin1Char('x')), {}, {}};
  const auto summary = ClipboardHistory::summary(longText, 20);
  QCOMPARE(summary.size(), 20);
  QVERIFY(summary.endsWith(QChar(0x2026)));

  ClipboardEntry image{{}, testImage(1920, 1080), {}};
  QCOMPARE(ClipboardHistory::summary(image), QStringLiteral("Image 1920 × 1080"));
}

QTEST_MAIN(ClipboardHistoryTests)
