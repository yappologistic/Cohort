#pragma once
// The laptop, as the window sees it.
//
// Every figure and every control state here is read from sysfs without
// privilege, so the window never needs root to show what the machine is
// doing. Changing something goes one of two ways:
//
// - A power mode that power-profiles-daemon also knows is set through it, over
//   D-Bus, so the desktop's own power menu and the daemon's CPU settings stay
//   in step with the firmware instead of being overridden behind their back.
// - Everything else goes to cohort-helper through pkexec, one request at a
//   time, and the result is read back from the kernel rather than assumed.
//
// Nothing is sampled while the window is hidden. A tool that keeps a laptop
// awake to report on the laptop has failed at its own job. Nothing is read on
// the window's thread either: some of these files take the kernel tens of
// milliseconds to produce (src/snapshot.h), so reads run on a worker, one at a
// time, and the window applies each finished snapshot.
#include "sensors.h"
#include "snapshot.h"
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <filesystem>
#include <functional>
#include <memory>

class QProcess;

class Machine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString model READ model CONSTANT)
  // True while the window can be seen. Sampling runs only then.
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

  // --- Power ----------------------------------------------------------------
  // The platform profile and the words this firmware accepts for it.
  Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY changed)
  Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY changed)
  // The firmware's power limits, where lenovo-wmi-other publishes them:
  // [{key, name, label, value, minimum, maximum, step}]. They only take
  // effect, and only accept a write, in the custom profile.
  Q_PROPERTY(QVariantList powerLimits READ powerLimits NOTIFY changed)

  // --- Battery --------------------------------------------------------------
  // "Long_Life", "Standard" or "Fast", in the words of the charge_types ABI,
  // whichever interface this kernel actually offers them through.
  Q_PROPERTY(QString chargeMode READ chargeMode NOTIFY changed)
  Q_PROPERTY(QStringList chargeModes READ chargeModes NOTIFY changed)
  // {present, percent, status, ac, watts, health, cycles}
  Q_PROPERTY(QVariantMap battery READ battery NOTIFY sampled)

  // --- Switches -------------------------------------------------------------
  // Every on/off control this machine has, by key, as true or false. A key
  // that is absent is a control the machine does not have.
  Q_PROPERTY(QVariantMap switches READ switches NOTIFY changed)

  // --- Thermals -------------------------------------------------------------
  Q_PROPERTY(double cpuTemperature READ cpuTemperature NOTIFY sampled)
  Q_PROPERTY(double gpuTemperature READ gpuTemperature NOTIFY sampled)
  Q_PROPERTY(bool gpuPresent READ gpuPresent NOTIFY sampled)
  Q_PROPERTY(bool gpuAsleep READ gpuAsleep NOTIFY sampled)
  Q_PROPERTY(QVariantList fans READ fans NOTIFY sampled)
  // Whether LenovoLegionLinux's legion_laptop module is loaded. Fan curves
  // and several switches exist only through it.
  Q_PROPERTY(bool legionModule READ legionModule NOTIFY changed)
  // {available, maxRpm, points: [{speed, cpu, gpu}]}. speed is fan 1's on the
  // hwmon PWM scale, 0 to 255; cpu and gpu are the upper temperatures of the
  // step, in degrees Celsius.
  Q_PROPERTY(QVariantMap fanCurve READ fanCurve NOTIFY changed)

  // --- Keyboard -------------------------------------------------------------
  Q_PROPERTY(bool lightingAvailable READ lightingAvailable NOTIFY changed)
  // {effect, speed, brightness, direction, zones: [4 x "#rrggbb"]}. The
  // keyboard cannot be asked what it shows, so this is what Cohort last sent.
  Q_PROPERTY(QVariantMap lighting READ lighting NOTIFY lightingChanged)

  // Keys with a change under way.
  Q_PROPERTY(QStringList pending READ pending NOTIFY pendingChanged)

