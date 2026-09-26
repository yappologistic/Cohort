#include "machine.h"
#include "controls.h"
#include "keyboard.h"
#include <QColor>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFileInfo>
#include <QProcess>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <fcntl.h>
#include <algorithm>
#include <fstream>
#include <optional>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

QString mapDaemonProfile(const QString &profile, const QStringList &choices) {
  // power-profiles-daemon's platform_profile driver maps power-saver to
  // low-power, or to quiet where there is no low-power; balanced and
  // performance are the same words. Nothing else has a daemon profile.
  if (profile == "balanced" || profile == "performance")
    return profile;
  if (profile == "low-power" || (profile == "quiet" && !choices.contains("low-power")))
    return QStringLiteral("power-saver");
  return {};
}

// Which of power-profiles-daemon's two bus names is on this system bus: the
// current one, or the one it had before 0.20, which tuned-ppd also answers.
struct Daemon {
  QString service, path, interface;
};
std::optional<Daemon> powerProfilesDaemon() {
  auto bus = QDBusConnection::systemBus();
  if (!bus.isConnected() || !bus.interface())
    return std::nullopt;
  if (bus.interface()->isServiceRegistered("org.freedesktop.UPower.PowerProfiles"))
    return Daemon{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles",
                  "org.freedesktop.UPower.PowerProfiles"};
  if (bus.interface()->isServiceRegistered("net.hadess.PowerProfiles"))
    return Daemon{"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles"};
  return std::nullopt;
}

QString colourWord(const QVariant &value) {
  const QColor colour(value.toString());
  return (colour.isValid() ? colour : QColor(Qt::white)).name().mid(1);
}

} // namespace

Machine::Machine(fs::path root, QObject *parent)
    : QObject(parent), m_root(std::move(root)), m_fixture(m_root != fs::path("/")),
      m_sensors(m_root) {
  // The product version is the marketing name on Lenovo machines ("Legion 5
  // Pro 16ITH6H"); the product name is the machine type ("82JD").
  m_model = QString::fromStdString(read("sys/class/dmi/id/product_version"));
  if (m_model.isEmpty() || m_model.startsWith("0"))
    m_model = QString::fromStdString(read("sys/class/dmi/id/product_name"));

  m_lighting = {
      {"effect", m_settings.value("lighting/effect", "static").toString()},
      {"speed", m_settings.value("lighting/speed", 2).toInt()},
      {"brightness", m_settings.value("lighting/brightness", 2).toInt()},
      {"direction", m_settings.value("lighting/direction", "right").toString()},
      {"zones", m_settings.value("lighting/zones", QStringList{"#ffffff", "#ffffff", "#ffffff", "#ffffff"})
                    .toStringList()},
  };

  // A refresh two seconds apart is fast enough for a figure people
  // glance at and slow enough to cost nothing measurable; it only runs while
  // the window is visible.
  m_timer.setInterval(2000);
  connect(&m_timer, &QTimer::timeout, this, [this] { requestRead(false); });
  m_lightingDebounce.setSingleShot(true);
  // A colour dragged across a picker sends one report when it rests, not one
  // per frame, and each report is a process under pkexec.
  m_lightingDebounce.setInterval(150);
  connect(&m_lightingDebounce, &QTimer::timeout, this, &Machine::sendLighting);
  // The firmware needs a moment after resuming before the embedded
  // controller answers, as it does after a mode change.
  m_resume.setSingleShot(true);
  m_resume.setInterval(2000);
  connect(&m_resume, &QTimer::timeout, this, &Machine::restore);
  m_powerPoll.setInterval(10000);
  connect(&m_powerPoll, &QTimer::timeout, this, [this] { requestRead(false); });
  // legiond waits a second and a half after a mode change before it writes
  // its curve, because the firmware loads the mode's own curve first
  // (LenovoLegionLinux extra/service/legiond). Writing sooner is overwritten.
  m_curveReapply.setSingleShot(true);
  m_curveReapply.setInterval(1500);
  // The reapply compares against the curve the firmware has now, so it
  // reads the curve first and decides once that read lands.
  connect(&m_curveReapply, &QTimer::timeout, this, [this] { requestRead(true, true); });
  connect(&m_reading, &QFutureWatcherBase::finished, this, [this] {
    const Wanted done = m_inFlight;
    m_inFlight = {};
    apply(m_reading.result());
    for (const auto &key : std::as_const(m_settlingInFlight))
      if (!m_settling.contains(key) && std::none_of(m_queue.cbegin(), m_queue.cend(),
                                                    [&](const Request &r) { return r.key == key; }))
        setPending(key, false);
    m_settlingInFlight.clear();
    if (done.reapply)
      reapplyFanCurve();
    if (m_restoreAfterRead && done.full) {
      m_restoreAfterRead = false;
      restoreLighting();
      applyAutomatic();
    }
    if (m_wanted.read)
      requestRead(m_wanted.full, m_wanted.reapply, m_wanted.rediscover);
  });

  // The first reading is taken here, so the window opens on real figures.
  // It leaves out the full reading's slow part, which follows at once from
  // the worker.
  apply(capture(m_root, m_sensors, false, true));
  // That reading skipped the module's switches; they arrive with the full
  // one below, and until then the rows for them are not shown.
  requestRead(true);
  watchProfile();
}

