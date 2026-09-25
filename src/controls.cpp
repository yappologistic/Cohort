#include "controls.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace controls {
namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

bool startsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

// The first entry of a directory whose name starts with `prefix`, in name
// order so the answer does not depend on the order the kernel lists them in.
std::optional<fs::path> firstMatching(const fs::path &directory, std::string_view prefix) {
  std::error_code error;
  std::vector<fs::path> found;
  for (const auto &entry : fs::directory_iterator(directory, error))
    if (startsWith(entry.path().filename().string(), prefix))
      found.push_back(entry.path());
  if (found.empty())
    return std::nullopt;
  std::sort(found.begin(), found.end());
  return found.front();
}

bool present(const fs::path &path) {
  std::error_code error;
  return fs::exists(path, error);
}

Control toggle(std::string_view key, fs::path path) {
  Control control;
  control.key = key;
  control.kind = Kind::Toggle;
  control.path = std::move(path);
  return control;
}

// A choice whose words the driver publishes in a separate file, or inline
// around the current one.
std::optional<Control> choice(std::string_view key, fs::path path, const fs::path &choicesFile) {
  const auto listed = readText(choicesFile);
  if (!listed)
    return std::nullopt;
  Control control;
  control.key = key;
  control.kind = Kind::Choice;
  control.path = std::move(path);
  control.choices = parseChoices(*listed);
  if (control.choices.empty())
    return std::nullopt;
  return control;
}

// An integer attribute of the firmware-attributes class: the value lives in
// current_value and its bounds in min_value, max_value and scalar_increment
// beside it (Documentation/ABI/testing/sysfs-class-firmware-attributes).
std::optional<Control> firmwareInteger(std::string_view key, const fs::path &attribute) {
  const auto minimum = readText(attribute / "min_value");
  const auto maximum = readText(attribute / "max_value");
  if (!minimum || !maximum || !present(attribute / "current_value"))
    return std::nullopt;
  const auto low = parseInteger(*minimum), high = parseInteger(*maximum);
  if (!low || !high || *low > *high)
    return std::nullopt;
  Control control;
  control.key = key;
  control.kind = Kind::Integer;
  control.path = attribute / "current_value";
  control.minimum = *low;
  control.maximum = *high;
  if (const auto step = readText(attribute / "scalar_increment"))
    if (const auto value = parseInteger(*step); value && *value > 0)
      control.step = *value;
  return control;
}

} // namespace

std::vector<std::string> parseChoices(std::string_view text) {
  std::vector<std::string> out;
  std::istringstream words{std::string(text)};
  std::string word;
  while (words >> word) {
    if (word.size() >= 2 && word.front() == '[' && word.back() == ']')
      word = word.substr(1, word.size() - 2);
    if (!word.empty() && std::find(out.begin(), out.end(), word) == out.end())
      out.push_back(word);
  }
  return out;
}

std::string parseSelected(std::string_view text) {
  std::istringstream words{std::string(text)};
  std::string word, only;
  int count = 0;
  while (words >> word) {
    if (word.size() >= 2 && word.front() == '[' && word.back() == ']')
      return word.substr(1, word.size() - 2);
    only = word;
    ++count;
  }
  return count == 1 ? only : std::string();
}

std::optional<long> parseInteger(std::string_view text) {
  text = trim(text);
  if (text.empty())
    return std::nullopt;
  long value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
    return std::nullopt;
  return value;
}

bool isAttributeName(std::string_view name) {
  if (name.empty() || name.size() > 64)
    return false;
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
  });
}

std::optional<std::string> readText(const fs::path &path) {
  std::ifstream file(path);
  if (!file)
    return std::nullopt;
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad())
    return std::nullopt;
  return std::string(trim(text));
}

int writeText(const fs::path &path, std::string_view value) {
  // O_NOFOLLOW: sysfs attributes are never symlinks, so a link where one is
  // expected is not something to follow as root. O_TRUNC is what a shell's
  // `echo x > attribute` opens with; sysfs ignores it, and a plain file,
  // like a fixture's, needs it. No O_CREAT: the helper never makes a file.
  const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return errno;
  // A sysfs store sees one write; a short or split write would hand the
  // driver half a word.
  const ssize_t written = ::write(fd, value.data(), value.size());
  const int error = written < 0 ? errno : written != ssize_t(value.size()) ? EIO : 0;
  ::close(fd);
  return error;
}

std::optional<fs::path> ideapadDevice(const fs::path &root) {
  // ideapad-laptop binds to the VPC2004 ACPI device; there is one per machine.
  return firstMatching(root / "sys/bus/platform/devices", "VPC2004:");
}

