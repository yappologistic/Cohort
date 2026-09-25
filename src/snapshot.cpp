#include "snapshot.h"
#include "controls.h"
#include "keyboard.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

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

QVariantMap readCurve(const fs::path &root) {
  QVariantMap curve{{"available", false}};
  const auto hwmon = controls::legionHwmon(root);
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
  return curve;
}

QVariantMap readBattery(const fs::path &root) {
  QVariantMap battery{{"present", false}};
  const auto bat = controls::battery(root);
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
  for (const auto &supply : fs::directory_iterator(root / "sys/class/power_supply", error))
    if (controls::readText(supply.path() / "type").value_or("") == "Mains" &&
        controls::readText(supply.path() / "online").value_or("0") == "1")
      ac = true;
  battery["ac"] = ac;
  return battery;
}

} // namespace

bool fullReadOnly(const QString &key) { return key.startsWith("legion/") || key.startsWith("led/"); }

Snapshot capture(const fs::path &root, Sensors &sensors, bool full, bool rediscover) {
  Snapshot s;
  QString profile;
  QStringList profiles;
  if (const auto control = controls::resolve(root, "platform-profile")) {
    for (const auto &word : control->choices)
      profiles << QString::fromStdString(word);
    profile = QString::fromStdString(controls::currentProfile(root));
  }

  QVariantList limits;
  for (const auto &attribute : controls::firmwareAttributes(root)) {
    const auto name = attribute.filename().string();
    for (const auto &limit : kLimits) {
      if (name != limit.name)
        continue;
      const auto control = controls::resolve(root, "firmware/" + name);
      const auto value = controls::parseInteger(controls::readText(attribute / "current_value").value_or(""));
      if (!control || !value)
        continue;
      limits.append(QVariantMap{{"key", QString::fromStdString(control->key)},
                                {"name", QString::fromLatin1(limit.name)},
                                {"label", QCoreApplication::translate("Machine", limit.label)},
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
  if (const auto control = controls::resolve(root, "charge-types")) {
    for (const char *word : {"Long_Life", "Standard", "Fast"})
      if (std::find(control->choices.begin(), control->choices.end(), word) != control->choices.end())
        modes << word;
    mode = QString::fromStdString(controls::parseSelected(*controls::readText(control->path)));
  } else {
    auto conservation = controls::resolve(root, "conservation-mode");
    if (!conservation)
      conservation = controls::resolve(root, "legion/battery_conservation");
    const auto rapid = controls::resolve(root, "legion/rapidcharge");
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
    if (!full && fullReadOnly(QString::fromLatin1(key)))
      continue;
    const auto control = controls::resolve(root, key);
    if (!control)
      continue;
    const auto value = controls::parseInteger(controls::readText(control->path).value_or(""));
    if (value)
      switches.insert(key, *value > 0);
  }

  const bool legion = controls::legionDevice(root).has_value();
  const bool lighting = keyboard::find(root).has_value();

  s.profile = profile;
  s.profiles = profiles;
  s.limits = limits;
  s.chargeMode = mode;
  s.chargeModes = modes;
  s.switches = switches;
  s.legion = controls::legionDevice(root).has_value();
  s.lighting = keyboard::find(root).has_value();
  if (full) {
    s.full = true;
    s.curve = readCurve(root);
  }
  s.battery = readBattery(root);
  if (rediscover)
    sensors.discover();
  sensors.sample();
  s.cpu = sensors.cpu();
  s.gpu = sensors.gpu();
  s.gpuPresent = sensors.gpuPresent();
  s.gpuAsleep = sensors.gpuAsleep();
  s.fans = sensors.fans();
  return s;
}