Machine::~Machine() {
  // The worker holds the sensors; it finishes before they go.
  m_reading.waitForFinished();
  m_profileNotifier.reset();
  if (m_profileFd >= 0)
    ::close(m_profileFd);
}

std::string Machine::read(const fs::path &relative) const {
  return controls::readText(m_root / relative).value_or(std::string());
}

void Machine::setActive(bool active) {
  if (active == m_active)
    return;
  m_active = active;
  if (active) {
    // Coming back is when the figures are most stale, so read at once, and
    // fully: a hotkey may have changed a switch while the window was away.
    requestRead(true);
    m_timer.start();
  } else {
    m_timer.stop();
  }
  emit activeChanged();
}

void Machine::refresh() { requestRead(true, false, true); }

QString Machine::profileName(const QString &profile) const {
  // The Fn+Q modes are Quiet, Balanced and Performance, and max-power is the
  // mode Lenovo calls Extreme.
  if (profile == "low-power" || profile == "quiet")
    return tr("Quiet");
  if (profile == "cool")
    return tr("Cool");
  if (profile == "balanced")
    return tr("Balanced");
  if (profile == "balanced-performance")
    return tr("Balanced+");
  if (profile == "performance")
    return tr("Performance");
  if (profile == "max-power")
    return tr("Extreme");
  if (profile == "custom")
    return tr("Custom");
  return profile;
}

void Machine::requestRead(bool full, bool reapply, bool rediscover) {
  m_wanted.full |= full;
  m_wanted.reapply |= reapply;
  m_wanted.rediscover |= rediscover;
  m_wanted.read = true;
  if (m_reading.isRunning())
    return;
  m_inFlight = m_wanted;
  m_wanted = {};
  m_settlingInFlight = m_settling;
  m_settling.clear();
  const auto root = m_root;
  Sensors *sensors = &m_sensors;
  const Wanted what = m_inFlight;
  m_reading.setFuture(QtConcurrent::run([root, sensors, what] {
    return capture(root, *sensors, what.full, what.rediscover);
  }));
}

