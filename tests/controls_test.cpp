// The rules the helper applies before it writes anything as root, and the
// keyboard report it builds. These are what stand between an unprivileged
// request and a root write, so the cases that matter most are the refusals:
// a key that names no control, a path smuggled into a key, a value the driver
// does not publish, a number off its step.
#include "controls.h"
#include "fixture.h"
#include "keyboard.h"
#include <QTest>

class ControlsTest : public QObject {
  Q_OBJECT

  std::filesystem::path root(const QTemporaryDir &dir) { return dir.path().toStdString(); }

private slots:
  void parsesChoices() {
    const auto words = controls::parseChoices("Fast [Standard] Long_Life\n");
    QCOMPARE(words.size(), size_t(3));
    QCOMPARE(QString::fromStdString(words[1]), QString("Standard"));
    QCOMPARE(QString::fromStdString(controls::parseSelected("Fast [Standard] Long_Life")), QString("Standard"));
    // platform_profile shows only the current word, with no brackets.
    QCOMPARE(QString::fromStdString(controls::parseSelected("balanced\n")), QString("balanced"));
    // Two unbracketed words say nothing about which one is current.
    QVERIFY(controls::parseSelected("low-power balanced").empty());
  }

  void parsesIntegers() {
    QCOMPARE(controls::parseInteger(" 42\n").value_or(-1), 42L);
    QCOMPARE(controls::parseInteger("-5").value_or(0), -5L);
    QVERIFY(!controls::parseInteger("12abc"));
    QVERIFY(!controls::parseInteger(""));
    QVERIFY(!controls::parseInteger("0x10"));
  }

  void refusesNamesThatAreNotNames() {
    QVERIFY(controls::isAttributeName("ppt_pl1_spl"));
    QVERIFY(!controls::isAttributeName("../../../etc/shadow"));
    QVERIFY(!controls::isAttributeName("a/b"));
    QVERIFY(!controls::isAttributeName("name.with.dots"));
    QVERIFY(!controls::isAttributeName(""));
  }

