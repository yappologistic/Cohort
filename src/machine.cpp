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
#include <cmath>
#include <fcntl.h>
#include <algorithm>
#include <fstream>
#include <optional>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// The on/off controls the window can show, by the key the helper knows them
// by. Their labels live with the pages that show them.
const char *const kSwitchKeys[] = {
    "fn-lock",           "usb-charging",          "legion/winkey",    "legion/touchpad",
    "legion/overdrive",  "legion/gsync",          "legion/fan_fullspeed", "legion/lockfancontroller",
    "led/platform::ylogo", "led/platform::ioport",
};

// The firmware power limits worth a slider, from the attributes
// lenovo-wmi-other publishes (Documentation/wmi/devices/lenovo-wmi-other.rst).
// Each carries the unit its value is in. The rest of what the driver
// publishes are modes and identifiers written as integers, which a slider
// would misrepresent, and are left alone.
struct Limit {
  const char *name;
  const char *label;
  const char *unit;
};
const Limit kLimits[] = {
    {"ppt_pl1_spl", "Sustained CPU power", "W"},
    {"ppt_pl2_sppt", "Short-term CPU power", "W"},
    {"ppt_pl3_fppt", "Peak CPU power", "W"},
    {"ppt_pl4_ipl", "Instantaneous CPU power", "W"},
    {"ppt_pl1_tau", "Sustained power window", "s"},
    {"cpu_temp", "CPU temperature limit", "°C"},
    {"gpu_nv_ctgp", "GPU power", "W"},
    {"gpu_nv_ppab", "GPU boost power", "W"},
    {"gpu_temp", "GPU temperature limit", "°C"},
};

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
  connect(&m_timer, &QTimer::timeout, this, [this] {
    readControls();
    sample();
  });
  m_lightingDebounce.setSingleShot(true);
  // A colour dragged across a picker sends one report when it rests, not one
  // per frame, and each report is a process under pkexec.
  m_lightingDebounce.setInterval(150);
  connect(&m_lightingDebounce, &QTimer::timeout, this, [this] {
    const bool off = m_lighting.value("effect").toString() == "off";
    QStringList arguments{"lighting", off ? "static" : m_lighting.value("effect").toString(),
                          QString::number(m_lighting.value("speed").toInt()),
                          QString::number(m_lighting.value("brightness").toInt()),
                          m_lighting.value("effect").toString() == "wave" ? m_lighting.value("direction").toString()
                                                                          : QStringLiteral("none")};
    // Off is static black: the protocol has no off of its own, and the
    // colours chosen stay stored for when it comes back on.
    for (const auto &zone : m_lighting.value("zones").toList())
      arguments << (off ? QStringLiteral("000000") : colourWord(zone));
    enqueue({"lighting", arguments, tr("the keyboard lighting")});
  });
  // legiond waits a second and a half after a mode change before it writes
  // its curve, because the firmware loads the mode's own curve first
  // (LenovoLegionLinux extra/service/legiond). Writing sooner is overwritten.
  m_curveReapply.setSingleShot(true);
  m_curveReapply.setInterval(1500);
  connect(&m_curveReapply, &QTimer::timeout, this, &Machine::reapplyFanCurve);

  m_sensors.discover();
  readControls();
  sample();
  watchProfile();
}

Machine::~Machine() {
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
    // Coming back is when the figures are most stale, so read at once.
    readControls();
    sample();
    m_timer.start();
  } else {
    m_timer.stop();
  }
  emit activeChanged();
}

void Machine::refresh() {
  m_sensors.discover();
  readControls();
  sample();
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
    readControls();
  });
}

