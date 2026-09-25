#pragma once
// The temperatures and fan speeds a power mode or a fan curve is judged by.
//
// Everything comes from hwmon (Documentation/hwmon/sysfs-interface.rst):
// temperatures in millidegrees Celsius in tempN_input, fan speeds in RPM in
// fanN_input. The one exception is the NVIDIA driver, which publishes no hwmon
// device, so its temperature comes from NVML, opened at runtime and never
// linked: a machine without the driver is a machine without that figure.
//
// A discrete GPU that has runtime-suspended is left asleep. Asking it for a
// temperature would power it back up, and a laptop tool that costs battery to
// report on the laptop has failed at its own job; the figure is reported as
// asleep instead.
#include <QString>
#include <QVariantList>
#include <vector>
#include <filesystem>

class Sensors {
public:
  explicit Sensors(std::filesystem::path root = "/");
  ~Sensors();
  Sensors(const Sensors &) = delete;
  Sensors &operator=(const Sensors &) = delete;

  // Finds the devices. Nothing here changes without a reboot or a module
  // load, so it runs once and when the machine layer asks again.
  void discover();
  void sample();

  // Degrees Celsius, or NaN where this machine does not say.
  double cpu() const { return m_cpu; }
  double gpu() const { return m_gpu; }
  bool gpuPresent() const { return m_gpuKind != GpuKind::None; }
  bool gpuAsleep() const { return m_gpuAsleep; }
  // [{label, rpm}] for every fan hwmon reports.
  QVariantList fans() const { return m_fans; }

private:
  enum class GpuKind { None, Hwmon, Nvml };
  struct Fan {
    std::filesystem::path input;
    QString label;
  };
  void openNvml();
  double sampleNvml();

  std::filesystem::path m_root;
  std::filesystem::path m_cpuInput;
  std::filesystem::path m_gpuInput;
  std::filesystem::path m_gpuPower; // the PCI device's power/runtime_status
  GpuKind m_gpuKind = GpuKind::None;
  std::vector<Fan> m_fanInputs;

  double m_cpu;
  double m_gpu;
  bool m_gpuAsleep = false;
  QVariantList m_fans;

  void *m_nvml = nullptr;
  void *m_nvmlDevice = nullptr;
  bool m_nvmlTried = false;
  int (*m_nvmlTemperature)(void *, int, unsigned int *) = nullptr;
};