public:
  // `root` is "/" except under test, where it is a fixture tree. A fixture
  // is written to directly: the rules the helper applies are the same code,
  // and there is no root to ask for.
  explicit Machine(std::filesystem::path root = "/", QObject *parent = nullptr);
  ~Machine() override;

  QString model() const { return m_model; }
  bool active() const { return m_active; }
  void setActive(bool active);

  QString powerProfile() const { return m_powerProfile; }
  QStringList powerProfiles() const { return m_powerProfiles; }
  QVariantList powerLimits() const { return m_powerLimits; }
  QString chargeMode() const { return m_chargeMode; }
  QStringList chargeModes() const { return m_chargeModes; }
  QVariantMap battery() const { return m_battery; }
  QVariantMap switches() const { return m_switches; }
  double cpuTemperature() const { return m_cpu; }
  double gpuTemperature() const { return m_gpu; }
  bool gpuPresent() const { return m_gpuPresent; }
  bool gpuAsleep() const { return m_gpuAsleep; }
  QVariantList fans() const { return m_fans; }
  bool legionModule() const { return m_legionModule; }
  QVariantMap fanCurve() const { return m_fanCurve; }
  bool lightingAvailable() const { return m_lightingAvailable; }
  QVariantMap lighting() const { return m_lighting; }
  QStringList pending() const { return m_pending; }

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void setPowerProfile(const QString &profile);
  Q_INVOKABLE void setChargeMode(const QString &mode);
  Q_INVOKABLE void setSwitch(const QString &key, bool on);
  Q_INVOKABLE void setPowerLimit(const QString &key, int value);
  // Fan speeds for each point of the curve, on the PWM scale. Both fans take
  // the same speeds; the firmware scales each to its own maximum.
  Q_INVOKABLE void setFanSpeeds(const QVariantList &speeds);
  Q_INVOKABLE void setLighting(const QVariantMap &lighting);

signals:
  void activeChanged();
  void changed();
  void sampled();
  void lightingChanged();
  void pendingChanged();
  // Something the person asked for did not happen, in words they can act on.
  void failed(const QString &message);

private:
  struct Request {
    QString key;         // what the pending state is shown against
    QStringList arguments; // after the helper's path
    QString what;        // how a failure names it
  };
  // Asks for a reading. One runs at a time; asking while one runs asks for
  // another after it, with the union of what was wanted.
  void requestRead(bool full, bool reapply = false, bool rediscover = false);
  void apply(const Snapshot &snapshot);
  void watchProfile();
  void enqueue(const Request &request);
  void runNext();
  void finish(const Request &request, bool ok, const QString &error);
  bool runInFixture(const Request &request, QString *error);
  void setPending(const QString &key, bool on);
  void setPowerProfileThroughDaemon(const QString &profile, const QString &daemonProfile);
  void reapplyFanCurve();
  std::string read(const std::filesystem::path &relative) const;
  QString helperPath() const;

  std::filesystem::path m_root;
  bool m_fixture;
  QString m_model;
  bool m_active = false;
  QTimer m_timer;
  QSettings m_settings;

  QString m_powerProfile;
  QStringList m_powerProfiles;
  QVariantList m_powerLimits;
  QString m_chargeMode;
  QStringList m_chargeModes;
  QVariantMap m_battery;
  QVariantMap m_switches;
  bool m_legionModule = false;
  QVariantMap m_fanCurve;
  bool m_lightingAvailable = false;
  QVariantMap m_lighting;
  QStringList m_pending;

  double m_cpu = 0;
  double m_gpu = 0;
  bool m_gpuPresent = false;
  bool m_gpuAsleep = false;
  QVariantList m_fans;

  // Used only by the capture in flight.
  Sensors m_sensors;
  QFutureWatcher<Snapshot> m_reading;
  struct Wanted {
    bool read = false, full = false, reapply = false, rediscover = false;
  } m_wanted, m_inFlight;
  // Changes that have been made and are waiting for the kernel's answer to
  // be read back. Their pending state ends when a read that started after
  // them lands, so a control never shows the old value in between.
  QStringList m_settling, m_settlingInFlight;
  std::unique_ptr<QSocketNotifier> m_profileNotifier;
  int m_profileFd = -1;

  QList<Request> m_queue;
  bool m_running = false;
  QTimer m_lightingDebounce;
  QTimer m_curveReapply;
};