void Machine::readControls() {
  const auto previousProfile = m_powerProfile;
  QString profile;
  QStringList profiles;
  if (const auto control = controls::resolve(m_root, "platform-profile")) {
    for (const auto &word : control->choices)
      profiles << QString::fromStdString(word);
    profile = QString::fromStdString(controls::parseSelected(read("sys/firmware/acpi/platform_profile")));
  }

  QVariantList limits;
  for (const auto &attribute : controls::firmwareAttributes(m_root)) {
    const auto name = attribute.filename().string();
    for (const auto &limit : kLimits) {
      if (name != limit.name)
        continue;
      const auto control = controls::resolve(m_root, "firmware/" + name);
      const auto value = controls::parseInteger(controls::readText(attribute / "current_value").value_or(""));
      if (!control || !value)
        continue;
      limits.append(QVariantMap{{"key", QString::fromStdString(control->key)},
                                {"name", QString::fromLatin1(limit.name)},
                                {"label", tr(limit.label)},
                                {"unit", QString::fromUtf8(limit.unit)},
                                {"value", int(*value)},
                                {"minimum", int(control->minimum)},
                                {"maximum", int(control->maximum)},
                                {"step", int(control->step)}});
    }
  }

  // Charging: the charge_types ABI where the kernel offers it, and the older
  // pair of switches it replaced where it does not (conservation mode from
  // ideapad-laptop or legion_laptop, rapid charge from legion_laptop).
  QString mode;
  QStringList modes;
  if (const auto control = controls::resolve(m_root, "charge-types")) {
    for (const char *word : {"Long_Life", "Standard", "Fast"})
      if (std::find(control->choices.begin(), control->choices.end(), word) != control->choices.end())
        modes << word;
    mode = QString::fromStdString(controls::parseSelected(*controls::readText(control->path)));
  } else {
    auto conservation = controls::resolve(m_root, "conservation-mode");
    if (!conservation)
      conservation = controls::resolve(m_root, "legion/battery_conservation");
    const auto rapid = controls::resolve(m_root, "legion/rapidcharge");
    if (conservation)
      modes << "Long_Life";
    if (conservation || rapid)
      modes << "Standard";
    if (rapid)
      modes << "Fast";
    mode = QStringLiteral("Standard");
    if (conservation && controls::readText(conservation->path).value_or("0") == "1")
      mode = QStringLiteral("Long_Life");
    else if (rapid && controls::readText(rapid->path).value_or("0") == "1")
      mode = QStringLiteral("Fast");
    if (modes.isEmpty())
      mode.clear();
  }

  QVariantMap switches;
  for (const char *key : kSwitchKeys) {
    const auto control = controls::resolve(m_root, key);
    if (!control)
      continue;
    const auto value = controls::parseInteger(controls::readText(control->path).value_or(""));
    if (value)
      switches.insert(key, *value > 0);
  }

  const bool legion = controls::legionDevice(m_root).has_value();
  const bool lighting = keyboard::find(m_root).has_value();

  const bool differs = profile != m_powerProfile || profiles != m_powerProfiles || limits != m_powerLimits ||
                       mode != m_chargeMode || modes != m_chargeModes || switches != m_switches ||
                       legion != m_legionModule || lighting != m_lightingAvailable;
  m_powerProfile = profile;
  m_powerProfiles = profiles;
  m_powerLimits = limits;
  m_chargeMode = mode;
  m_chargeModes = modes;
  m_switches = switches;
  m_legionModule = legion;
  m_lightingAvailable = lighting;
  const auto curve = m_fanCurve;
  readFanCurve();
  if (differs || curve != m_fanCurve)
    emit changed();
  if (!previousProfile.isEmpty() && profile != previousProfile)
    m_curveReapply.start();
}

void Machine::readFanCurve() {
  QVariantMap curve{{"available", false}};
  const auto hwmon = controls::legionHwmon(m_root);
  if (hwmon) {
    const auto text = [](const fs::path &path) { return controls::readText(path).value_or(std::string()); };
    int size = int(controls::parseInteger(text(*hwmon / "auto_points_size")).value_or(0));
    if (size <= 0)
      while (size < 10 && fs::exists(*hwmon / ("pwm1_auto_point" + std::to_string(size + 1) + "_pwm")))
        ++size;
    QVariantList points;
    for (int point = 1; point <= size; ++point) {
      const auto base = "_auto_point" + std::to_string(point) + "_";
      const auto speed = controls::parseInteger(text(*hwmon / ("pwm1" + base + "pwm")));
      if (!speed)
        break;
      points.append(QVariantMap{
          {"speed", int(*speed)},
          {"cpu", int(controls::parseInteger(text(*hwmon / ("pwm1" + base + "temp"))).value_or(0))},
          {"gpu", int(controls::parseInteger(text(*hwmon / ("pwm2" + base + "temp"))).value_or(0))}});
    }
    if (!points.isEmpty()) {
      curve["available"] = true;
      curve["points"] = points;
      curve["maxRpm"] = int(controls::parseInteger(text(*hwmon / "fan1_max")).value_or(0));
    }
  }
  m_fanCurve = curve;
}

