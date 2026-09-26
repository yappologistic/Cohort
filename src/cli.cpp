#include "cli.h"
#include "machine.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// The inverse of profileFor: the word --mode takes for a profile.
QString wordFor(const QString &profile) {
  if (profile == "low-power")
    return QStringLiteral("quiet");
  if (profile == "max-power")
    return QStringLiteral("extreme");
  return profile;
}

void usage() {
  std::fputs("usage: cohort --mode quiet|balanced|performance|extreme|custom|next\n"
             "       cohort --charge conservation|standard|rapid\n"
             "       cohort --lighting off|static|breath|wave|smooth\n"
             "       cohort --backlight off|low|high\n"
             "       cohort --status [--json]\n",
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

// --status, one figure to a line.
QString statusText(const Machine &machine) {
  const auto battery = machine.battery();
  QString out;
  const auto line = [&](const QString &label, const QString &value) {
    out += label.leftJustified(9) + ' ' + value + '\n';
  };
  line("mode", machine.powerProfile());
  if (!machine.chargeMode().isEmpty())
    line("charging", machine.chargeMode());
  if (battery.value("present").toBool())
    line("battery", QString::number(battery.value("percent").toInt()) + "% " + battery.value("status").toString());
  line("cpu", readable(machine.cpuTemperature()));
  if (machine.gpuPresent())
    line("gpu", machine.gpuAsleep() ? QStringLiteral("asleep") : readable(machine.gpuTemperature()));
  for (const auto &fan : machine.fans())
    line(fan.toMap().value("label").toString().toLower(), QString::number(fan.toMap().value("rpm").toInt()) + " rpm");
  if (machine.backlight() >= 0)
    line("backlight", QString::number(machine.backlight()) + " of " + QString::number(machine.backlightMax()));
  return out;
}

// --status --json, for status bars and scripts. text, alt, class and tooltip
// are the fields Waybar's custom module reads (man 5 waybar-custom); the
// rest are the figures themselves, null where the laptop has none to give.
QByteArray statusJson(const Machine &machine) {
  const auto celsius = [](double value) { return std::isnan(value) ? QJsonValue() : QJsonValue(qRound(value)); };
  const auto battery = machine.battery();
  const QString word = wordFor(machine.powerProfile());
  QJsonArray fans;
  for (const auto &fan : machine.fans())
    fans.append(QJsonObject{{"label", fan.toMap().value("label").toString()}, {"rpm", fan.toMap().value("rpm").toInt()}});
  const QJsonObject status{
      {"text", machine.profileName(machine.powerProfile())},
      {"alt", word},
      {"class", word},
      {"tooltip", statusText(machine).trimmed()},
      {"mode", machine.powerProfile()},
      {"charging", machine.chargeMode().isEmpty() ? QJsonValue() : QJsonValue(machine.chargeMode())},
      {"battery", battery.value("present").toBool() ? QJsonValue(battery.value("percent").toInt()) : QJsonValue()},
      {"cpu", celsius(machine.cpuTemperature())},
      {"gpu", machine.gpuPresent() && !machine.gpuAsleep() ? celsius(machine.gpuTemperature()) : QJsonValue()},
      {"fans", fans},
      {"backlight", machine.backlight() >= 0 ? QJsonValue(machine.backlight()) : QJsonValue()},
  };
  return QJsonDocument(status).toJson(QJsonDocument::Compact);
}

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
    if (arguments.contains("--json"))
      std::printf("%s\n", statusJson(machine).constData());
    else
      std::fputs(qPrintable(statusText(machine)), stdout);
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
