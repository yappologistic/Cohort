#pragma once
// The controls Cohort can change, and where each one lives.
//
// This is the one list of what may be written, shared by the window and by
// the privileged helper. The window uses it to find what this machine has; the
// helper uses it to refuse anything else. A key names a control, never a path:
// the helper resolves the path itself, under a root it does not take from its
// caller, so nothing the unprivileged side sends can point a root write at a
// file of its choosing.
//
// No Qt in here. The helper runs as root and links against the C++ library
// alone, so the code that decides what root will write is small enough to read
// in one sitting.
//
// Every control is a sysfs attribute that a kernel driver publishes:
//
// - platform_profile, from the ACPI platform profile class
//   (Documentation/ABI/testing/sysfs-platform_profile). On Legion laptops the
//   lenovo-wmi-gamezone driver provides it; on older kernels the out-of-tree
//   legion_laptop module does.
// - charge_types on the battery, from ideapad-laptop's battery extension
//   (Documentation/ABI/testing/sysfs-class-power, "charge_types"), and the
//   older conservation_mode on the VPC2004 device it replaced.
// - fn_lock and usb_charging on the VPC2004 device, from ideapad-laptop
//   (Documentation/ABI/testing/sysfs-platform-ideapad-laptop).
// - Power limits under /sys/class/firmware-attributes, from lenovo-wmi-other
//   (Documentation/ABI/testing/sysfs-class-firmware-attributes).
// - The legion_laptop module's own attributes and fan curve, from
//   LenovoLegionLinux (kernel_module/legion-laptop.c), where it is loaded.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace controls {

enum class Kind {
  // One word out of a published list, like a platform profile.
  Choice,
  // 0 or 1.
  Toggle,
  // A whole number between published bounds, on a published step.
  Integer,
};

struct Control {
  std::string key;
  Kind kind = Kind::Toggle;
  std::filesystem::path path;
  // Choice: the words the driver accepts, in its own order.
  std::vector<std::string> choices;
  // Integer: the bounds and the step, inclusive.
  long minimum = 0;
  long maximum = 1;
  long step = 1;
  // Where "custom" is written, when it cannot go to `path`. The legacy
  // /sys/firmware/acpi/platform_profile refuses custom by design
  // (drivers/acpi/platform_profile.c, platform_profile_store), because it
  // drives every handler at once and custom belongs to one of them; custom
  // goes to that handler's own class device instead.
  std::filesystem::path customPath;
};

// --- Pure parsers ------------------------------------------------------------
// Bytes in, values out. No file access, so the awkward cases are testable
// without the hardware.

// Words separated by whitespace, with the brackets that mark the current one
// removed: "[Fast] Standard Long_Life" is Fast, Standard and Long_Life.
std::vector<std::string> parseChoices(std::string_view text);
// The bracketed word, or the only word when there is one: "[Fast] Standard"
// gives Fast, "balanced\n" gives balanced.
std::string parseSelected(std::string_view text);
// A decimal integer with surrounding whitespace, or nothing.
std::optional<long> parseInteger(std::string_view text);
// A key made only of the characters a sysfs attribute name uses. Anything with
// a slash or a dot in it is not a name and is refused before it is joined to a
// path.
bool isAttributeName(std::string_view name);

// --- Files -------------------------------------------------------------------
std::optional<std::string> readText(const std::filesystem::path &path);
// Writes the whole value in one write(2), which is how sysfs expects a store.
// Returns 0 or the errno the kernel answered with.
int writeText(const std::filesystem::path &path, std::string_view value);

// --- Resolution --------------------------------------------------------------

// Where this machine keeps a control, or nothing when it has no such control.
// `root` is "/" everywhere but the tests.
std::optional<Control> resolve(const std::filesystem::path &root, std::string_view key);

// The value to write for a request, normalised, or nothing when the control
// does not accept it. A Toggle takes 0/1, true/false or on/off; a Choice one of
// its words exactly; an Integer a number inside its bounds on its step.
std::optional<std::string> validate(const Control &control, std::string_view value);

// The file a validated value is written to: the control's own, except for a
// custom platform profile, which goes to the handler that offers it.
std::filesystem::path target(const Control &control, std::string_view value);

// The platform profile the machine is in. The legacy file reads "custom" when
// the handlers disagree; a handler that is itself in custom says so on its
// class device, which is read first.
std::string currentProfile(const std::filesystem::path &root);

// The keys this build knows how to resolve, for the helper's usage text and for
// discovery. Keys with a parameter are listed with it in angle brackets.
std::vector<std::string> knownKeys();

// The fixed directories the keys resolve inside. Exposed so discovery and the
// helper agree on them.
std::optional<std::filesystem::path> ideapadDevice(const std::filesystem::path &root);
std::optional<std::filesystem::path> battery(const std::filesystem::path &root);
std::optional<std::filesystem::path> legionDevice(const std::filesystem::path &root);
std::optional<std::filesystem::path> legionHwmon(const std::filesystem::path &root);
std::vector<std::filesystem::path> firmwareAttributes(const std::filesystem::path &root);

} // namespace controls
