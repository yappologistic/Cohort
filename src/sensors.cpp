#include "sensors.h"
#include "controls.h"
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <limits>

namespace fs = std::filesystem;

namespace {

constexpr double kUnknown = std::numeric_limits<double>::quiet_NaN();

std::string text(const fs::path &path) { return controls::readText(path).value_or(std::string()); }

double milliCelsius(const fs::path &input) {
  const auto value = controls::parseInteger(text(input));
  return value ? *value / 1000.0 : kUnknown;
}

std::vector<fs::path> hwmons(const fs::path &root) {
  std::vector<fs::path> out;
  std::error_code error;
  for (const auto &entry : fs::directory_iterator(root / "sys/class/hwmon", error))
    out.push_back(entry.path());
  std::sort(out.begin(), out.end());
  return out;
}

// The input among tempN whose label is `label`, or temp1 where the chip
// labels nothing.
fs::path labelledTemp(const fs::path &hwmon, std::initializer_list<const char *> labels) {
  for (int i = 1; i < 64; ++i) {
    const auto base = hwmon / ("temp" + std::to_string(i));
    const auto label = text(base.string() + "_label");
    for (const char *wanted : labels)
      if (label == wanted)
        return base.string() + "_input";
  }
  const auto first = hwmon / "temp1_input";
  std::error_code error;
  return fs::exists(first, error) ? first : fs::path();
}

} // namespace

Sensors::Sensors(fs::path root) : m_root(std::move(root)), m_cpu(kUnknown), m_gpu(kUnknown) {}

Sensors::~Sensors() {
  if (m_nvml) {
    if (auto shutdown = reinterpret_cast<int (*)()>(::dlsym(m_nvml, "nvmlShutdown")))
      shutdown();
    ::dlclose(m_nvml);
  }
}

void Sensors::discover() {
  m_cpuInput.clear();
  m_gpuInput.clear();
  m_gpuPower.clear();
  m_gpuKind = GpuKind::None;
  m_fanInputs.clear();

  for (const auto &hwmon : hwmons(m_root)) {
    const auto name = text(hwmon / "name");
    // The package sensor is the one firmware throttles on: coretemp calls it
    // "Package id 0", k10temp "Tctl" and zenpower "Tdie".
    if (m_cpuInput.empty() && name == "coretemp")
      m_cpuInput = labelledTemp(hwmon, {"Package id 0"});
    else if (m_cpuInput.empty() && (name == "k10temp" || name == "zenpower"))
      m_cpuInput = labelledTemp(hwmon, {"Tctl", "Tdie"});
    for (int i = 1; i < 16; ++i) {
      const auto input = hwmon / ("fan" + std::to_string(i) + "_input");
      std::error_code error;
      if (!fs::exists(input, error))
        continue;
      auto label = QString::fromStdString(text(hwmon / ("fan" + std::to_string(i) + "_label")));
      m_fanInputs.push_back({input, label});
    }
  }
  // Fans the chip does not name are numbered in the order they were found.
  for (size_t i = 0; i < m_fanInputs.size(); ++i)
    if (m_fanInputs[i].label.isEmpty())
      m_fanInputs[i].label = m_fanInputs.size() == 1 ? QStringLiteral("Fan")
                                                     : QStringLiteral("Fan %1").arg(i + 1);

  // The GPU worth a temperature: an NVIDIA display controller (PCI class
  // 0x03) wherever it is, since on a laptop it is always the discrete one and
  // it is the only GPU in the machine when the MUX runs it alone; otherwise
  // an AMD one that is not the GPU the firmware booted on, which on an AMD
  // laptop is the discrete card beside the APU.
  std::error_code error;
  std::vector<fs::path> devices;
  for (const auto &entry : fs::directory_iterator(m_root / "sys/bus/pci/devices", error))
    devices.push_back(entry.path());
  std::sort(devices.begin(), devices.end());
  for (const auto &device : devices) {
    if (text(device / "class").rfind("0x03", 0) != 0)
      continue;
    const auto vendor = text(device / "vendor");
    if (vendor == "0x10de") {
      m_gpuKind = GpuKind::Nvml;
    } else if (vendor == "0x1002" && text(device / "boot_vga") != "1") {
      std::error_code inner;
      for (const auto &hw : fs::directory_iterator(device / "hwmon", inner)) {
        m_gpuInput = labelledTemp(hw.path(), {"edge"});
        if (!m_gpuInput.empty())
          m_gpuKind = GpuKind::Hwmon;
        break;
      }
    }
    if (m_gpuKind != GpuKind::None) {
      m_gpuPower = device / "power/runtime_status";
      break;
    }
  }
}

void Sensors::openNvml() {
  m_nvmlTried = true;
  m_nvml = ::dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (!m_nvml)
    return;
  auto init = reinterpret_cast<int (*)()>(::dlsym(m_nvml, "nvmlInit_v2"));
  auto handle = reinterpret_cast<int (*)(unsigned int, void **)>(::dlsym(m_nvml, "nvmlDeviceGetHandleByIndex_v2"));
  m_nvmlTemperature = reinterpret_cast<int (*)(void *, int, unsigned int *)>(::dlsym(m_nvml, "nvmlDeviceGetTemperature"));
  // NVML_SUCCESS is 0.
  if (!init || !handle || !m_nvmlTemperature || init() != 0 || handle(0, &m_nvmlDevice) != 0) {
    ::dlclose(m_nvml);
    m_nvml = nullptr;
    m_nvmlDevice = nullptr;
  }
}

double Sensors::sampleNvml() {
  // NVML is opened the first time the GPU is found awake, never before:
  // initialising it opens the device, which is what would wake it.
  if (!m_nvmlTried)
    openNvml();
  if (!m_nvmlDevice)
    return kUnknown;
  unsigned int celsius = 0;
  // NVML_TEMPERATURE_GPU is 0.
  return m_nvmlTemperature(m_nvmlDevice, 0, &celsius) == 0 ? double(celsius) : kUnknown;
}

void Sensors::sample() {
  m_cpu = m_cpuInput.empty() ? kUnknown : milliCelsius(m_cpuInput);

  m_gpuAsleep = !m_gpuPower.empty() && text(m_gpuPower) == "suspended";
  if (m_gpuAsleep || m_gpuKind == GpuKind::None)
    m_gpu = kUnknown;
  else if (m_gpuKind == GpuKind::Nvml)
    m_gpu = sampleNvml();
  else
    m_gpu = milliCelsius(m_gpuInput);

  QVariantList fans;
  for (const auto &fan : m_fanInputs) {
    const auto rpm = controls::parseInteger(text(fan.input));
    if (rpm)
      fans.append(QVariantMap{{"label", fan.label}, {"rpm", int(*rpm)}});
  }
  m_fans = fans;
}
