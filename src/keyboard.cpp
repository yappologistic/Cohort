#include "keyboard.h"
#include "controls.h"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <fstream>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace keyboard {

Report report(const Lighting &lighting) {
  Report out{};
  out[0] = 0xCC; // the report id
  out[1] = 0x16; // the "set lighting" command
  out[2] = static_cast<std::uint8_t>(lighting.effect);
  out[3] = static_cast<std::uint8_t>(std::clamp(lighting.speed, 1, 4));
  out[4] = static_cast<std::uint8_t>(std::clamp(lighting.brightness, 1, 2));
  // Bytes 5 to 16 are the four zones as R, G, B. The animated effects that
  // cycle colours on their own ignore them, and the references send them
  // only for static and breath; sending zeros for the rest keeps the report
  // the same as theirs.
  if (lighting.effect == Effect::Static || lighting.effect == Effect::Breath)
    for (std::size_t zone = 0; zone < 4; ++zone) {
      const auto rgb = lighting.zones[zone];
      out[5 + zone * 3] = (rgb >> 16) & 0xFF;
      out[6 + zone * 3] = (rgb >> 8) & 0xFF;
      out[7 + zone * 3] = rgb & 0xFF;
    }
  // Byte 17 is zero. 18 and 19 set the wave's direction.
  if (lighting.effect == Effect::Wave) {
    out[18] = lighting.direction == Direction::LeftToRight ? 1 : 0;
    out[19] = lighting.direction == Direction::RightToLeft ? 1 : 0;
  }
  return out;
}

bool isKnownProduct(std::uint16_t product) {
  // L5P-Keyboard-RGB's list, by model year: c955 (2020), c965 and c963
  // (2021), c975 and c973 (2022), c985, c984 and c983 (2023), c995, c994 and
  // c993 (2024).
  constexpr std::uint16_t known[] = {0xc955, 0xc963, 0xc965, 0xc973, 0xc975, 0xc983,
                                     0xc984, 0xc985, 0xc993, 0xc994, 0xc995};
  return std::find(std::begin(known), std::end(known), product) != std::end(known);
}

std::optional<Effect> parseEffect(std::string_view word) {
  if (word == "static") return Effect::Static;
  if (word == "breath") return Effect::Breath;
  if (word == "wave") return Effect::Wave;
  if (word == "smooth") return Effect::Smooth;
  return std::nullopt;
}

std::optional<Direction> parseDirection(std::string_view word) {
  if (word == "none") return Direction::None;
  if (word == "left") return Direction::RightToLeft;
  if (word == "right") return Direction::LeftToRight;
  return std::nullopt;
}

std::optional<std::uint32_t> parseColour(std::string_view word) {
  if (word.size() != 6)
    return std::nullopt;
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(word.data(), word.data() + 6, value, 16);
  if (error != std::errc() || end != word.data() + 6)
    return std::nullopt;
  return value;
}

std::string effectName(Effect effect) {
  switch (effect) {
  case Effect::Static: return "static";
  case Effect::Breath: return "breath";
  case Effect::Wave: return "wave";
  case Effect::Smooth: return "smooth";
  }
  return "static";
}

std::string directionName(Direction direction) {
  switch (direction) {
  case Direction::None: return "none";
  case Direction::RightToLeft: return "left";
  case Direction::LeftToRight: return "right";
  }
  return "none";
}

std::optional<Lighting> parse(const std::vector<std::string_view> &words) {
  if (words.size() != 8)
    return std::nullopt;
  Lighting out;
  const auto effect = parseEffect(words[0]);
  const auto speed = controls::parseInteger(words[1]);
  const auto brightness = controls::parseInteger(words[2]);
  const auto direction = parseDirection(words[3]);
  if (!effect || !speed || *speed < 1 || *speed > 4 || !brightness || *brightness < 1 ||
      *brightness > 2 || !direction)
    return std::nullopt;
  out.effect = *effect;
  out.speed = int(*speed);
  out.brightness = int(*brightness);
  out.direction = *direction;
  for (std::size_t zone = 0; zone < 4; ++zone) {
    const auto colour = parseColour(words[4 + zone]);
    if (!colour)
      return std::nullopt;
    out.zones[zone] = *colour;
  }
  return out;
}

namespace {

// HID_ID=0003:0000048D:0000C965 is bus, vendor and product, in hex.
std::optional<std::uint16_t> lightingProduct(const fs::path &hidraw) {
  std::ifstream uevent(hidraw / "device/uevent");
  std::string line;
  while (std::getline(uevent, line)) {
    if (line.rfind("HID_ID=", 0) != 0)
      continue;
    const auto id = std::string_view(line).substr(7);
    if (id.size() != 22 || id.substr(5, 8) != "0000048D")
      return std::nullopt;
    std::uint32_t product = 0;
    const auto digits = id.substr(14, 8);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), product, 16);
    if (error != std::errc() || product > 0xFFFF || !isKnownProduct(std::uint16_t(product)))
      return std::nullopt;
    return std::uint16_t(product);
  }
  return std::nullopt;
}

// A long Usage Page item for 0xFF89 is 06 89 FF in the report descriptor
// (HID 1.11, 6.2.2.7).
bool opensLightingPage(const fs::path &hidraw) {
  std::ifstream file(hidraw / "device/report_descriptor", std::ios::binary);
  const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  const char page[] = {char(0x06), char(0x89), char(0xFF)};
  return std::search(bytes.begin(), bytes.end(), std::begin(page), std::end(page)) != bytes.end();
}

} // namespace

std::optional<Device> find(const fs::path &root) {
  std::error_code error;
  std::vector<fs::path> nodes;
  for (const auto &entry : fs::directory_iterator(root / "sys/class/hidraw", error))
    nodes.push_back(entry.path());
  std::sort(nodes.begin(), nodes.end());
  for (const auto &node : nodes) {
    const auto product = lightingProduct(node);
    if (product && opensLightingPage(node))
      return Device{root / "dev" / node.filename(), *product};
  }
  return std::nullopt;
}

int send(const fs::path &node, const Report &report) {
  const int fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return errno;
  auto buffer = report;
  const int result = ::ioctl(fd, HIDIOCSFEATURE(kReportSize), buffer.data());
  const int error = result < 0 ? errno : 0;
  ::close(fd);
  return error;
}

} // namespace keyboard