void Machine::watchProfile() {
  if (m_fixture)
    return;
  // platform_profile_notify() calls sysfs_notify on this file when the
  // firmware changes mode on its own, as it does for Fn+Q, and sysfs wakes a
  // poll() waiting for POLLPRI on it (Documentation/filesystems/sysfs.rst).
  // QSocketNotifier's exception type is that poll.
  m_profileFd = ::open((m_root / "sys/firmware/acpi/platform_profile").c_str(), O_RDONLY | O_CLOEXEC);
  if (m_profileFd < 0)
    return;
  char buffer[64];
  // The notification is armed by reading the file once.
  [[maybe_unused]] auto armed = ::read(m_profileFd, buffer, sizeof buffer);
  m_profileNotifier = std::make_unique<QSocketNotifier>(m_profileFd, QSocketNotifier::Exception);
  connect(m_profileNotifier.get(), &QSocketNotifier::activated, this, [this] {
    char again[64];
    ::lseek(m_profileFd, 0, SEEK_SET);
    [[maybe_unused]] auto read = ::read(m_profileFd, again, sizeof again);
    requestRead(false);
  });
}

void Machine::apply(const Snapshot &s) {
  const auto previousProfile = m_powerProfile;
  // A reading that was not full did not look at the module's switches; the
  // last values read stand.
  QVariantMap switches = s.switches;
  if (!s.full)
    for (auto it = m_switches.cbegin(); it != m_switches.cend(); ++it)
      if (fullReadOnly(it.key()))
        switches.insert(it.key(), it.value());
  bool differs = s.profile != m_powerProfile || s.profiles != m_powerProfiles || s.limits != m_powerLimits ||
                       s.chargeMode != m_chargeMode || s.chargeModes != m_chargeModes ||
                       switches != m_switches || s.legion != m_legionModule || s.lighting != m_lightingAvailable ||
                       (s.full && s.curve != m_fanCurve);
  m_powerProfile = s.profile;
  m_powerProfiles = s.profiles;
  m_powerLimits = s.limits;
  m_chargeMode = s.chargeMode;
  m_chargeModes = s.chargeModes;
  m_switches = switches;
  m_legionModule = s.legion;
  m_lightingAvailable = s.lighting;
  const bool wasOnCharger = m_battery.value("ac").toBool();
  const bool knewCharger = m_battery.contains("ac");
  if (s.full) {
    m_fanCurve = s.curve;
    differs = differs || s.backlight != m_backlight || s.backlightMax != m_backlightMax;
    m_backlight = s.backlight;
    m_backlightMax = s.backlightMax;
  }
  m_battery = s.battery;
  m_cpu = s.cpu;
  m_gpu = s.gpu;
  m_gpuPresent = s.gpuPresent;
  m_gpuAsleep = s.gpuAsleep;
  m_fans = s.fans;
  if (differs)
    emit changed();
  emit sampled();
  if (!m_restoring)
    return;
  // A mode change makes the firmware load that mode's own fan curve.
  if (!previousProfile.isEmpty() && s.profile != previousProfile && m_legionModule)
    m_curveReapply.start();
  if (knewCharger && wasOnCharger != m_battery.value("ac").toBool())
    applyAutomatic();
}

// --- Changing things ---------------------------------------------------------

void Machine::setPowerProfile(const QString &profile) {
  if (!m_powerProfiles.contains(profile) || profile == m_powerProfile)
    return;
  const auto daemonProfile = mapDaemonProfile(profile, m_powerProfiles);
  if (!m_fixture && !daemonProfile.isEmpty() && powerProfilesDaemon()) {
    setPowerProfileThroughDaemon(profile, daemonProfile);
    return;
  }
  enqueue({"platform-profile", {"set", "platform-profile=" + profile}, tr("the power mode")});
}

void Machine::setPowerProfileThroughDaemon(const QString &profile, const QString &daemonProfile) {
  const auto daemon = *powerProfilesDaemon();
  setPending("platform-profile", true);
  auto message = QDBusMessage::createMethodCall(daemon.service, daemon.path, "org.freedesktop.DBus.Properties", "Set");
  message << daemon.interface << QStringLiteral("ActiveProfile") << QVariant::fromValue(QDBusVariant(daemonProfile));
  auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, profile] {
    watcher->deleteLater();
    QDBusPendingReply<> reply = *watcher;
    // The daemon writes platform_profile itself; this only waits for the
    // kernel to say it has.
    if (reply.isError()) {
      setPending("platform-profile", false);
      // A daemon that refuses leaves the helper, which answers to polkit
      // on its own terms.
      enqueue({"platform-profile", {"set", "platform-profile=" + profile}, tr("the power mode")});
      return;
    }
    if (!m_settling.contains("platform-profile"))
      m_settling << "platform-profile";
    requestRead(true);
  });
}

