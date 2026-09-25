#pragma once
// The four-zone RGB keyboard of Legion 5, Legion 5 Pro, IdeaPad Gaming and LOQ
// laptops.
//
// The keyboard is an ITE controller on USB (vendor 048d) that takes one HID
// feature report and holds it until the next: an effect, a speed, a
// brightness, a colour for each of the four zones from left to right, and a
// direction for the wave. There is no report to read the current state back,
// so what the keyboard is showing is whatever was last sent to it.
//
// The layout is the one documented by two independent implementations:
// L5P-Keyboard-RGB (github.com/4JX/L5P-Keyboard-RGB, driver/src/lib.rs) and
// OpenRGB's Lenovo4ZoneUSBController. Only the facts of the protocol are taken
// from them; no code is.
//
// No Qt in here: the helper builds the report itself from validated fields,
// so the unprivileged side never hands it bytes to send.
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keyboard {

enum class Effect : std::uint8_t {
  Static = 0x01,
  Breath = 0x03,
  Wave = 0x04,
  Smooth = 0x06,
};

enum class Direction { None, LeftToRight, RightToLeft };

struct Lighting {
  Effect effect = Effect::Static;
  // 1 to 4, fastest at 4.
  int speed = 1;
  // 1 low, 2 high.
  int brightness = 2;
  Direction direction = Direction::None;
  // 0xRRGGBB for each zone, left to right.
  std::array<std::uint32_t, 4> zones{};
};

constexpr std::size_t kReportSize = 33;
using Report = std::array<std::uint8_t, kReportSize>;

// The feature report for a state. Pure.
Report report(const Lighting &lighting);

// The product ids the protocol is known to hold for, under vendor 048d.
bool isKnownProduct(std::uint16_t product);

// Words to fields and back, as the helper and the window exchange them:
// "static", "breath", "wave", "smooth"; "none", "left", "right"; "rrggbb".
std::optional<Effect> parseEffect(std::string_view word);
std::optional<Direction> parseDirection(std::string_view word);
std::optional<std::uint32_t> parseColour(std::string_view word);
std::string effectName(Effect effect);
std::string directionName(Direction direction);

// The lighting a helper command line describes, or nothing if any field is out
// of range: effect speed brightness direction colour colour colour colour.
std::optional<Lighting> parse(const std::vector<std::string_view> &words);

struct Device {
  // The hidraw node, /dev/hidrawN.
  std::filesystem::path node;
  std::uint16_t product = 0;
};

// The keyboard's lighting interface, if this machine has one. The controller
// enumerates more than one HID interface; the lighting one is the interface
// whose report descriptor opens the vendor usage page 0xFF89, which is what
// both references match on.
std::optional<Device> find(const std::filesystem::path &root);

// Sends a report with HIDIOCSFEATURE. Returns 0 or the errno.
int send(const std::filesystem::path &node, const Report &report);

} // namespace keyboard
