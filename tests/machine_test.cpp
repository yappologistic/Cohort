// The machine layer, driven the way the window drives it, against fixture
// trees. A fixture Machine writes into its tree through the same resolve and
// validate code the helper runs as root, so what lands in a file here is what
// would land in sysfs.
#include "fixture.h"
#include "machine.h"
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

class MachineTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_config;

  std::filesystem::path root(const QTemporaryDir &dir) { return dir.path().toStdString(); }

private slots:
  void initTestCase() {
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_config.path());
  }
  void init() { QSettings().clear(); }

  void readsTheMainlineMachine() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    QCOMPARE(machine.model(), QString("Legion 5 Pro 16ITH6H"));
    QCOMPARE(machine.powerProfile(), QString("balanced"));
    QCOMPARE(machine.powerProfiles(), QStringList({"low-power", "balanced", "performance", "custom"}));
    QCOMPARE(machine.chargeModes(), QStringList({"Long_Life", "Standard", "Fast"}));
    QCOMPARE(machine.chargeMode(), QString("Standard"));
    QCOMPARE(machine.switches().value("fn-lock"), QVariant(true));
    QCOMPARE(machine.switches().value("usb-charging"), QVariant(false));
    QVERIFY(!machine.switches().contains("legion/winkey"));
    QVERIFY(!machine.legionModule());
    QVERIFY(!machine.fanCurve().value("available").toBool());
    QVERIFY(machine.lightingAvailable());
    QVERIFY(machine.powerLimits().isEmpty());

    const auto battery = machine.battery();
    QCOMPARE(battery.value("percent").toInt(), 76);
    QCOMPARE(battery.value("status").toString(), QString("Discharging"));
    QCOMPARE(battery.value("watts").toDouble(), 14.8);
    QCOMPARE(battery.value("health").toInt(), 90);
    QCOMPARE(battery.value("cycles").toInt(), 287);
    QCOMPARE(battery.value("ac").toBool(), false);

    QCOMPARE(machine.cpuTemperature(), 61.0);
    // The discrete GPU is asleep and is left that way.
    QVERIFY(machine.gpuPresent());
    QVERIFY(machine.gpuAsleep());
    QVERIFY(std::isnan(machine.gpuTemperature()));
  }

  void changesThePowerMode() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    QSignalSpy pending(&machine, &Machine::pendingChanged);
    machine.setPowerProfile("custom");
    QVERIFY(machine.pending().contains("platform-profile"));
    QTRY_COMPARE(machine.powerProfile(), QString("custom"));
    QVERIFY(!machine.pending().contains("platform-profile"));
    // Custom goes to the handler's class device; the legacy file refuses it.
    QCOMPARE(fixture::read(dir.path(), "sys/class/platform-profile/platform-profile-0/profile"), QByteArray("custom"));
    QCOMPARE(fixture::read(dir.path(), "sys/firmware/acpi/platform_profile"), QByteArray("balanced"));
    // Any other mode goes to the legacy file, which sets every handler.
    machine.setPowerProfile("performance");
    QTRY_COMPARE(machine.powerProfile(), QString("performance"));
    QCOMPARE(fixture::read(dir.path(), "sys/firmware/acpi/platform_profile"), QByteArray("performance"));
    // A mode the firmware does not publish is never sent.
    machine.setPowerProfile("max-power");
    QVERIFY(machine.pending().isEmpty());
    QCOMPARE(fixture::read(dir.path(), "sys/firmware/acpi/platform_profile"), QByteArray("performance"));
  }

  void sendsCustomToTheMainlineHandler() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    Machine machine(root(dir));
    machine.setPowerProfile("custom");
    QTRY_COMPARE(machine.powerProfile(), QString("custom"));
    QCOMPARE(fixture::read(dir.path(), "sys/class/platform-profile/platform-profile-0/profile"), QByteArray("custom"));
    QCOMPARE(fixture::read(dir.path(), "sys/class/platform-profile/platform-profile-1/profile"), QByteArray("balanced"));
  }

  void followsAModeChangedElsewhere() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    QSignalSpy changed(&machine, &Machine::changed);
    // Fn+Q changes the mode in firmware. The window reads it on its next
    // pass while it is showing.
    fixture::put(dir.path(), "sys/firmware/acpi/platform_profile", "performance\n");
    machine.setActive(true);
    QTRY_COMPARE(machine.powerProfile(), QString("performance"));
    QVERIFY(changed.count() >= 1);
    machine.setActive(false);
  }

  void changesChargingThroughChargeTypes() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    machine.setChargeMode("Long_Life");
    QTRY_COMPARE(machine.chargeMode(), QString("Long_Life"));
    QCOMPARE(fixture::read(dir.path(), "sys/class/power_supply/BAT0/charge_types"),
             QByteArray("Fast Standard [Long_Life]"));
    // The deprecated switch is left alone where charge_types exists.
    QCOMPARE(fixture::read(dir.path(), "sys/bus/platform/devices/VPC2004:00/conservation_mode"), QByteArray("0"));
  }

  void changesChargingThroughTheOlderSwitches() {
    QTemporaryDir dir;
    fixture::olderKernel(dir.path());
    Machine machine(root(dir));
    QCOMPARE(machine.chargeModes(), QStringList({"Long_Life", "Standard", "Fast"}));
    QCOMPARE(machine.chargeMode(), QString("Long_Life"));
    const QString vpc = "sys/bus/platform/devices/VPC2004:00/conservation_mode";
    const QString rapid = "sys/bus/platform/drivers/legion/PNP0C09:00/rapidcharge";
    machine.setChargeMode("Fast");
    QTRY_COMPARE(machine.chargeMode(), QString("Fast"));
    // Conservation is let go of before rapid charging is asked for; the
    // firmware never holds both.
    QCOMPARE(fixture::read(dir.path(), vpc), QByteArray("0"));
    QCOMPARE(fixture::read(dir.path(), rapid), QByteArray("1"));
    machine.setChargeMode("Standard");
    QTRY_COMPARE(machine.chargeMode(), QString("Standard"));
    QCOMPARE(fixture::read(dir.path(), vpc), QByteArray("0"));
    QCOMPARE(fixture::read(dir.path(), rapid), QByteArray("0"));
  }

  void flipsSwitches() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    Machine machine(root(dir));
    machine.setSwitch("fn-lock", false);
    QTRY_COMPARE(machine.switches().value("fn-lock"), QVariant(false));
    QCOMPARE(fixture::read(dir.path(), "sys/bus/platform/devices/VPC2004:00/fn_lock"), QByteArray("0"));
    machine.setSwitch("legion/winkey", false);
    QTRY_COMPARE(machine.switches().value("legion/winkey"), QVariant(false));
    QCOMPARE(fixture::read(dir.path(), "sys/bus/platform/devices/legion/winkey"), QByteArray("0"));
    // An LED turns on at its full brightness.
    machine.setSwitch("led/platform::ylogo", true);
    QTRY_COMPARE(machine.switches().value("led/platform::ylogo"), QVariant(true));
    QCOMPARE(fixture::read(dir.path(), "sys/class/leds/platform::ylogo/brightness"), QByteArray("1"));
    // A switch this machine does not have is not sent anywhere.
    QTemporaryDir plain;
    fixture::mainline(plain.path());
    Machine mainline(root(plain));
    mainline.setSwitch("legion/winkey", false);
    QVERIFY(mainline.pending().isEmpty());
  }

  void offersOnlyTheLimitsASliderCanSay() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    Machine machine(root(dir));
    const auto limits = machine.powerLimits();
    QCOMPARE(limits.size(), 3);
    QCOMPARE(limits.first().toMap().value("key").toString(), QString("firmware/ppt_pl1_spl"));
    QCOMPARE(limits.first().toMap().value("unit").toString(), QString("W"));
    for (const auto &limit : limits)
      QVERIFY(limit.toMap().value("name").toString() != "gpu_mode");

    machine.setPowerLimit("firmware/ppt_pl1_spl", 95);
    QTRY_VERIFY(machine.pending().isEmpty());
    QCOMPARE(fixture::read(dir.path(), "sys/class/firmware-attributes/lenovo-wmi-other-0/attributes/ppt_pl1_spl/current_value"),
             QByteArray("95"));
    // Past the firmware's maximum: refused, and said so.
    QSignalSpy failed(&machine, &Machine::failed);
    machine.setPowerLimit("firmware/ppt_pl1_spl", 400);
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(fixture::read(dir.path(), "sys/class/firmware-attributes/lenovo-wmi-other-0/attributes/ppt_pl1_spl/current_value"),
             QByteArray("95"));
  }

  void readsAndWritesTheFanCurve() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    Machine machine(root(dir));
    QVERIFY(machine.legionModule());
    // The curve is the slow read, and follows the first reading from the
    // worker rather than holding up the window.
    QVERIFY(!machine.fanCurve().value("available").toBool());
    QTRY_VERIFY(machine.fanCurve().value("available").toBool());
    const auto curve = machine.fanCurve();
    QCOMPARE(curve.value("maxRpm").toInt(), 4400);
    const auto points = curve.value("points").toList();
    QCOMPARE(points.size(), 10);
    QCOMPARE(points.at(2).toMap().value("speed").toInt(), 46);
    QCOMPARE(points.at(2).toMap().value("cpu").toInt(), 60);
    QCOMPARE(points.last().toMap().value("cpu").toInt(), 127);
    QCOMPARE(machine.fans().size(), 2);

    QVariantList speeds;
    for (int i = 0; i < 10; ++i)
      speeds << i * 25;
    machine.setFanSpeeds(speeds);
    QTRY_VERIFY(machine.pending().isEmpty());
    const QString hw = "sys/class/hwmon/hwmon7/";
    // Both fans take the curve.
    QCOMPARE(fixture::read(dir.path(), hw + "pwm1_auto_point4_pwm"), QByteArray("75"));
    QCOMPARE(fixture::read(dir.path(), hw + "pwm2_auto_point4_pwm"), QByteArray("75"));
    QCOMPARE(fixture::read(dir.path(), hw + "pwm1_auto_point10_pwm"), QByteArray("225"));
    // The temperatures are the firmware's and are not touched.
    QCOMPARE(fixture::read(dir.path(), hw + "pwm1_auto_point4_temp"), QByteArray("65"));
    // The change stays pending until its readback lands, so the curve the
    // window shows is already the new one when it ends.
    QCOMPARE(machine.fanCurve().value("points").toList().at(3).toMap().value("speed").toInt(), 75);
    // A curve of the wrong length is not a curve for this machine.
    machine.setFanSpeeds({1, 2, 3});
    QVERIFY(machine.pending().isEmpty());
  }

  void restoresTheCurveAfterTheFirmwareSwapsIt() {
    QTemporaryDir dir;
    fixture::withLegion(dir.path());
    fixture::put(dir.path(), "sys/firmware/acpi/platform_profile", "custom\n");
    Machine machine(root(dir));
    QTRY_VERIFY(machine.fanCurve().value("available").toBool());
    QVariantList speeds;
    for (int i = 0; i < 10; ++i)
      speeds << 100;
    machine.setFanSpeeds(speeds);
    QTRY_VERIFY(machine.pending().isEmpty());

    // A mode change: the firmware loads that mode's curve, then comes back to
    // custom and loads custom's own again.
    const QString hw = "sys/class/hwmon/hwmon7/";
    fixture::put(dir.path(), "sys/firmware/acpi/platform_profile", "performance\n");
    machine.refresh();
    QTRY_COMPARE(machine.powerProfile(), QString("performance"));
    fixture::put(dir.path(), "sys/firmware/acpi/platform_profile", "custom\n");
    for (int i = 1; i <= 10; ++i)
      fixture::put(dir.path(), hw + "pwm1_auto_point" + QString::number(i) + "_pwm", "10\n");
    machine.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(fixture::read(dir.path(), hw + "pwm1_auto_point5_pwm"), QByteArray("100"), 4000);
  }

  void sendsTheLightingItWasAskedFor() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    machine.setLighting({{"effect", "breath"}, {"brightness", 1}, {"speed", 3},
                         {"zones", QStringList{"#ff0000", "#00ff00", "#0000ff", "#ffffff"}}});
    QTRY_COMPARE(fixture::readRaw(dir.path(), "dev/hidraw0").size(), 33);
    auto report = fixture::readRaw(dir.path(), "dev/hidraw0");
    QCOMPARE(quint8(report[0]), quint8(0xcc));
    QCOMPARE(quint8(report[2]), quint8(0x03));
    QCOMPARE(quint8(report[3]), quint8(3));
    QCOMPARE(quint8(report[4]), quint8(1));
    QCOMPARE(quint8(report[5]), quint8(0xff));
    QCOMPARE(quint8(report[9]), quint8(0xff));
    // The report went to the lighting interface, not the other one.
    QCOMPARE(fixture::readRaw(dir.path(), "dev/hidraw1").size(), 0);
    // It is remembered, because the keyboard cannot be asked.
    QCOMPARE(QSettings().value("lighting/effect").toString(), QString("breath"));

    // Off keeps the colours for later and sends black.
    machine.setLighting({{"effect", "off"}});
    QTRY_COMPARE(quint8(fixture::readRaw(dir.path(), "dev/hidraw0")[2]), quint8(0x01));
    report = fixture::readRaw(dir.path(), "dev/hidraw0");
    for (int i = 5; i <= 16; ++i)
      QCOMPARE(quint8(report[i]), quint8(0));
    QCOMPARE(machine.lighting().value("zones").toStringList().first(), QString("#ff0000"));
  }

  void coalescesADraggedColour() {
    QTemporaryDir dir;
    fixture::mainline(dir.path());
    Machine machine(root(dir));
    QSignalSpy pending(&machine, &Machine::pendingChanged);
    // A drag across the hue slider: many changes, one report at rest.
    for (int hue = 0; hue < 30; ++hue)
      machine.setLighting({{"zones", QStringList(4, QColor::fromHsv(hue * 12, 255, 255).name())}});
    QTRY_VERIFY(fixture::readRaw(dir.path(), "dev/hidraw0").size() == 33);
    QTRY_VERIFY(machine.pending().isEmpty());
    // Pending went on once and off once.
    QCOMPARE(pending.count(), 2);
    const auto report = fixture::readRaw(dir.path(), "dev/hidraw0");
    const QColor last = QColor::fromHsv(29 * 12, 255, 255);
    QCOMPARE(quint8(report[5]), quint8(last.red()));
    QCOMPARE(quint8(report[7]), quint8(last.blue()));
  }
};

QTEST_GUILESS_MAIN(MachineTest)
#include "machine_test.moc"