void Machine::setChargeMode(const QString &mode) {
  if (!m_chargeModes.contains(mode) || mode == m_chargeMode)
    return;
  if (controls::resolve(m_root, "charge-types")) {
    enqueue({"charge", {"set", "charge-types=" + mode}, tr("the charging mode")});
    return;
  }
  // The older interfaces are two switches, and the firmware keeps them
  // exclusive. Whichever is being turned off goes first, so the two are
  // never asked to be on at once.
  QString conservation = controls::resolve(m_root, "conservation-mode") ? "conservation-mode"
                         : controls::resolve(m_root, "legion/battery_conservation") ? "legion/battery_conservation"
                                                                                    : QString();
  const bool rapid = controls::resolve(m_root, "legion/rapidcharge").has_value();
  QStringList off, on;
  if (!conservation.isEmpty())
    (mode == "Long_Life" ? on : off) << conservation + "=" + (mode == "Long_Life" ? "1" : "0");
  if (rapid)
    (mode == "Fast" ? on : off) << QStringLiteral("legion/rapidcharge=") + (mode == "Fast" ? "1" : "0");
  enqueue({"charge", QStringList{"set"} + off + on, tr("the charging mode")});
}

void Machine::setSwitch(const QString &key, bool on) {
  if (!m_switches.contains(key) || m_switches.value(key).toBool() == on)
    return;
  QString value = on ? "1" : "0";
  // An LED is a brightness, and on means as bright as it goes.
  if (key.startsWith("led/") && on)
    if (const auto control = controls::resolve(m_root, key.toStdString()))
      value = QString::number(control->maximum);
  enqueue({key, {"set", key + "=" + value}, tr("that setting")});
}

void Machine::setPowerLimit(const QString &key, int value) {
  for (const auto &entry : m_powerLimits) {
    const auto limit = entry.toMap();
    if (limit.value("key").toString() != key)
      continue;
    if (limit.value("value").toInt() == value)
      return;
    enqueue({key, {"set", key + "=" + QString::number(value)}, limit.value("label").toString().toLower()});
    return;
  }
}

void Machine::setFanSpeeds(const QVariantList &speeds) {
  auto points = m_fanCurve.value("points").toList();
  if (!m_fanCurve.value("available").toBool() || speeds.size() != points.size())
    return;
  for (int i = 0; i < points.size(); ++i) {
    auto point = points.at(i).toMap();
    point["speed"] = speeds.at(i).toInt();
    points[i] = point;
  }
  setFanCurve(points);
}

QVariantList Machine::normalisedCurve(const QVariantList &points) const {
  const auto current = m_fanCurve.value("points").toList();
  QVariantList out;
  // The upper temperature of the step before, per sensor, in the new curve
  // and in the one the firmware has now; the second gives each lower
  // temperature the gap it already keeps below the step before it.
  QVariantMap previous, previousNow;
  for (int i = 0; i < points.size(); ++i) {
    const auto wanted = points.at(i).toMap();
    const auto now = current.value(i).toMap();
    QVariantMap point = now;
    point["speed"] = qBound(0, wanted.value("speed", now.value("speed")).toInt(), 255);
    const bool last = i == points.size() - 1;
    for (const char *sensor : {"cpu", "gpu", "ic"}) {
      const QString low = QString::fromLatin1(sensor) + "Low";
      // legion-laptop.c: temperatures rise from step to step, and the last
      // step's upper temperature is 127, the end of the scale.
      const int floor = i ? previous.value(sensor).toInt() : 0;
      const int upper = last ? 127 : qBound(floor, wanted.value(sensor, now.value(sensor)).toInt(), 127);
      point[sensor] = upper;
      if (i) {
        const int gap = qMax(0, previousNow.value(sensor).toInt() - now.value(low).toInt());
        point[low] = qBound(0, floor - gap, upper);
      }
    }
    for (const char *ramp : {"accel", "decel"})
      if (now.value(ramp).toInt() > 0)
        point[ramp] = qBound(2, wanted.value(ramp, now.value(ramp)).toInt(), 5);
    for (const char *sensor : {"cpu", "gpu", "ic"}) {
      previous[sensor] = point.value(sensor);
      previousNow[sensor] = now.value(sensor);
    }
    out << point;
  }
  return out;
}