  void resolvesTheMainlineDrivers() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    const auto profile = controls::resolve(root(dir), "platform-profile");
    QVERIFY(profile);
    QCOMPARE(profile->kind, controls::Kind::Choice);
    QCOMPARE(profile->choices.size(), size_t(4));
    // Custom is written to the handler's own class device.
    QCOMPARE(QString::fromStdString(controls::target(*profile, "custom").string()),
             dir.path() + "/sys/class/platform-profile/platform-profile-0/profile");
    QCOMPARE(QString::fromStdString(controls::target(*profile, "balanced").string()),
             dir.path() + "/sys/firmware/acpi/platform_profile");
    const auto charge = controls::resolve(root(dir), "charge-types");
    QVERIFY(charge);
    QCOMPARE(charge->choices.size(), size_t(3));
    for (const char *key : {"fn-lock", "usb-charging", "conservation-mode"}) {
      const auto control = controls::resolve(root(dir), key);
      QVERIFY2(control, key);
      QCOMPARE(control->kind, controls::Kind::Toggle);
    }
    // Nothing of legion_laptop's exists on this machine.
    QVERIFY(!controls::resolve(root(dir), "legion/winkey"));
    QVERIFY(!controls::resolve(root(dir), "curve/pwm1_auto_point1_pwm"));
    QVERIFY(!controls::legionDevice(root(dir)));
  }

  void findsTheModuleOnEitherDevice() {
    // Kernel 7.0 and later: the virtual "legion" platform device.
    QTemporaryDir current;
    fixture::withLegion(current.path());
    QCOMPARE(QString::fromStdString(controls::legionDevice(root(current))->filename().string()), QString("legion"));
    // Before 7.0: bound to the embedded controller's PNP0C09 device.
    QTemporaryDir older;
    fixture::olderKernel(older.path());
    QCOMPARE(QString::fromStdString(controls::legionDevice(root(older))->filename().string()), QString("PNP0C09:00"));
    QVERIFY(controls::resolve(root(older), "legion/rapidcharge"));
  }

  void refusesKeysOutsideTheList() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    // powermode exists on the device but is not something the helper writes:
    // the platform profile is how a mode is set.
    QVERIFY(!controls::resolve(root(dir), "legion/powermode"));
    QVERIFY(!controls::resolve(root(dir), "legion/../../../etc/passwd"));
    QVERIFY(!controls::resolve(root(dir), "firmware/../lenovo-wmi-other-0/attributes/ppt_pl1_spl"));
    QVERIFY(!controls::resolve(root(dir), "led/../../../../etc"));
    QVERIFY(!controls::resolve(root(dir), "led/input4::capslock"));
    QVERIFY(!controls::resolve(root(dir), "curve/fan1_input"));
    QVERIFY(!controls::resolve(root(dir), "curve/pwm1_auto_point11_pwm"));
    QVERIFY(!controls::resolve(root(dir), "curve/pwm1_auto_point0_pwm"));
    // Only the first fan has ramp times.
    QVERIFY(!controls::resolve(root(dir), "curve/pwm2_auto_point1_accel"));
    // The chipset sensor has temperatures and no speed of its own.
    QVERIFY(!controls::resolve(root(dir), "curve/pwm3_auto_point1_pwm"));
    QVERIFY(!controls::resolve(root(dir), "/etc/passwd"));
    QVERIFY(!controls::resolve(root(dir), ""));
  }

  void validatesToggles() {
    controls::Control control;
    control.kind = controls::Kind::Toggle;
    QCOMPARE(QString::fromStdString(*controls::validate(control, "on")), QString("1"));
    QCOMPARE(QString::fromStdString(*controls::validate(control, "false")), QString("0"));
    QVERIFY(!controls::validate(control, "2"));
    QVERIFY(!controls::validate(control, "1\n0"));
  }

  void validatesChoicesExactly() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    const auto profile = *controls::resolve(root(dir), "platform-profile");
    QCOMPARE(QString::fromStdString(*controls::validate(profile, "custom")), QString("custom"));
    QVERIFY(!controls::validate(profile, "max-power"));
    QVERIFY(!controls::validate(profile, "Balanced"));
    QVERIFY(!controls::validate(profile, "balanced performance"));
    const auto charge = *controls::resolve(root(dir), "charge-types");
    QVERIFY(controls::validate(charge, "Long_Life"));
    // The brackets are how the kernel marks the current word, not part of it.
    QVERIFY(!controls::validate(charge, "[Standard]"));
  }

  void validatesIntegersOnTheirStep() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    const auto limit = *controls::resolve(root(dir), "firmware/ppt_pl1_spl");
    QCOMPARE(limit.minimum, 15L);
    QCOMPARE(limit.maximum, 130L);
    QVERIFY(controls::validate(limit, "15"));
    QVERIFY(controls::validate(limit, "130"));
    QVERIFY(!controls::validate(limit, "14"));
    QVERIFY(!controls::validate(limit, "131"));
    QVERIFY(!controls::validate(limit, "80.5"));

    const auto speed = *controls::resolve(root(dir), "curve/pwm2_auto_point10_pwm");
    QVERIFY(controls::validate(speed, "255"));
    QVERIFY(!controls::validate(speed, "256"));
    const auto ramp = *controls::resolve(root(dir), "curve/pwm1_auto_point3_accel");
    QVERIFY(!controls::validate(ramp, "1"));
    QVERIFY(controls::validate(ramp, "5"));
    const auto temp = *controls::resolve(root(dir), "curve/pwm3_auto_point10_temp");
    QVERIFY(controls::validate(temp, "127"));
    QVERIFY(!controls::validate(temp, "128"));

    const auto logo = *controls::resolve(root(dir), "led/platform::ylogo");
    QCOMPARE(logo.maximum, 1L);
    QVERIFY(!controls::validate(logo, "2"));
  }

  void writesOneValue() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    const auto control = *controls::resolve(root(dir), "fn-lock");
    QCOMPARE(controls::writeText(control.path, "0"), 0);
    QCOMPARE(fixture::read(dir.path(), "sys/bus/platform/devices/VPC2004:00/fn_lock"), QByteArray("0"));
    QVERIFY(controls::writeText(root(dir) / "missing/attribute", "1") != 0);
  }

  void buildsTheKeyboardReport() {
    keyboard::Lighting state;
    state.effect = keyboard::Effect::Static;
    state.speed = 2;
    state.brightness = 1;
    state.zones = {0xff0000, 0x00ff00, 0x0000ff, 0x123456};
    const auto report = keyboard::report(state);
    QCOMPARE(report.size(), size_t(33));
    QCOMPARE(report[0], std::uint8_t(0xcc));
    QCOMPARE(report[1], std::uint8_t(0x16));
    QCOMPARE(report[2], std::uint8_t(0x01));
    QCOMPARE(report[3], std::uint8_t(2));
    QCOMPARE(report[4], std::uint8_t(1));
    // Zone 0 is the left of the keyboard, as R, G, B.
    QCOMPARE(report[5], std::uint8_t(0xff));
    QCOMPARE(report[9], std::uint8_t(0xff));
    QCOMPARE(report[13], std::uint8_t(0xff));
    QCOMPARE(report[14], std::uint8_t(0x12));
    QCOMPARE(report[15], std::uint8_t(0x34));
    QCOMPARE(report[16], std::uint8_t(0x56));
    for (size_t i = 17; i < report.size(); ++i)
      QCOMPARE(report[i], std::uint8_t(0));
  }

  void sendsNoColoursForTheCyclingEffects() {
    keyboard::Lighting state;
    state.effect = keyboard::Effect::Smooth;
    state.zones = {0xffffff, 0xffffff, 0xffffff, 0xffffff};
    const auto report = keyboard::report(state);
    QCOMPARE(report[2], std::uint8_t(0x06));
    for (size_t i = 5; i <= 16; ++i)
      QCOMPARE(report[i], std::uint8_t(0));
  }

  void setsTheWaveDirection() {
    keyboard::Lighting state;
    state.effect = keyboard::Effect::Wave;
    state.direction = *keyboard::parseDirection("right");
    auto report = keyboard::report(state);
    QCOMPARE(report[2], std::uint8_t(0x04));
    QCOMPARE(report[18], std::uint8_t(1));
    QCOMPARE(report[19], std::uint8_t(0));
    state.direction = *keyboard::parseDirection("left");
    report = keyboard::report(state);
    QCOMPARE(report[18], std::uint8_t(0));
    QCOMPARE(report[19], std::uint8_t(1));
  }

  void parsesHelperArguments() {
    const auto good = keyboard::parse({"breath", "4", "2", "none", "ff0000", "00ff00", "0000ff", "ffffff"});
    QVERIFY(good);
    QCOMPARE(good->effect, keyboard::Effect::Breath);
    QCOMPARE(good->zones[2], std::uint32_t(0x0000ff));
    QVERIFY(!keyboard::parse({"breath", "5", "2", "none", "ff0000", "00ff00", "0000ff", "ffffff"}));
    QVERIFY(!keyboard::parse({"breath", "4", "3", "none", "ff0000", "00ff00", "0000ff", "ffffff"}));
    QVERIFY(!keyboard::parse({"strobe", "4", "2", "none", "ff0000", "00ff00", "0000ff", "ffffff"}));
    QVERIFY(!keyboard::parse({"static", "1", "2", "none", "#ff0000", "00ff00", "0000ff", "ffffff"}));
    QVERIFY(!keyboard::parse({"static", "1", "2", "none", "ff0000", "00ff00", "0000ff"}));
  }

  void findsTheLightingInterface() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    // Both interfaces carry the keyboard's id; only one opens page 0xFF89.
    fixture::put(dir.path(), "sys/class/hidraw/hidraw0/device/report_descriptor", fixture::otherDescriptor());
    fixture::put(dir.path(), "sys/class/hidraw/hidraw1/device/report_descriptor", fixture::lightingDescriptor());
    const auto device = keyboard::find(root(dir));
    QVERIFY(device);
    QCOMPARE(QString::fromStdString(device->node.filename().string()), QString("hidraw1"));
    QCOMPARE(device->product, std::uint16_t(0xc965));
  }

  void ignoresOtherKeyboards() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    fixture::put(dir.path(), "sys/class/hidraw/hidraw0/device/uevent", "HID_ID=0003:0000048D:0000C101\n");
    fixture::put(dir.path(), "sys/class/hidraw/hidraw1/device/uevent", "HID_ID=0003:000024AE:0000C965\n");
    QVERIFY(!keyboard::find(root(dir)));
    QVERIFY(keyboard::isKnownProduct(0xc995));
    QVERIFY(!keyboard::isKnownProduct(0xc101));
  }
};

QTEST_GUILESS_MAIN(ControlsTest)
#include "controls_test.moc"
