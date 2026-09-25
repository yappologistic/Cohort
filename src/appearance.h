#pragma once
// How the window looks and moves, which Theme.qml reads as `app`.
//
// Kept apart from the machine: nothing here touches hardware, and nothing the
// hardware layer does depends on a colour. Stored with QSettings in the XDG
// config directory, never in the source tree.
#include <QColor>
#include <QObject>
#include <QSettings>
#include <QVariantMap>

class Appearance : public QObject {
  Q_OBJECT
  // "system", "light" or "dark". System follows the desktop's palette file
  // where it publishes one, and the platform's colour scheme otherwise.
  Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
  Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY changed)
  // Whether an accent was chosen at all, said outright rather than left to
  // whether an unset colour happens to read as false.
  Q_PROPERTY(bool accentChosen READ accentChosen NOTIFY changed)
  Q_PROPERTY(QString colorVariant READ colorVariant WRITE setColorVariant NOTIFY changed)
  Q_PROPERTY(double colorContrast READ colorContrast WRITE setColorContrast NOTIFY changed)
  // False asks every animation to finish at once, for people who find motion
  // uncomfortable.
  Q_PROPERTY(bool motion READ motion WRITE setMotion NOTIFY changed)
  // "expressive" or "standard", Material's two motion schemes.
  Q_PROPERTY(QString motionScheme READ motionScheme WRITE setMotionScheme NOTIFY changed)

public:
  explicit Appearance(QObject *parent = nullptr);

  QString theme() const;
  void setTheme(const QString &theme);
  QColor accentColor() const;
  void setAccentColor(const QColor &color);
  bool accentChosen() const;
  Q_INVOKABLE void clearAccent();
  QString colorVariant() const;
  void setColorVariant(const QString &variant);
  double colorContrast() const;
  void setColorContrast(double contrast);
  bool motion() const;
  void setMotion(bool motion);
  QString motionScheme() const;
  void setMotionScheme(const QString &scheme);

  // Every Material colour role for a source colour, at the chosen variant and
  // contrast (src/m3color.cpp).
  Q_INVOKABLE QVariantMap colorScheme(const QColor &source, bool dark) const;
  // Material's six springs as Qt Quick curves and durations (src/m3motion.cpp).
  Q_INVOKABLE QVariantMap motionSprings(bool expressive) const;

signals:
  void changed();

private:
  QSettings m_settings;
  mutable QHash<QString, QVariantMap> m_schemes;
};