void Machine::writeCurve(const QVariantList &points) {
  const auto hwmon = controls::legionHwmon(m_root);
  if (!hwmon)
    return;
  const auto present = [&](const QString &name) { return fs::exists(*hwmon / name.toStdString()); };
  QStringList arguments{"set"};
  // Step by step from the first, so each step's temperatures are written
  // after the ones below them have moved.
  for (int i = 0; i < points.size(); ++i) {
    const auto point = points.at(i).toMap();
    const auto n = QString::number(i + 1);
    const auto add = [&](const QString &name, const QVariant &value) {
      if (present(name))
        arguments << "curve/" + name + "=" + QString::number(value.toInt());
    };
    add("pwm1_auto_point" + n + "_pwm", point.value("speed"));
    add("pwm2_auto_point" + n + "_pwm", point.value("speed"));
    int sensor = 1;
    for (const char *name : {"cpu", "gpu", "ic"}) {
      const auto base = "pwm" + QString::number(sensor++) + "_auto_point" + n;
      add(base + "_temp", point.value(name));
      add(base + "_temp_hyst", point.value(QString::fromLatin1(name) + "Low"));
    }
    if (point.value("accel").toInt() > 0)
      add("pwm1_auto_point" + n + "_accel", point.value("accel"));
    if (point.value("decel").toInt() > 0)
      add("pwm1_auto_point" + n + "_decel", point.value("decel"));
  }
  enqueue({"curve", arguments, tr("the fan curve")});
}

void Machine::setFanCurve(const QVariantList &points) {
  const auto current = m_fanCurve.value("points").toList();
  if (!m_fanCurve.value("available").toBool() || points.size() != current.size() || m_powerProfile.isEmpty())
    return;
  const auto curve = normalisedCurve(points);
  // The firmware keeps one curve per power mode and loads it on every mode
  // change, so a curve is remembered against the mode it was made in, and
  // so is the firmware's own, the first time it is replaced, for Reset.
  if (!m_settings.contains("fanCurveOriginal/" + m_powerProfile))
    m_settings.setValue("fanCurveOriginal/" + m_powerProfile, current);
  m_settings.setValue("fanCurve/" + m_powerProfile, curve);
  writeCurve(curve);
}

void Machine::resetFanCurve() {
  const auto original = m_settings.value("fanCurveOriginal/" + m_powerProfile).toList();
  m_settings.remove("fanCurve/" + m_powerProfile);
  m_settings.remove("fanCurveOriginal/" + m_powerProfile);
  if (!original.isEmpty() && original.size() == m_fanCurve.value("points").toList().size())
    writeCurve(original);
  else
    emit changed();
}

bool Machine::fanCurveCustomized() const { return m_settings.contains("fanCurve/" + m_powerProfile); }

