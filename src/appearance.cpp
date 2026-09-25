#include "appearance.h"
#include "m3color.h"
#include "m3motion.h"
#include <QCoreApplication>
#include <cmath>

Appearance::Appearance(QObject *parent) : QObject(parent) {}

QString Appearance::theme() const {
  const QString value = m_settings.value("theme", "system").toString();
  return value == "light" || value == "dark" ? value : QStringLiteral("system");
}
void Appearance::setTheme(const QString &theme) {
  if (theme == this->theme())
    return;
  m_settings.setValue("theme", theme);
  emit changed();
}

QColor Appearance::accentColor() const { return m_settings.value("accent").value<QColor>(); }
void Appearance::setAccentColor(const QColor &color) {
  if (!color.isValid() || color == accentColor())
    return;
  m_settings.setValue("accent", color);
  m_schemes.clear();
  emit changed();
}
bool Appearance::accentChosen() const { return accentColor().isValid(); }
void Appearance::clearAccent() {
  if (!accentChosen())
    return;
  m_settings.remove("accent");
  m_schemes.clear();
  emit changed();
}

QString Appearance::colorVariant() const {
  const QString value = m_settings.value("colorVariant", "tonalSpot").toString();
  return m3::variantNames().contains(value) ? value : QStringLiteral("tonalSpot");
}
void Appearance::setColorVariant(const QString &variant) {
  if (variant == colorVariant() || !m3::variantNames().contains(variant))
    return;
  m_settings.setValue("colorVariant", variant);
  m_schemes.clear();
  emit changed();
}

double Appearance::colorContrast() const {
  return qBound(0.0, m_settings.value("colorContrast", 0.0).toDouble(), 1.0);
}
void Appearance::setColorContrast(double contrast) {
  contrast = qBound(0.0, contrast, 1.0);
  if (qFuzzyCompare(contrast + 1, colorContrast() + 1))
    return;
  m_settings.setValue("colorContrast", contrast);
  m_schemes.clear();
  emit changed();
}

bool Appearance::motion() const { return m_settings.value("motion", true).toBool(); }
void Appearance::setMotion(bool motion) {
  if (motion == this->motion())
    return;
  m_settings.setValue("motion", motion);
  emit changed();
}

QString Appearance::motionScheme() const {
  return m_settings.value("motionScheme", "expressive").toString() == "standard"
             ? QStringLiteral("standard")
             : QStringLiteral("expressive");
}
void Appearance::setMotionScheme(const QString &scheme) {
  if (scheme == motionScheme())
    return;
  m_settings.setValue("motionScheme", scheme);
  emit changed();
}

QVariantMap Appearance::colorScheme(const QColor &source, bool dark) const {
  // A scheme is a few hundred HCT solves. The window asks for the same one on
  // every binding that reads a role, so it is made once per source and theme.
  const QString key = source.name(QColor::HexArgb) + (dark ? "/d" : "/l");
  if (const auto found = m_schemes.constFind(key); found != m_schemes.cend())
    return *found;
  const auto scheme = m3::scheme(source, dark, m3::variantFor(colorVariant()), colorContrast());
  m_schemes.insert(key, scheme);
  return scheme;
}

QVariantMap Appearance::motionSprings(bool expressive) const { return m3::motionScheme(expressive); }

namespace {
// The sizes offered. Material's guidance is to scale the whole interface
// together rather than any one part of it, and these four steps keep every
// 4dp grid value within a pixel of a whole one at common display scales.
constexpr double kScales[] = {0.9, 1.0, 1.15, 1.3};
double nearestScale(double wanted) {
  double best = 1.0;
  for (double scale : kScales)
    if (std::abs(scale - wanted) < std::abs(best - wanted))
      best = scale;
  return best;
}
} // namespace

double Appearance::storedScale() {
  return nearestScale(QSettings("cohort", "cohort").value("interfaceScale", 1.0).toDouble());
}

double Appearance::interfaceScale() const {
  return nearestScale(m_settings.value("interfaceScale", 1.0).toDouble());
}
void Appearance::setInterfaceScale(double scale) {
  scale = nearestScale(scale);
  if (qFuzzyCompare(scale, interfaceScale()))
    return;
  m_settings.setValue("interfaceScale", scale);
  emit changed();
}

void Appearance::reopen() {
  m_reopen = true;
  m_settings.sync();
  QCoreApplication::quit();
}
