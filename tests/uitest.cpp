#include "uitest.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <functional>

namespace {

QString g_root;
QString g_out;
int g_failures = 0;

QByteArray fixtureFile(const QString &path) {
  QFile file(g_root + "/" + path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QQuickItem *find(QQuickItem *from, const QString &name) {
  if (from->objectName() == name && from->isVisible())
    return from;
  for (auto *child : from->childItems())
    if (auto *found = find(child, name))
      return found;
  return nullptr;
}

bool waitFor(const std::function<bool()> &condition, int ms = 4000) {
  QElapsedTimer clock;
  clock.start();
  while (clock.elapsed() < ms) {
    if (condition())
      return true;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
  }
  return condition();
}

void check(bool ok, const char *what) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++g_failures;
}

void snap(QQuickWindow *window, const QString &name) {
  QTest::qWait(700); // let the springs settle before the picture
  window->grabWindow().save(g_out + "/" + name + ".png");
}

// Scrolls whatever page holds the item until the item is in view, as a
// person scrolls to what they want to press.
void reveal(QQuickItem *item) {
  for (QQuickItem *up = item->parentItem(); up; up = up->parentItem()) {
    if (!up->inherits("QQuickFlickable"))
      continue;
    auto *content = up->property("contentItem").value<QQuickItem *>();
    const QPointF at = item->mapToItem(content, QPointF(0, 0));
    const double view = up->height();
    const double y = up->property("contentY").toDouble();
    if (at.y() < y || at.y() + item->height() > y + view) {
      const double target = qBound(0.0, at.y() - view / 3, qMax(0.0, up->property("contentHeight").toDouble() - view));
      up->setProperty("contentY", target);
      QTest::qWait(50);
    }
    return;
  }
}

QPoint centre(QQuickItem *item) {
  return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

// Clicks the centre of a named item, as a pointer would.
bool click(QQuickWindow *window, const QString &name) {
  auto *item = find(window->contentItem(), name);
  if (!item) {
    std::printf("      no visible item named %s\n", qPrintable(name));
    return false;
  }
  reveal(item);
  QTest::mouseClick(window, Qt::LeftButton, {}, centre(item));
  return true;
}

// Drags a named slider's handle by a distance, in steps, as a hand would.
bool drag(QQuickWindow *window, const QString &name, QPoint by) {
  auto *item = find(window->contentItem(), name);
  if (!item)
    return false;
  reveal(item);
  auto *handle = item->property("handle").value<QQuickItem *>();
  const QPoint from = handle ? centre(handle) : centre(item);
  QTest::mousePress(window, Qt::LeftButton, {}, from);
  for (int i = 1; i <= 10; ++i)
    QTest::mouseMove(window, from + by * i / 10, 10);
  QTest::mouseRelease(window, Qt::LeftButton, {}, from + by);
  return true;
}

} // namespace

void runUiTest(QQuickWindow *window, const QString &out) {
  g_root = qEnvironmentVariable("COHORT_SYS_ROOT");
  g_out = out;
  QDir().mkpath(out);
  window->resize(1000, 760);
  check(QTest::qWaitForWindowExposed(window), "the window is shown");
  QTest::qWait(500);

  // Power: choosing Custom sets the platform profile and brings the
  // firmware's limits, which only apply there.
  snap(window, "01-power");
  check(click(window, "profile_custom"), "the Custom mode can be clicked");
  check(waitFor([] { return fixtureFile("sys/class/platform-profile/platform-profile-0/profile").trimmed() == "custom"; }),
        "Custom reaches the gamezone handler, not the legacy file that refuses it");
  check(waitFor([&] { return find(window->contentItem(), "limit_ppt_pl1_spl") != nullptr; }),
        "the power limits appear in Custom");
  snap(window, "02-power-custom");
  const QString pl1 = "sys/class/firmware-attributes/lenovo-wmi-other-0/attributes/ppt_pl1_spl/current_value";
  const QByteArray before = fixtureFile(pl1);
  check(drag(window, "limit_ppt_pl1_spl", QPoint(120, 0)), "the sustained power slider can be dragged");
  check(waitFor([&] { return fixtureFile(pl1) != before; }), "a dragged limit reaches current_value");
  check(fixtureFile(pl1).trimmed().toInt() > before.trimmed().toInt(), "dragging right raised the limit");

  // The automatic mode: on battery, turning it on moves to the battery mode.
  check(click(window, "automaticSwitch"), "Follow the charger can be turned on");
  check(waitFor([] { return fixtureFile("sys/firmware/acpi/platform_profile").trimmed() == "low-power"; }),
        "on battery it moves to the battery mode");
  check(find(window->contentItem(), "automaticBattery") != nullptr, "the two modes to follow can be chosen");
  snap(window, "02b-power-automatic");

  // Battery, reached by the rail.
  check(click(window, "rail_battery"), "the rail reaches Battery");
  check(waitFor([&] { return find(window->contentItem(), "charge_Long_Life") != nullptr; }), "Battery is showing");
  check(click(window, "charge_Long_Life"), "Conservation can be clicked");
  check(waitFor([] { return fixtureFile("sys/class/power_supply/BAT0/charge_types").contains("[Long_Life]"); }),
        "Conservation reaches charge_types");
  check(click(window, "switch_usb-charging"), "the always-on USB switch can be clicked");
  check(waitFor([] { return fixtureFile("sys/bus/platform/devices/VPC2004:00/usb_charging").trimmed() == "1"; }),
        "always-on USB reaches usb_charging");
  snap(window, "03-battery");

  // Fans: raising one step of the curve writes both fans' points.
  check(click(window, "rail_fans"), "the rail reaches Fans");
  check(waitFor([&] { return find(window->contentItem(), "curvePoint3") != nullptr; }), "the fan curve is showing");
  const QString point = "sys/class/hwmon/hwmon7/pwm1_auto_point3_pwm";
  const QByteArray speed = fixtureFile(point);
  check(drag(window, "curvePoint3", QPoint(0, -80)), "a curve point can be dragged");
  check(waitFor([&] { return fixtureFile(point) != speed; }), "the dragged point reaches pwm1");
  check(fixtureFile("sys/class/hwmon/hwmon7/pwm2_auto_point3_pwm") == fixtureFile(point),
        "the second fan takes the same point");
  check(fixtureFile(point).trimmed().toInt() > speed.trimmed().toInt(), "dragging up raised the speed");
  snap(window, "04-fans");

  // A step's own thresholds, through its temperature.
  const QString threshold = "sys/class/hwmon/hwmon7/pwm1_auto_point3_temp";
  const QByteArray thresholdBefore = fixtureFile(threshold);
  check(click(window, "curveStep3"), "a step's temperature opens it");
  check(waitFor([&] { return find(window->contentItem(), "step_cpu") != nullptr; }), "the step editor is showing");
  snap(window, "04b-fans-step");
  check(drag(window, "step_cpu", QPoint(60, 0)), "the CPU threshold can be dragged");
  check(click(window, "stepDialog_button_0"), "Save can be pressed");
  check(waitFor([&] { return fixtureFile(threshold).trimmed().toInt() > thresholdBefore.trimmed().toInt(); }),
        "a raised threshold reaches pwm1_auto_point3_temp");
  // The dialog closes before the page under it can be pressed again, as for
  // a person: its scrim holds the pointer until then.
  check(waitFor([&] { return find(window->contentItem(), "step_cpu") == nullptr; }), "the step editor closes");
  // Reset takes the curve back to the firmware's.
  const QByteArray firmwareSpeed = "46";
  check(waitFor([&] { return find(window->contentItem(), "sectionAction") != nullptr; }), "Reset appears once the curve is changed");
  check(click(window, "sectionAction"), "Reset can be pressed");
  check(waitFor([&] { return fixtureFile(point).trimmed() == firmwareSpeed && fixtureFile(threshold) == thresholdBefore; }),
        "Reset puts the firmware's curve back");

  // Keyboard: an effect, then a colour on two zones only.
  check(click(window, "rail_keyboard"), "the rail reaches Keyboard");
  check(waitFor([&] { return find(window->contentItem(), "effect_breath") != nullptr; }), "Keyboard is showing");
  check(click(window, "effect_breath"), "Breathing can be clicked");
  check(waitFor([] { const auto r = fixtureFile("dev/hidraw0"); return r.size() == 33 && quint8(r[2]) == 0x03; }),
        "Breathing reaches the keyboard as effect 0x03");
  check(click(window, "zone1") && click(window, "zone2"), "two zones can be let go of");
  check(click(window, "preset1"), "red can be picked");
  check(waitFor([] {
          const auto r = fixtureFile("dev/hidraw0");
          // Zones 3 and 4 are red; zones 1 and 2 keep their white.
          return r.size() == 33 && quint8(r[11]) == 0xff && quint8(r[12]) == 0 && quint8(r[5]) == 0xff &&
                 quint8(r[6]) == 0xff;
        }),
        "red lands on the chosen zones only");
  check(fixtureFile("dev/hidraw1").isEmpty(), "nothing is sent to the keyboard's other interface");
  snap(window, "05-keyboard");
  check(click(window, "effect_off"), "Off can be clicked");
  check(waitFor([] { return fixtureFile("sys/class/leds/platform::kbd_backlight/brightness").trimmed() == "0"; }),
        "Off turns the keyboard backlight off, as Fn+Space does");
  check(click(window, "switch_legion/winkey"), "the Windows key switch can be clicked");
  check(waitFor([] { return fixtureFile("sys/bus/platform/devices/legion/winkey").trimmed() == "0"; }),
        "the Windows key switch reaches winkey");

  // The keyboard reaches the destinations too.
  QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
  check(waitFor([&] { return window->property("page").toString() == "battery"; }), "Ctrl+2 opens Battery");

  // Compact: the rail gives way to a bar along the bottom.
  window->resize(400, 760);
  check(waitFor([&] { return find(window->contentItem(), "navigationBar") != nullptr; }),
        "a compact window shows the navigation bar");
  check(find(window->contentItem(), "navigationRail") == nullptr, "and hides the rail");
  check(click(window, "bar_power"), "the bar reaches Power");
  check(find(window->contentItem(), "compactSettings") != nullptr, "Settings stays reachable when compact");
  snap(window, "06-compact");

  // Light theme, through Settings.
  window->resize(1000, 760);
  check(waitFor([&] { return find(window->contentItem(), "railSettings") != nullptr; }),
        "a medium window brings the rail back");
  check(click(window, "railSettings"), "Settings opens from the rail");
  // Popups live in the window's overlay, which is under its content item.
  check(waitFor([&] { return find(window->contentItem(), "theme_light") != nullptr; }),
        "Settings shows the theme choice");
  snap(window, "07-settings");
  check(click(window, "size_large"), "a larger size can be chosen");
  check(waitFor([&] { return find(window->contentItem(), "reopenButton") != nullptr; }),
        "a new size offers to reopen");
  check(find(window->contentItem(), "backgroundSwitch") != nullptr, "Run in background is in Settings");
  check(click(window, "theme_light"), "Light can be chosen");
  check(waitFor([&] { return window->color().lightnessF() > 0.9; }), "the window turns light");
  QTest::keyClick(window, Qt::Key_Escape);
  snap(window, "08-light");

  std::printf("%d failure%s\n", g_failures, g_failures == 1 ? "" : "s");
  QCoreApplication::exit(g_failures ? 1 : 0);
}

void runLatencyTest(QQuickWindow *window) {
  window->setProperty("page", "fans");
  QTest::qWait(3000); // the startup reads, including the fan curve, land first
  QElapsedTimer clock;
  qint64 last = 0, worst = 0;
  QList<qint64> gaps;
  clock.start();
  QTimer tick;
  tick.setTimerType(Qt::PreciseTimer);
  tick.setInterval(4);
  QObject::connect(&tick, &QTimer::timeout, [&] {
    const qint64 now = clock.nsecsElapsed();
    if (last)
      gaps << now - last;
    worst = std::max(worst, now - last);
    last = now;
  });
  tick.start();
  QTest::qWait(10000);
  tick.stop();
  std::sort(gaps.begin(), gaps.end());
  const double p99 = gaps.isEmpty() ? 0 : gaps.at(gaps.size() * 99 / 100) / 1e6;
  std::printf("longest stall %.1f ms, 99th percentile %.1f ms, over %lld ticks\n", worst / 1e6, p99,
              qlonglong(gaps.size()));
  QCoreApplication::exit(worst / 1e6 > 50 ? 1 : 0);
}
