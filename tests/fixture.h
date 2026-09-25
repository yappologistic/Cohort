#pragma once
// Fixture machines: sysfs and /dev trees laid out file for file the way each
// driver lays them out, so the rules and the machine layer can be driven
// through states this laptop is not in and hardware it does not have.
//
// Three machines:
//
// - mainline(): a Legion 5 Pro Gen 6 on kernel 7.2 with only the drivers
//   Linux ships: lenovo-wmi-gamezone's platform profile, ideapad-laptop's
//   switches and its charge_types battery extension, the ITE four-zone
//   keyboard. This is the machine Cohort was written on.
// - withLegion(): the same with LenovoLegionLinux's legion_laptop loaded on a
//   7.x kernel (the virtual "legion" device), its ten-point fan curve,
//   lenovo-wmi-other's firmware limits, and the lid logo LED.
// - olderKernel(): a 6.x kernel with no charge_types, where charging is the
//   conservation_mode switch plus legion_laptop's rapidcharge, on the
//   PNP0C09 device the module bound to before 7.0.
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

namespace fixture {

inline void put(const QString &root, const QString &path, const QByteArray &contents) {
  const QString full = root + "/" + path;
  QDir().mkpath(QFileInfo(full).absolutePath());
  QFile file(full);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    qFatal("fixture: cannot write %s", qPrintable(full));
  file.write(contents);
}

inline QByteArray read(const QString &root, const QString &path) {
  QFile file(root + "/" + path);
  return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

inline QByteArray readRaw(const QString &root, const QString &path) {
  QFile file(root + "/" + path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The start of the ITE controller's lighting interface report descriptor as
// this laptop reports it: Usage Page (0xFF89), Usage (0x10), Collection.
inline QByteArray lightingDescriptor() { return QByteArray::fromHex("0689ff0910a101855a0901"); }
// The controller's other interface, on the vendor page 0xFFC2.
inline QByteArray otherDescriptor() { return QByteArray::fromHex("06c2ff0904a10115002600"); }

inline void mainline(const QString &root) {
  put(root, "sys/class/dmi/id/product_version", "Legion 5 Pro 16ITH6H\n");
  put(root, "sys/class/dmi/id/product_name", "82JD\n");

  put(root, "sys/firmware/acpi/platform_profile", "balanced\n");
  put(root, "sys/firmware/acpi/platform_profile_choices", "low-power balanced performance custom\n");
  // lenovo-wmi-gamezone's own handler, where custom has to be written: the
  // legacy file refuses it (drivers/acpi/platform_profile.c).
  put(root, "sys/class/platform-profile/platform-profile-0/name", "lenovo-wmi-gamezone\n");
  put(root, "sys/class/platform-profile/platform-profile-0/choices", "low-power balanced performance custom\n");
  put(root, "sys/class/platform-profile/platform-profile-0/profile", "balanced\n");

  put(root, "sys/class/power_supply/BAT0/type", "Battery\n");
  put(root, "sys/class/power_supply/BAT0/status", "Discharging\n");
  put(root, "sys/class/power_supply/BAT0/capacity", "76\n");
  put(root, "sys/class/power_supply/BAT0/power_now", "14800000\n");
  put(root, "sys/class/power_supply/BAT0/energy_full", "72000000\n");
  put(root, "sys/class/power_supply/BAT0/energy_full_design", "80000000\n");
  put(root, "sys/class/power_supply/BAT0/cycle_count", "287\n");
  put(root, "sys/class/power_supply/BAT0/charge_types", "Fast [Standard] Long_Life\n");
  put(root, "sys/class/power_supply/ADP0/type", "Mains\n");
  put(root, "sys/class/power_supply/ADP0/online", "0\n");

  const QString vpc = "sys/bus/platform/devices/VPC2004:00/";
  put(root, vpc + "conservation_mode", "0\n");
  put(root, vpc + "fn_lock", "1\n");
  put(root, vpc + "usb_charging", "0\n");

  put(root, "sys/class/hwmon/hwmon4/name", "coretemp\n");
  put(root, "sys/class/hwmon/hwmon4/temp1_label", "Package id 0\n");
  put(root, "sys/class/hwmon/hwmon4/temp1_input", "61000\n");
  put(root, "sys/class/hwmon/hwmon4/temp2_label", "Core 0\n");
  put(root, "sys/class/hwmon/hwmon4/temp2_input", "58000\n");

  // A discrete NVIDIA GPU that has runtime-suspended, which the sensors must
  // leave asleep.
  put(root, "sys/bus/pci/devices/0000:01:00.0/class", "0x030000\n");
  put(root, "sys/bus/pci/devices/0000:01:00.0/vendor", "0x10de\n");
  put(root, "sys/bus/pci/devices/0000:01:00.0/boot_vga", "0\n");
  put(root, "sys/bus/pci/devices/0000:01:00.0/power/runtime_status", "suspended\n");

  // The ITE keyboard enumerates two hidraw interfaces; only the first opens
  // the lighting page.
  put(root, "sys/class/hidraw/hidraw0/device/uevent", "HID_ID=0003:0000048D:0000C965\nHID_NAME=ITE Tech. Inc. ITE Device(8295)\n");
  put(root, "sys/class/hidraw/hidraw0/device/report_descriptor", lightingDescriptor());
  put(root, "sys/class/hidraw/hidraw1/device/uevent", "HID_ID=0003:0000048D:0000C965\nHID_NAME=ITE Tech. Inc. ITE Device(8295)\n");
  put(root, "sys/class/hidraw/hidraw1/device/report_descriptor", otherDescriptor());
  put(root, "dev/hidraw0", "");
  put(root, "dev/hidraw1", "");
}

inline void legionModule(const QString &root, const QString &device) {
  QDir().mkpath(root + "/" + device + "/driver");
  for (const char *name : {"winkey", "touchpad", "gsync", "overdrive", "fan_fullspeed", "lockfancontroller",
                           "rapidcharge", "powermode"})
    put(root, device + "/" + name, "0\n");
  put(root, device + "/winkey", "1\n");
  put(root, device + "/touchpad", "1\n");
  put(root, device + "/gsync", "1\n");

  const QString hw = "sys/class/hwmon/hwmon7/";
  put(root, hw + "name", "legion_hwmon\n");
  put(root, hw + "fan1_input", "2200\n");
  put(root, hw + "fan1_label", "Fan 1\n");
  put(root, hw + "fan2_input", "2400\n");
  put(root, hw + "fan2_label", "Fan 2\n");
  put(root, hw + "fan1_max", "4400\n");
  put(root, hw + "auto_points_size", "10\n");
  put(root, hw + "minifancurve", "1\n");
  const int speeds[] = {0, 0, 46, 69, 92, 116, 139, 162, 197, 232};
  const int temps[] = {50, 55, 60, 65, 70, 75, 80, 85, 90, 127};
  for (int i = 0; i < 10; ++i) {
    const QString n = QString::number(i + 1);
    for (int sensor = 1; sensor <= 3; ++sensor) {
      const QString base = hw + "pwm" + QString::number(sensor) + "_auto_point" + n + "_";
      if (sensor <= 2)
        put(root, base + "pwm", QByteArray::number(speeds[i]) + "\n");
      put(root, base + "temp", QByteArray::number(temps[i]) + "\n");
      put(root, base + "temp_hyst", QByteArray::number(i ? temps[i - 1] - 3 : 0) + "\n");
    }
    put(root, hw + "pwm1_auto_point" + n + "_accel", "3\n");
    put(root, hw + "pwm1_auto_point" + n + "_decel", "3\n");
  }
}

inline void withLegion(const QString &root) {
  mainline(root);
  legionModule(root, "sys/bus/platform/devices/legion");
  // legion_laptop's own handler, which also offers custom. Custom must still
  // go to lenovo-wmi-gamezone's.
  put(root, "sys/class/platform-profile/platform-profile-1/name", "lenovo-legion\n");
  put(root, "sys/class/platform-profile/platform-profile-1/choices", "low-power balanced performance custom\n");
  put(root, "sys/class/platform-profile/platform-profile-1/profile", "balanced\n");

  const QString fw = "sys/class/firmware-attributes/lenovo-wmi-other-0/attributes/";
  const struct {
    const char *name, *value, *min, *max, *step;
  } limits[] = {{"ppt_pl1_spl", "80", "15", "130", "1"},
                {"ppt_pl2_sppt", "115", "15", "150", "1"},
                {"ppt_pl3_fppt", "135", "15", "175", "1"},
                // A mode written as an integer, which no slider should offer.
                {"gpu_mode", "1", "0", "3", "1"}};
  for (const auto &limit : limits) {
    const QString base = fw + limit.name + "/";
    put(root, base + "current_value", QByteArray(limit.value) + "\n");
    put(root, base + "min_value", QByteArray(limit.min) + "\n");
    put(root, base + "max_value", QByteArray(limit.max) + "\n");
    put(root, base + "scalar_increment", QByteArray(limit.step) + "\n");
    put(root, base + "type", "integer\n");
  }

  put(root, "sys/class/leds/platform::ylogo/brightness", "0\n");
  put(root, "sys/class/leds/platform::ylogo/max_brightness", "1\n");
}

inline void olderKernel(const QString &root) {
  mainline(root);
  QFile::remove(root + "/sys/class/power_supply/BAT0/charge_types");
  put(root, "sys/bus/platform/devices/VPC2004:00/conservation_mode", "1\n");
  legionModule(root, "sys/bus/platform/drivers/legion/PNP0C09:00");
}

} // namespace fixture