void Machine::readBattery() {
  QVariantMap battery{{"present", false}};
  const auto bat = controls::battery(m_root);
  if (bat) {
    const auto number = [&](const char *name) {
      return controls::parseInteger(controls::readText(*bat / name).value_or(""));
    };
    battery["present"] = true;
    battery["percent"] = int(number("capacity").value_or(0));
    battery["status"] = QString::fromStdString(controls::readText(*bat / "status").value_or(""));
    // power_now is microwatts; a battery that reports current instead gives
    // microamps and microvolts (Documentation/ABI/testing/sysfs-class-power).
    double watts = 0;
    if (const auto power = number("power_now"))
      watts = *power / 1e6;
    else if (const auto current = number("current_now"), voltage = number("voltage_now"); current && voltage)
      watts = double(*current) * double(*voltage) / 1e12;
    battery["watts"] = std::round(std::abs(watts) * 10) / 10;
    auto full = number("energy_full"), design = number("energy_full_design");
    if (!full || !design) {
      full = number("charge_full");
      design = number("charge_full_design");
    }
    if (full && design && *design > 0)
      battery["health"] = int(std::lround(100.0 * double(*full) / double(*design)));
    if (const auto cycles = number("cycle_count"); cycles && *cycles > 0)
      battery["cycles"] = int(*cycles);
  }
  bool ac = false;
  std::error_code error;
  for (const auto &supply : fs::directory_iterator(m_root / "sys/class/power_supply", error))
    if (controls::readText(supply.path() / "type").value_or("") == "Mains" &&
        controls::readText(supply.path() / "online").value_or("0") == "1")
      ac = true;
  battery["ac"] = ac;
  m_battery = battery;
}

void Machine::sample() {
  readBattery();
  m_sensors.sample();
  emit sampled();
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
    setPending("platform-profile", false);
    if (reply.isError()) {
      // A daemon that refuses leaves the helper, which answers to polkit
      // on its own terms.
      enqueue({"platform-profile", {"set", "platform-profile=" + profile}, tr("the power mode")});
      return;
    }
    readControls();
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
  const auto points = m_fanCurve.value("points").toList();
  if (!m_fanCurve.value("available").toBool() || speeds.size() != points.size())
    return;
  const auto hwmon = controls::legionHwmon(m_root);
  if (!hwmon)
    return;
  QStringList arguments{"set"};
  QVariantList stored;
  for (int i = 0; i < speeds.size(); ++i) {
    const int speed = qBound(0, speeds.at(i).toInt(), 255);
    stored << speed;
    const auto point = QString::number(i + 1);
    arguments << "curve/pwm1_auto_point" + point + "_pwm=" + QString::number(speed);
    if (fs::exists(*hwmon / ("pwm2_auto_point" + point.toStdString() + "_pwm")))
      arguments << "curve/pwm2_auto_point" + point + "_pwm=" + QString::number(speed);
  }
  // The firmware keeps one curve per power mode and loads it on every mode
  // change, so a curve is remembered against the mode it was made in.
  if (!m_powerProfile.isEmpty())
    m_settings.setValue("fanCurve/" + m_powerProfile, stored);
  enqueue({"curve", arguments, tr("the fan curve")});
}

void Machine::reapplyFanCurve() {
  const auto stored = m_settings.value("fanCurve/" + m_powerProfile).toList();
  if (stored.isEmpty() || !m_fanCurve.value("available").toBool())
    return;
  QVariantList current;
  for (const auto &point : m_fanCurve.value("points").toList())
    current << point.toMap().value("speed").toInt();
  QVariantList wanted;
  for (const auto &speed : stored)
    wanted << speed.toInt();
  if (wanted != current)
    setFanSpeeds(wanted);
}

void Machine::setLighting(const QVariantMap &lighting) {
  if (!m_lightingAvailable)
    return;
  bool differs = false;
  for (auto it = lighting.cbegin(); it != lighting.cend(); ++it) {
    if (!m_lighting.contains(it.key()) || m_lighting.value(it.key()) == it.value())
      continue;
    m_lighting.insert(it.key(), it.value());
    m_settings.setValue("lighting/" + it.key(), it.value());
    differs = true;
  }
  if (!differs)
    return;
  emit lightingChanged();
  m_lightingDebounce.start();
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
      if (controls::writeText(control.path, shown) != 0) {
        *error = tr("The firmware did not accept %1").arg(request.what);
        return false;
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
  if (std::none_of(m_queue.cbegin(), m_queue.cend(), [&](const Request &r) { return r.key == request.key; }))
    setPending(request.key, false);
  readControls();
  if (!ok)
    emit failed(error);
  runNext();
}
