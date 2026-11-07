/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenDimmerTests.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

namespace {
void waitForFade()
{
  QTest::qWait(ScreenDimmer::kFadeTime + 150);
}
} // namespace

void ScreenDimmerTests::startsClear()
{
  ScreenDimmer dimmer;
  QVERIFY(!dimmer.isDimmed());
  for (auto overlay : dimmer.overlays())
    QVERIFY(!overlay->isVisible());
}

void ScreenDimmerTests::coversEveryScreen()
{
  ScreenDimmer dimmer;
  dimmer.setDimmed(true);
  waitForFade();
  const auto screens = QGuiApplication::screens();
  const auto overlays = dimmer.overlays();
  QCOMPARE(overlays.size(), screens.size());
  for (qsizetype i = 0; i < screens.size(); ++i) {
    QVERIFY(overlays.at(i)->isVisible());
    QCOMPARE(overlays.at(i)->geometry(), screens.at(i)->geometry());
  }
}

void ScreenDimmerTests::neverTakesTheMouseOrFocus()
{
  ScreenDimmer dimmer;
  dimmer.setDimmed(true);
  for (auto overlay : dimmer.overlays()) {
    QVERIFY(overlay->windowFlags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(overlay->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QVERIFY(overlay->testAttribute(Qt::WA_ShowWithoutActivating));
  }
}

void ScreenDimmerTests::undimHides()
{
  ScreenDimmer dimmer;
  dimmer.setDimmed(true);
  waitForFade();
  dimmer.setDimmed(false);
  waitForFade();
  QVERIFY(!dimmer.isDimmed());
  for (auto overlay : dimmer.overlays())
    QVERIFY(!overlay->isVisible());
}

void ScreenDimmerTests::quickChangesEndRight()
{
  // The cursor can cross back before a fade finishes
  ScreenDimmer dimmer;
  dimmer.setDimmed(true);
  dimmer.setDimmed(false);
  dimmer.setDimmed(true);
  waitForFade();
  QVERIFY(dimmer.isDimmed());
  for (auto overlay : dimmer.overlays())
    QVERIFY(overlay->isVisible());
}

QTEST_MAIN(ScreenDimmerTests)
