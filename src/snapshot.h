#pragma once
// One reading of the machine, taken off the window's thread.
//
// Some of what Cohort shows is slow for the kernel to produce. On a Legion 5
// Pro Gen 6 with legion_laptop loaded, every fan curve attribute is a trip to
// the embedded controller at about 14ms each, thirty of them for one curve,
// and the battery's ACPI method takes up to 65ms. Read on the thread that
// draws the window, that was a half-second freeze every two seconds. So the
// reading is a plain function from a root to a value, run on a worker, and
// the window only ever applies a finished snapshot.
//
// A full reading adds what is slow or noisy to read and only changes when it
// is changed: the fan curve, which changes when Cohort writes it or the
// firmware loads a mode's own curve, and legion_laptop's switches and LEDs,
// each of which the module logs to the kernel journal on every read. Polling
// those every two seconds would cost half a second of the embedded
// controller's time and thousands of journal lines an hour. The two-second
// pass leaves them out.
#include "sensors.h"
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>

struct Snapshot {
  QString profile;
  QStringList profiles;
  QVariantList limits;
  QString chargeMode;
  QStringList chargeModes;
  QVariantMap switches;
  bool legion = false;
  bool lighting = false;
  // Whether this was a full reading. Otherwise the curve and the module's
  // switches were not read, and the last ones stand.
  bool full = false;
  QVariantMap curve;
  // The keyboard backlight's level and its top, or -1 where there is none.
  int backlight = -1;
  int backlightMax = -1;
  QVariantMap battery;
  double cpu = 0;
  double gpu = 0;
  bool gpuPresent = false;
  bool gpuAsleep = false;
  QVariantList fans;
};

// Reads everything under `root`. `sensors` is used by one capture at a time.
Snapshot capture(const std::filesystem::path &root, Sensors &sensors, bool full, bool rediscover);

// Whether a switch key belongs to the full reading.
bool fullReadOnly(const QString &key);