void Machine::reapplyFanCurve() {
  const auto stored = m_settings.value("fanCurve/" + m_powerProfile).toList();
  const auto current = m_fanCurve.value("points").toList();
  if (stored.isEmpty() || !m_fanCurve.value("available").toBool() || stored.size() != current.size())
    return;
  // A curve stored before the full curve could be edited is speeds alone.
  QVariantList wanted;
  for (int i = 0; i < stored.size(); ++i) {
    if (stored.at(i).typeId() == QMetaType::QVariantMap) {
      wanted << stored.at(i);
    } else {
      auto point = current.at(i).toMap();
      point["speed"] = stored.at(i).toInt();
      wanted << point;
    }
  }
  wanted = normalisedCurve(wanted);
  if (wanted != current)
    writeCurve(wanted);
}

void Machine::setLighting(const QVariantMap &lighting) {
  if (!m_lightingAvailable)
    return;
  const bool led = m_backlight >= 0;
  const QString effect = lighting.value("effect").toString();
  // Where the module publishes the backlight, off is the backlight off, the
  // same thing Fn+Space does; the effect and colours chosen stay as they
  // were for when it comes back on.
  if (led && effect == "off") {
    setBacklight(0);
    return;
  }
  bool differs = false;
  for (auto it = lighting.cbegin(); it != lighting.cend(); ++it) {
    if (!m_lighting.contains(it.key()) || m_lighting.value(it.key()) == it.value())
      continue;
    m_lighting.insert(it.key(), it.value());
    m_settings.setValue("lighting/" + it.key(), it.value());
    differs = true;
  }
  m_settings.setValue("lighting/set", true);
  if (led && (m_backlight == 0 || lighting.contains("brightness")))
    setBacklight(qBound(1, m_lighting.value("brightness").toInt(), qMax(1, m_backlightMax)));
  if (differs || (led && effect.size())) {
    emit lightingChanged();
    m_lightingDebounce.start();
  }
}

void Machine::setBacklight(int level) {
  if (m_backlight < 0 || level < 0 || level > m_backlightMax || level == m_backlight)
    return;
  enqueue({"backlight", {"set", "led/platform::kbd_backlight=" + QString::number(level)},
           tr("the keyboard backlight")});
}

void Machine::sendLighting() {
  // Without the module's backlight, off is static black: the protocol has no
  // off of its own, and the colours chosen stay stored for when it comes
  // back on.
  const QString effect = m_lighting.value("effect").toString();
  const bool off = effect == "off";
  QStringList arguments{"lighting", off ? QStringLiteral("static") : effect,
                        QString::number(m_lighting.value("speed").toInt()),
                        QString::number(m_lighting.value("brightness").toInt()),
                        effect == "wave" ? m_lighting.value("direction").toString() : QStringLiteral("none")};
  for (const auto &zone : m_lighting.value("zones").toList())
    arguments << (off ? QStringLiteral("000000") : colourWord(zone));
  enqueue({"lighting", arguments, tr("the keyboard lighting")});
}

void Machine::restoreLighting() {
  // Only a lighting someone chose is put back, and not over a backlight
  // they turned off.
  if (!m_lightingAvailable || !m_settings.value("lighting/set").toBool() || m_backlight == 0)
    return;
  m_lightingDebounce.start();
}

// --- Keeping settings applied -------------------------------------------------

bool Machine::automatic() const { return m_settings.value("automatic/enabled", false).toBool(); }
void Machine::setAutomatic(bool on) {
  if (on == automatic())
    return;
  m_settings.setValue("automatic/enabled", on);
  emit automationChanged();
  if (on && m_restoring)
    applyAutomatic();
}
QString Machine::automaticAc() const {
  const auto chosen = m_settings.value("automatic/ac").toString();
  return m_powerProfiles.contains(chosen) ? chosen
         : m_powerProfiles.contains("performance") ? QStringLiteral("performance") : QString();
}
void Machine::setAutomaticAc(const QString &profile) {
  if (profile == automaticAc() || !m_powerProfiles.contains(profile))
    return;
  m_settings.setValue("automatic/ac", profile);
  emit automationChanged();
  if (m_restoring)
    applyAutomatic();
}
QString Machine::automaticBattery() const {
  const auto chosen = m_settings.value("automatic/battery").toString();
  if (m_powerProfiles.contains(chosen))
    return chosen;
  for (const char *quiet : {"low-power", "quiet"})
    if (m_powerProfiles.contains(quiet))
      return QString::fromLatin1(quiet);
  return {};
}
void Machine::setAutomaticBattery(const QString &profile) {
  if (profile == automaticBattery() || !m_powerProfiles.contains(profile))
    return;
  m_settings.setValue("automatic/battery", profile);
  emit automationChanged();
  if (m_restoring)
    applyAutomatic();
}

