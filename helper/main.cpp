// cohort-helper: the only part of Cohort that runs as root.
//
// The window runs as the person using it and reads everything it shows
// without privilege, because sysfs attributes are world readable. Changing
// one needs root, and polkit is how a desktop grants that: the window runs
// this program through pkexec, and the policy in
// assets/io.github.yappologistic.Cohort.policy decides whether it may.
//
// What it will do is fixed here and in src/controls.cpp, not by its caller:
//
//   cohort-helper set KEY=VALUE [KEY=VALUE ...]
//   cohort-helper lighting EFFECT SPEED BRIGHTNESS DIRECTION RGB RGB RGB RGB
//
// A key names a control and the helper finds the file itself, so no argument
// can point a write elsewhere. Every value is checked against what the driver
// publishes before anything is written, and a request with one bad pair
// writes nothing. The keyboard report is built here from checked fields
// rather than taken as bytes.
//
// Exit status: 0 done, 64 usage, 65 a value the control does not accept,
// 69 a control this machine does not have, 74 the kernel refused the write,
// 77 not running as root. pkexec itself exits 126 when authorisation is
// refused and 127 when it is dismissed.
#include "controls.h"
#include "keyboard.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <sysexits.h>
#include <unistd.h>
#include <vector>

namespace {

void usage() {
  std::fputs("usage: cohort-helper set KEY=VALUE [KEY=VALUE ...]\n"
             "       cohort-helper lighting EFFECT SPEED BRIGHTNESS DIRECTION RGB RGB RGB RGB\n"
             "keys:\n",
             stderr);
  for (const auto &key : controls::knownKeys())
    std::fprintf(stderr, "  %s\n", key.c_str());
}

int set(int argc, char **argv) {
  if (argc < 3 || argc > 64) {
    usage();
    return EX_USAGE;
  }
  struct Write {
    controls::Control control;
    std::string value;
  };
  std::vector<Write> writes;
  for (int i = 2; i < argc; ++i) {
    const std::string_view pair(argv[i]);
    const auto equals = pair.find('=');
    if (equals == std::string_view::npos) {
      usage();
      return EX_USAGE;
    }
    const auto key = pair.substr(0, equals);
    const auto control = controls::resolve("/", key);
    if (!control) {
      std::fprintf(stderr, "cohort-helper: this machine has no %.*s\n", int(key.size()), key.data());
      return EX_UNAVAILABLE;
    }
    const auto value = controls::validate(*control, pair.substr(equals + 1));
    if (!value) {
      std::fprintf(stderr, "cohort-helper: %.*s does not accept that value\n", int(key.size()), key.data());
      return EX_DATAERR;
    }
    writes.push_back({*control, *value});
  }
  // In the order given: a fan curve's points are written low to high so the
  // firmware never holds a curve whose temperatures fall.
  for (const auto &write : writes)
    if (const int error = controls::writeText(write.control.path, write.value)) {
      std::fprintf(stderr, "cohort-helper: %s: %s\n", write.control.key.c_str(), std::strerror(error));
      return EX_IOERR;
    }
  return EX_OK;
}

int lighting(int argc, char **argv) {
  std::vector<std::string_view> words(argv + 2, argv + argc);
  const auto state = keyboard::parse(words);
  if (!state) {
    usage();
    return EX_DATAERR;
  }
  const auto device = keyboard::find("/");
  if (!device) {
    std::fputs("cohort-helper: this machine has no four-zone keyboard\n", stderr);
    return EX_UNAVAILABLE;
  }
  if (const int error = keyboard::send(device->node, keyboard::report(*state))) {
    std::fprintf(stderr, "cohort-helper: keyboard: %s\n", std::strerror(error));
    return EX_IOERR;
  }
  return EX_OK;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return EX_USAGE;
  }
  if (::geteuid() != 0) {
    std::fputs("cohort-helper: run through pkexec\n", stderr);
    return EX_NOPERM;
  }
  const std::string_view command(argv[1]);
  if (command == "set")
    return set(argc, argv);
  if (command == "lighting")
    return lighting(argc, argv);
  usage();
  return EX_USAGE;
}