std::optional<fs::path> battery(const fs::path &root) {
  std::error_code error;
  std::vector<fs::path> found;
  for (const auto &entry : fs::directory_iterator(root / "sys/class/power_supply", error)) {
    const auto type = readText(entry.path() / "type");
    if (type && *type == "Battery")
      found.push_back(entry.path());
  }
  if (found.empty())
    return std::nullopt;
  std::sort(found.begin(), found.end());
  return found.front();
}

std::optional<fs::path> legionDevice(const fs::path &root) {
  // LenovoLegionLinux's legion_laptop registers a virtual platform device
  // named "legion" from kernel 7.0 (platform_device_register_simple), and
  // bound its platform driver of the same name to the embedded controller's
  // PNP0C09 device before that.
  const auto virtualDevice = root / "sys/bus/platform/devices/legion";
  if (present(virtualDevice / "driver"))
    return virtualDevice;
  return firstMatching(root / "sys/bus/platform/drivers/legion", "PNP0C09:");
}

std::optional<fs::path> legionHwmon(const fs::path &root) {
  std::error_code error;
  std::vector<fs::path> found;
  for (const auto &entry : fs::directory_iterator(root / "sys/class/hwmon", error)) {
    const auto name = readText(entry.path() / "name");
    if (name && *name == "legion_hwmon")
      found.push_back(entry.path());
  }
  if (found.empty())
    return std::nullopt;
  std::sort(found.begin(), found.end());
  return found.front();
}