void Machine::applyAutomatic() {
  if (!automatic() || !m_battery.contains("ac"))
    return;
  const QString wanted = m_battery.value("ac").toBool() ? automaticAc() : automaticBattery();
  if (!wanted.isEmpty())
    setPowerProfile(wanted);
}

bool Machine::background() const { return m_settings.value("background/enabled", true).toBool(); }
void Machine::setBackground(bool on) {
  if (on == background())
    return;
  m_settings.setValue("background/enabled", on);
  emit backgroundChanged();
}

void Machine::setRestoring(bool restoring) {
  if (restoring == m_restoring)
    return;
  m_restoring = restoring;
  if (m_fixture)
    return;
  auto bus = QDBusConnection::systemBus();
  if (restoring) {
    // The charger coming or going is UPower's OnBattery changing, and
    // resuming is logind's PrepareForSleep(false). Both are signals, so
    // nothing is polled while the machine is left alone.
    const bool upower = bus.connect("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                    "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                                    SLOT(powerSourceChanged()));
    bus.connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                "PrepareForSleep", this, SLOT(preparingForSleep(bool)));
    // Without UPower the charger is looked at every ten seconds instead: one
    // sysfs file, read on the worker.
    if (!upower || !bus.interface()->isServiceRegistered("org.freedesktop.UPower"))
      m_powerPoll.start();
  } else {
    bus.disconnect("org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.DBus.Properties",
                   "PropertiesChanged", this, SLOT(powerSourceChanged()));
    bus.disconnect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                   "PrepareForSleep", this, SLOT(preparingForSleep(bool)));
    m_powerPoll.stop();
    m_curveReapply.stop();
  }
}

void Machine::powerSourceChanged() { requestRead(false); }

void Machine::preparingForSleep(bool start) {
  if (!start)
    m_resume.start();
}

void Machine::restore() {
  m_restoreAfterRead = true;
  requestRead(true, true);
}

// --- The queue ---------------------------------------------------------------

void Machine::setPending(const QString &key, bool on) {
  if (on == m_pending.contains(key))
    return;
  if (on)
    m_pending << key;
  else
    m_pending.removeAll(key);
  emit pendingChanged();
}

void Machine::enqueue(const Request &request) {
  // A request that has not started yet is replaced by a newer one for the
  // same thing: only the last position of a dragged slider matters.
  for (auto &queued : m_queue)
    if (queued.key == request.key) {
      queued = request;
      return;
    }
  m_queue.append(request);
  setPending(request.key, true);
  runNext();
}

QString Machine::helperPath() const {
  // The installed helper is the one the polkit policy names. A build that has
  // not been installed runs the one beside it, which polkit treats as any
  // other program and asks an administrator's password for.
  const QString installed = QStringLiteral(COHORT_HELPER_PATH);
  if (QFileInfo(installed).isExecutable())
    return installed;
  return QCoreApplication::applicationDirPath() + "/cohort-helper";
}

