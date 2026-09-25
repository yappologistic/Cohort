// The window-less ways Cohort runs, driven as the desktop drives them: the
// real cohort binary started as a separate process against a fixture
// machine, the way a keybind runs a command and the way login starts the
// agent. What each one did is read back from the fixture's files.
#include "fixture.h"
#include <QProcess>
#include <QSettings>
#include <QTest>

class ProcessTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_config;
  QTemporaryDir m_machine;

  QProcessEnvironment environment() const {
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("COHORT_SYS_ROOT", m_machine.path());
    env.insert("COHORT_CONFIG_DIR", m_config.path());
    env.insert("QT_QPA_PLATFORM", "offscreen");
    return env;
  }

  // Runs one command to the end. Returns its exit code; its output goes to
  // `out` and `err`.
  int cohort(const QStringList &arguments, QString *out = nullptr, QString *err = nullptr) {
    QProcess process;
    process.setProcessEnvironment(environment());
    process.start(COHORT_BINARY, arguments);
    if (!process.waitForFinished(30000)) {
      process.kill();
      return -1;
    }
    if (out)
      *out = QString::fromUtf8(process.readAllStandardOutput());
    if (err)
      *err = QString::fromUtf8(process.readAllStandardError());
    return process.exitCode();
  }

  QByteArray file(const QString &path) { return fixture::read(m_machine.path(), path); }

private slots:
  void init() {
    // A fresh laptop and fresh settings for every case.
    QDir(m_machine.path()).removeRecursively();
    QDir(m_config.path()).removeRecursively();
    QDir().mkpath(m_machine.path());
    QDir().mkpath(m_config.path());
    fixture::withLegion(m_machine.path());
  }

  void reportsStatus() {
    QString out;
    QCOMPARE(cohort({"--status"}, &out), 0);
    QVERIFY2(out.contains("mode      balanced"), qPrintable(out));
    QVERIFY(out.contains("charging  Standard"));
    QVERIFY(out.contains("gpu       asleep"));
    QVERIFY(out.contains("fan 1     2200 rpm"));
    QVERIFY(out.contains("backlight 2 of 2"));
  }

  void changesTheModeByName() {
    QCOMPARE(cohort({"--mode", "performance"}), 0);
    QCOMPARE(file("sys/firmware/acpi/platform_profile"), QByteArray("performance"));
    // Lenovo's word for low-power works too.
    QCOMPARE(cohort({"--mode", "quiet"}), 0);
    QCOMPARE(file("sys/firmware/acpi/platform_profile"), QByteArray("low-power"));
    // Custom goes to the gamezone handler, as it does from the window.
    QCOMPARE(cohort({"--mode", "custom"}), 0);
    QCOMPARE(file("sys/class/platform-profile/platform-profile-0/profile"), QByteArray("custom"));
  }

  void cyclesLikeFnQ() {
    // Quiet, balanced, performance and round again; custom is left out.
    QCOMPARE(cohort({"--mode", "next"}), 0);
    QCOMPARE(file("sys/firmware/acpi/platform_profile"), QByteArray("performance"));
    QCOMPARE(cohort({"--mode", "next"}), 0);
    QCOMPARE(file("sys/firmware/acpi/platform_profile"), QByteArray("low-power"));
  }

  void refusesWordsItDoesNotKnow() {
    QString err;
    QCOMPARE(cohort({"--mode", "turbo"}, nullptr, &err), 64);
    QVERIFY(err.contains("low-power, balanced, performance, custom"));
    QCOMPARE(file("sys/firmware/acpi/platform_profile"), QByteArray("balanced"));
    QCOMPARE(cohort({"--charge", "overnight"}), 64);
    QCOMPARE(cohort({"--backlight", "9"}), 64);
  }

  void changesChargingAndLighting() {
    QCOMPARE(cohort({"--charge", "conservation"}), 0);
    QCOMPARE(file("sys/class/power_supply/BAT0/charge_types"), QByteArray("Fast Standard [Long_Life]"));
    QCOMPARE(cohort({"--lighting", "off"}), 0);
    QCOMPARE(file("sys/class/leds/platform::kbd_backlight/brightness"), QByteArray("0"));
    QCOMPARE(cohort({"--lighting", "wave"}), 0);
    QCOMPARE(file("sys/class/leds/platform::kbd_backlight/brightness"), QByteArray("2"));
    QCOMPARE(fixture::byteAt(m_machine.path(), "dev/hidraw0", 2), 0x04);
    QCOMPARE(cohort({"--backlight", "low"}), 0);
    QCOMPARE(file("sys/class/leds/platform::kbd_backlight/brightness"), QByteArray("1"));
  }

  void agentRestoresOnStartAndRunsOnce() {
    // Lighting chosen earlier, then lost, as a restart loses it.
    QCOMPARE(cohort({"--lighting", "smooth"}), 0);
    fixture::put(m_machine.path(), "dev/hidraw0", "");

    QProcess agent;
    agent.setProcessEnvironment(environment());
    agent.start(COHORT_BINARY, {"--background"});
    QVERIFY(agent.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(fixture::byteAt(m_machine.path(), "dev/hidraw0", 2), 0x06, 10000);
    QCOMPARE(agent.state(), QProcess::Running);

    // A second one, as a desktop that runs autostart and a window that
    // starts one would make, sees the first and leaves.
    QProcess second;
    second.setProcessEnvironment(environment());
    second.start(COHORT_BINARY, {"--background"});
    QVERIFY(second.waitForFinished(10000));
    QCOMPARE(second.exitCode(), 0);
    QCOMPARE(agent.state(), QProcess::Running);

    agent.terminate();
    QVERIFY(agent.waitForFinished(10000));
  }

  void agentStaysOffWhenTurnedOff() {
    {
      QSettings settings(m_config.path() + "/cohort/cohort.conf", QSettings::IniFormat);
      settings.setValue("background/enabled", false);
    }
    QProcess agent;
    agent.setProcessEnvironment(environment());
    agent.start(COHORT_BINARY, {"--background"});
    QVERIFY(agent.waitForFinished(10000));
    QCOMPARE(agent.exitCode(), 0);
  }
};

QTEST_GUILESS_MAIN(ProcessTest)
#include "process_test.moc"