std::vector<fs::path> firmwareAttributes(const fs::path &root) {
  // Each firmware-attributes class device keeps its settings under
  // attributes/. lenovo-wmi-other names its device after the driver.
  std::vector<fs::path> out;
  std::error_code error;
  for (const auto &device : fs::directory_iterator(root / "sys/class/firmware-attributes", error)) {
    std::error_code inner;
    for (const auto &attribute : fs::directory_iterator(device.path() / "attributes", inner))
      if (present(attribute.path() / "current_value"))
        out.push_back(attribute.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

// The legion_laptop attributes Cohort writes, all booleans. Each reads 1 when
// the thing it names is on: legion-laptop.c inverts the GameZone answer where
// the firmware reports the opposite, so winkey 1 is an enabled Windows key and
// gsync 1 is hybrid graphics. Everything else the module publishes is either
// read-only, a raw power limit with no published bounds to validate against,
// or a debugging aid, and is not reachable through the helper.
constexpr std::string_view kLegionToggles[] = {
    "lockfancontroller", "fan_fullspeed", "winkey", "touchpad",
    "gsync", "overdrive", "rapidcharge", "battery_conservation",
};

// The LEDs legion_laptop registers besides the keyboard (legion-laptop.c):
// the lid logo and the rear port lights.
constexpr std::string_view kLegionLeds[] = {"platform::ylogo", "platform::ioport",
                                            "platform::kbd_backlight"};

// A fan curve attribute of legion_hwmon, pwmF_auto_pointN_FIELD, split into
// its parts. F is the sensor the point belongs to (1 CPU, 2 GPU, 3 the
// chipset), N the point from 1 to 10 (MAXFANCURVESIZE).
struct CurveAttribute {
  int sensor = 0;
  int point = 0;
  std::string field;
};

std::optional<CurveAttribute> parseCurveAttribute(std::string_view name) {
  // "pwm1_auto_point1_pwm" is the shortest there is.
  if (!startsWith(name, "pwm") || name.size() < 20)
    return std::nullopt;
  CurveAttribute out;
  out.sensor = name[3] - '0';
  if (out.sensor < 1 || out.sensor > 3 || name.substr(4, 11) != "_auto_point")
    return std::nullopt;
  name.remove_prefix(15);
  const auto underscore = name.find('_');
  if (underscore == std::string_view::npos)
    return std::nullopt;
  const auto point = parseInteger(name.substr(0, underscore));
  if (!point || *point < 1 || *point > 10)
    return std::nullopt;
  out.point = int(*point);
  out.field = std::string(name.substr(underscore + 1));
  return out;
}

std::optional<Control> curveControl(std::string_view key, const fs::path &hwmon, std::string_view name) {
  if (name == "minifancurve") {
    if (!present(hwmon / name))
      return std::nullopt;
    return toggle(key, hwmon / name);
  }
  const auto parsed = parseCurveAttribute(name);
  if (!parsed)
    return std::nullopt;
  Control control;
  control.key = key;
  control.kind = Kind::Integer;
  control.path = hwmon / name;
  // The ranges legion-laptop.c accepts: speeds on the hwmon PWM scale, whole
  // degrees for the temperatures, and a ramp time from 2 to 5 where lower is
  // faster, which exists for the first fan only.
  if (parsed->field == "pwm" && parsed->sensor <= 2) {
    control.maximum = 255;
  } else if (parsed->field == "temp" || parsed->field == "temp_hyst") {
    control.maximum = 127;
  } else if ((parsed->field == "accel" || parsed->field == "decel") && parsed->sensor == 1) {
    control.minimum = 2;
    control.maximum = 5;
  } else {
    return std::nullopt;
  }
  if (!present(control.path))
    return std::nullopt;
  return control;
}

} // namespace

std::optional<Control> resolve(const fs::path &root, std::string_view key) {
  if (key == "platform-profile") {
    // The legacy file drives every registered handler at once, which is what
    // a person choosing a mode means; the class devices each drive one.
    const auto dir = root / "sys/firmware/acpi";
    return choice(key, dir / "platform_profile", dir / "platform_profile_choices");
  }
  if (key == "charge-types") {
    const auto bat = battery(root);
    if (!bat || !present(*bat / "charge_types"))
      return std::nullopt;
    return choice(key, *bat / "charge_types", *bat / "charge_types");
  }
  if (key == "conservation-mode" || key == "fn-lock" || key == "usb-charging") {
    const auto device = ideapadDevice(root);
    if (!device)
      return std::nullopt;
    std::string attribute(key);
    std::replace(attribute.begin(), attribute.end(), '-', '_');
    const auto path = *device / attribute;
    if (!present(path))
      return std::nullopt;
    return toggle(key, path);
  }
  if (startsWith(key, "firmware/")) {
    const auto name = key.substr(9);
    if (!isAttributeName(name))
      return std::nullopt;
    for (const auto &attribute : firmwareAttributes(root))
      if (attribute.filename() == name)
        return firmwareInteger(key, attribute);
    return std::nullopt;
  }
  if (startsWith(key, "legion/")) {
    const auto name = key.substr(7);
    if (std::find(std::begin(kLegionToggles), std::end(kLegionToggles), name) == std::end(kLegionToggles))
      return std::nullopt;
    const auto device = legionDevice(root);
    if (!device || !present(*device / name))
      return std::nullopt;
    return toggle(key, *device / name);
  }
  if (startsWith(key, "curve/")) {
    const auto hwmon = legionHwmon(root);
    if (!hwmon)
      return std::nullopt;
    return curveControl(key, *hwmon, key.substr(6));
  }
  if (startsWith(key, "led/")) {
    const auto name = key.substr(4);
    if (std::find(std::begin(kLegionLeds), std::end(kLegionLeds), name) == std::end(kLegionLeds))
      return std::nullopt;
    const auto led = root / "sys/class/leds" / std::string(name);
    const auto maximum = readText(led / "max_brightness");
    const auto bound = maximum ? parseInteger(*maximum) : std::nullopt;
    if (!bound || *bound < 1 || !present(led / "brightness"))
      return std::nullopt;
    Control control;
    control.key = key;
    control.kind = Kind::Integer;
    control.path = led / "brightness";
    control.maximum = *bound;
    return control;
  }
  return std::nullopt;
}

std::optional<std::string> validate(const Control &control, std::string_view value) {
  value = trim(value);
  switch (control.kind) {
  case Kind::Toggle:
    if (value == "1" || value == "true" || value == "on")
      return std::string("1");
    if (value == "0" || value == "false" || value == "off")
      return std::string("0");
    return std::nullopt;
  case Kind::Choice:
    for (const auto &word : control.choices)
      if (word == value)
        return word;
    return std::nullopt;
  case Kind::Integer: {
    const auto number = parseInteger(value);
    if (!number || *number < control.minimum || *number > control.maximum)
      return std::nullopt;
    if ((*number - control.minimum) % control.step != 0)
      return std::nullopt;
    return std::to_string(*number);
  }
  }
  return std::nullopt;
}

std::vector<std::string> knownKeys() {
  std::vector<std::string> keys{"platform-profile", "charge-types", "conservation-mode",
                                "fn-lock",          "usb-charging", "firmware/<attribute>"};
  for (const auto name : kLegionToggles)
    keys.push_back("legion/" + std::string(name));
  keys.push_back("curve/pwm<sensor>_auto_point<n>_<pwm|temp|temp_hyst|accel|decel>");
  keys.push_back("curve/minifancurve");
  for (const auto name : kLegionLeds)
    keys.push_back("led/" + std::string(name));
  return keys;
}

} // namespace controls
