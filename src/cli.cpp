#include "cli.h"
#include "machine.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <cmath>
#include <cstdio>

namespace cli {
namespace {

const char *const kCommands[] = {"--mode", "--charge", "--lighting", "--backlight", "--status", "--help"};

// The words people use, mapped to the ones the kernel does. Lenovo's names
// come first; the kernel's own words are accepted too.
QString profileFor(const QString &word, const QStringList &choices) {
  const QString w = word.toLower();
  if (w == "quiet")
    return choices.contains("low-power") ? QStringLiteral("low-power") : QStringLiteral("quiet");
  if (w == "extreme")
    return QStringLiteral("max-power");
  return w;
}

QString chargeFor(const QString &word) {
  const QString w = word.toLower();
  if (w == "conservation" || w == "long_life")
    return QStringLiteral("Long_Life");
  if (w == "standard")
    return QStringLiteral("Standard");
  if (w == "rapid" || w == "fast")
    return QStringLiteral("Fast");
  return {};
}

void usage() {
  std::fputs("usage: cohort --mode quiet|balanced|performance|extreme|custom|next\n"
             "       cohort --charge conservation|standard|rapid\n"
             "       cohort --lighting off|static|breath|wave|smooth\n"
             "       cohort --backlight off|low|high\n"
             "       cohort --status\n",
             stderr);
}

// Waits until no change and no reading is under way, or until a change has
// plainly failed. pkexec can take a moment on a first run.
bool settle(Machine &machine, QString *failure) {
  bool failed = false;
  // The connection lives as long as this call and no longer, since it
  // writes into the call's own variables.
  QObject scope;
  QObject::connect(&machine, &Machine::failed, &scope, [&](const QString &message) {
    failed = true;
    *failure = message;
  });
  QElapsedTimer clock;
  clock.start();
  QEventLoop loop;
  while (machine.settling() && !failed && clock.elapsed() < 30000) {
    QTimer::singleShot(25, &loop, &QEventLoop::quit);
    loop.exec();
  }
  return !failed && !machine.settling();
}

QString readable(double celsius) { return std::isnan(celsius) ? QStringLiteral("-") : QString::number(qRound(celsius)) + " °C"; }

} // namespace

bool wanted(const QStringList &arguments) {
  for (const char *command : kCommands)
    if (arguments.contains(QLatin1String(command)))
      return true;
  return false;
}

int run(const QStringList &arguments, const std::filesystem::path &root) {
  if (arguments.contains("--help")) {
    usage();
    return 0;
  }
  Machine machine(root);
  // The full reading, which the backlight's level and the module's switches
  // come with, lands before anything is decided from them.
  QString unused;
  settle(machine, &unused);
  const auto argument = [&](const char *flag) {
    const int at = arguments.indexOf(QLatin1String(flag));
    return at >= 0 && at + 1 < arguments.size() ? arguments.at(at + 1) : QString();
  };

  if (arguments.contains("--status")) {
    const auto battery = machine.battery();
    std::printf("mode      %s\n", qPrintable(machine.powerProfile()));
    if (!machine.chargeMode().isEmpty())
      std::printf("charging  %s\n", qPrintable(machine.chargeMode()));
    if (battery.value("present").toBool())
      std::printf("battery   %d%% %s\n", battery.value("percent").toInt(), qPrintable(battery.value("status").toString()));
    std::printf("cpu       %s\n", qPrintable(readable(machine.cpuTemperature())));
    if (machine.gpuPresent())
      std::printf("gpu       %s\n", machine.gpuAsleep() ? "asleep" : qPrintable(readable(machine.gpuTemperature())));
    for (const auto &fan : machine.fans())
      std::printf("%-9s %d rpm\n", qPrintable(fan.toMap().value("label").toString().toLower()),
                  fan.toMap().value("rpm").toInt());
    if (machine.backlight() >= 0)
      std::printf("backlight %d of %d\n", machine.backlight(), machine.backlightMax());
    return 0;
  }

  if (const auto word = argument("--mode"); !word.isEmpty()) {
    const auto choices = machine.powerProfiles();
    QString profile = profileFor(word, choices);
    if (profile == "next") {
      // The Fn+Q modes, in Fn+Q's order; custom and extreme are chosen by
      // name.
      QStringList cycle;
      for (const auto &p : choices)
        if (p != "custom" && p != "max-power")
          cycle << p;
      profile = cycle.value((cycle.indexOf(machine.powerProfile()) + 1) % qMax(1, int(cycle.size())));
    }
    if (!choices.contains(profile)) {
      std::fprintf(stderr, "cohort: this laptop has no %s mode (it has: %s)\n", qPrintable(word),
                   qPrintable(choices.join(", ")));
      return 64;
    }
    machine.setPowerProfile(profile);
  } else if (const auto charge = argument("--charge"); !charge.isEmpty()) {
    const QString mode = chargeFor(charge);
    if (!machine.chargeModes().contains(mode)) {
      std::fprintf(stderr, "cohort: this laptop has no %s charging\n", qPrintable(charge));
      return 64;
    }
    machine.setChargeMode(mode);
  } else if (const auto lighting = argument("--lighting"); !lighting.isEmpty()) {
    const QStringList effects{"off", "static", "breath", "wave", "smooth"};
    if (!effects.contains(lighting) || !machine.lightingAvailable()) {
      std::fprintf(stderr, "cohort: %s\n", machine.lightingAvailable() ? "no such lighting effect"
                                                                       : "this laptop has no four-zone keyboard");
      return 64;
    }
    machine.setLighting({{"effect", lighting}});
  } else if (const auto level = argument("--backlight"); !level.isEmpty()) {
    const QStringList names{"off", "low", "high"};
    int value = names.indexOf(level.toLower());
    if (value < 0) {
      bool ok = false;
      value = level.toInt(&ok);
      if (!ok)
        value = -1;
    }
    if (machine.backlight() < 0 || value < 0 || value > machine.backlightMax()) {
      std::fprintf(stderr, "cohort: %s\n", machine.backlight() < 0 ? "this laptop publishes no keyboard backlight"
                                                                   : "no such backlight level");
      return 64;
    }
    machine.setBacklight(value);
  } else {
    usage();
    return 64;
  }

  QString failure;
  if (!settle(machine, &failure)) {
    std::fprintf(stderr, "cohort: %s\n", qPrintable(failure.isEmpty() ? QStringLiteral("no answer from the laptop") : failure));
    return 1;
  }
  return 0;
}

} // namespace cli