void Machine::runNext() {
  if (m_running || m_queue.isEmpty())
    return;
  m_running = true;
  const Request request = m_queue.takeFirst();
  if (m_fixture) {
    QString error;
    const bool ok = runInFixture(request, &error);
    QTimer::singleShot(0, this, [this, request, ok, error] { finish(request, ok, error); });
    return;
  }
  auto *process = new QProcess(this);
  process->setProgram(QStringLiteral("pkexec"));
  process->setArguments(QStringList{helperPath()} + request.arguments);
  connect(process, &QProcess::errorOccurred, this, [this, process, request](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart)
      return;
    process->deleteLater();
    finish(request, false, tr("Changing %1 needs pkexec, which comes with polkit").arg(request.what));
  });
  connect(process, &QProcess::finished, this, [this, process, request](int code, QProcess::ExitStatus status) {
    process->deleteLater();
    const QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
    if (status != QProcess::NormalExit) {
      finish(request, false, tr("%1 could not be changed").arg(request.what));
      return;
    }
    switch (code) {
    case 0:
      finish(request, true, {});
      return;
    case 126:
    case 127:
      finish(request, false, tr("Permission to change %1 was not given").arg(request.what));
      return;
    case 69:
      finish(request, false, tr("This laptop has no control for %1").arg(request.what));
      return;
    default:
      // EBUSY is how lenovo-wmi-other and legion_laptop refuse a change the
      // current power mode does not allow.
      if (detail.contains("busy", Qt::CaseInsensitive))
        finish(request, false, tr("%1 can only be changed in the Custom power mode").arg(request.what));
      else
        finish(request, false, tr("The firmware did not accept %1").arg(request.what));
      return;
    }
  });
  process->start();
}

bool Machine::runInFixture(const Request &request, QString *error) {
  const auto command = request.arguments.value(0);
  if (command == "set") {
    std::vector<std::pair<controls::Control, std::string>> writes;
    for (const auto &pair : request.arguments.mid(1)) {
      const auto key = pair.section('=', 0, 0).toStdString();
      const auto control = controls::resolve(m_root, key);
      const auto value = control ? controls::validate(*control, pair.section('=', 1).toStdString()) : std::nullopt;
      if (!value) {
        *error = tr("The firmware did not accept %1").arg(request.what);
        return false;
      }
      writes.emplace_back(*control, *value);
    }
    for (const auto &[control, value] : writes) {
      // A fixture has no driver behind it to rewrite the file in its own
      // format, so a choice is stored the way the kernel shows it back.
      std::string shown = value;
      if (control.key == "charge-types") {
        shown.clear();
        for (const auto &word : control.choices)
          shown += (shown.empty() ? "" : " ") + (word == value ? "[" + word + "]" : word);
      }
      if (controls::writeText(controls::target(control, value), shown) != 0) {
        *error = tr("The firmware did not accept %1").arg(request.what);
        return false;
      }
      // The legacy platform_profile sets every handler, so each handler's
      // class device follows it, as platform_profile_store does.
      if (control.key == "platform-profile" && value != "custom") {
        std::error_code ignored;
        for (const auto &handler : std::filesystem::directory_iterator(m_root / "sys/class/platform-profile", ignored))
          controls::writeText(handler.path() / "profile", value);
      }
    }
    return true;
  }
  if (command == "lighting") {
    std::vector<std::string> words;
    for (const auto &word : request.arguments.mid(1))
      words.push_back(word.toStdString());
    const std::vector<std::string_view> views(words.begin(), words.end());
    const auto state = keyboard::parse(views);
    const auto device = keyboard::find(m_root);
    if (!state || !device)
      return false;
    // The fixture's node is a plain file, which takes the report as bytes.
    const auto report = keyboard::report(*state);
    std::ofstream(device->node, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char *>(report.data()), std::streamsize(report.size()));
    return true;
  }
  return false;
}

void Machine::finish(const Request &request, bool ok, const QString &error) {
  m_running = false;
  // The change stays pending until the kernel's answer has been read back.
  if (!m_settling.contains(request.key))
    m_settling << request.key;
  requestRead(request.key == "curve" || request.key == "platform-profile" || request.key == "backlight" ||
              fullReadOnly(request.key));
  if (!ok)
    emit failed(error);
  runNext();
}
