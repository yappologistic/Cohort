#include "appearance.h"
#include "desktoptheme.h"
#include "machine.h"
#include <QCache>
#include <QFile>
#include <QFontInfo>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSvgRenderer>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <unistd.h>
#ifdef COHORT_DIAGNOSTICS
#include "uitest.h"
#endif

// Material Symbols, tinted to the ink the caller asks for.
class Symbols : public QQuickImageProvider {
public:
  Symbols() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
    const auto parts = id.split('/');
    QString name = parts.value(0);
    if (name.contains(".."))
      return {};
    QSize s = !requested.isEmpty() ? requested : QSize(24, 24);
    s = s.boundedTo(QSize(256, 256));
    // Material draws each symbol at several optical sizes rather than scaling
    // one, so a glyph keeps its stroke weight wherever it is used. The caller
    // says how many points it wants, because the pixels depend on the display
    // and the optical size does not.
    const int points = parts.value(1).toInt();
    if (points > 0 && points <= 22)
      name += "_20";
    else if (points > 32)
      name += "_40";
    QMutexLocker lock(&m_mutex);
    const QString key = name + ":" + QString::number(s.width()) + "x" + QString::number(s.height());
    QImage img;
    if (const auto mask = m_masks.object(key)) {
      img = *mask;
    } else {
      QFile f(":/assets/icons/" + name + ".svg");
      // A symbol with no drawing at that optical size falls back to its own.
      if (!f.open(QIODevice::ReadOnly)) {
        f.setFileName(":/assets/icons/" + parts.value(0) + ".svg");
        if (!f.open(QIODevice::ReadOnly))
          return {};
      }
      QSvgRenderer svg(f.readAll());
      img = QImage(s, QImage::Format_ARGB32_Premultiplied);
      img.fill(Qt::transparent);
      QPainter painter(&img);
      svg.render(&painter);
      painter.end();
      m_masks.insert(key, new QImage(img), int(img.sizeInBytes()));
    }
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(img.rect(), QColor("#" + parts.value(2, "ffffff")));
    p.end();
    if (size)
      *size = s;
    return img;
  }

private:
  QCache<QString, QImage> m_masks{512 * 1024};
  QMutex m_mutex;
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setApplicationName("cohort");
  app.setApplicationDisplayName("Cohort");
  app.setOrganizationName("cohort");
  app.setApplicationVersion(COHORT_VERSION);
  // The desktop entry's name, which Wayland compositors take as the app id.
  app.setDesktopFileName("io.github.yappologistic.Cohort");

  // Settings live in the XDG config directory. Captures and tests point them
  // at a directory of their own, leaving the desktop's configuration, and so
  // its fonts and palette, as the person has them.
  if (qEnvironmentVariableIsSet("COHORT_CONFIG_DIR"))
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, qEnvironmentVariable("COHORT_CONFIG_DIR"));

  const auto args = app.arguments();
  if (args.contains("--version")) {
    std::fprintf(stdout, "Cohort %s\n", COHORT_VERSION);
    return 0;
  }

  // A second launch raises the window that is already open. Two windows would
  // be two sets of writes racing each other through the helper.
  const QString socket = "cohort-" + QString::number(getuid());
  const bool isolated = args.contains("--isolated") || qEnvironmentVariableIsSet("COHORT_SYS_ROOT");
  QLocalSocket peer;
  if (!isolated) {
    peer.connectToServer(socket);
    if (peer.waitForConnected(120)) {
      peer.write("raise");
      peer.flush();
      peer.waitForBytesWritten(150);
      return 0;
    }
  }
  QLocalServer server;
  if (!isolated) {
    QLocalServer::removeServer(socket);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(socket);
  }

  // Material is the style. Qt's own Material style is a reading of an older
  // specification, so every component here is built on Basic and follows the
  // current one itself.
  QQuickStyle::setStyle("Basic");
  // No font is bundled or forced. The window is set in the desktop's own
  // font, named by the family it actually resolves to rather than by an
  // alias like "Sans Serif", so a variable font's weight and width axes
  // reach the real face.
  QFont font = QGuiApplication::font();
  font.setFamily(QFontInfo(font).family());
  QGuiApplication::setFont(font);

  // A fixture tree stands in for /sys and /dev under test and for captures.
  const std::filesystem::path root = qEnvironmentVariableIsSet("COHORT_SYS_ROOT")
                                         ? qEnvironmentVariable("COHORT_SYS_ROOT").toStdString()
                                         : std::string("/");
  Appearance appearance;
  Machine machine(root);
  DesktopTheme desktopTheme;

  QQmlApplicationEngine engine;
  // The engine also looks for modules beside the binary, which in a build
  // tree finds the module CMake writes there. An installed binary has only
  // what is embedded in it, so the build tree is made to look the same: a
  // module that only loads from disk fails here, where it can be seen, and
  // not after installing.
  QStringList imports = engine.importPathList();
  imports.removeAll(QCoreApplication::applicationDirPath());
  engine.setImportPathList(imports);
  engine.addImageProvider("symbols", new Symbols);
  engine.rootContext()->setContextProperty("app", &appearance);
  engine.rootContext()->setContextProperty("machine", &machine);
  engine.rootContext()->setContextProperty("desktopTheme", &desktopTheme);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                   [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule("CohortUi", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

  QObject::connect(&server, &QLocalServer::newConnection, &app, [&server, window] {
    auto client = server.nextPendingConnection();
    QObject::connect(client, &QLocalSocket::readyRead, client, [client, window] {
      client->readAll();
      if (window) {
        window->show();
        window->raise();
        window->requestActivate();
      }
      client->deleteLater();
    });
  });

  // --page NAME and --size W H open on a destination at a size.
  if (const int page = args.indexOf("--page"); page > 0 && page + 1 < args.size())
    window->setProperty("page", args.at(page + 1));
  if (const int size = args.indexOf("--size"); size > 0 && size + 2 < args.size())
    window->resize(args.at(size + 1).toInt(), args.at(size + 2).toInt());
  // --screenshot PATH: photograph the window
  // once it has settled, and quit. For captures of fixture machines.
  if (const int at = args.indexOf("--screenshot"); at > 0 && at + 1 < args.size()) {
    const QString path = args.at(at + 1);
    QTimer::singleShot(1500, &app, [window, path] {
      window->grabWindow().save(path);
      QCoreApplication::quit();
    });
  }
  // --dump: after the window settles, print every named item's geometry
  // in window coordinates, for layout checks against Material's numbers.
  if (args.contains("--dump")) {
    QTimer::singleShot(1500, &app, [window] {
      std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item->objectName().isEmpty() && item->isVisible()) {
          const QPointF at = item->mapToScene({0, 0});
          std::printf("%s %s x=%.0f y=%.0f w=%.0f h=%.0f\n", item->metaObject()->className(),
                      qPrintable(item->objectName()), at.x(), at.y(), item->width(), item->height());
        }
        for (auto *child : item->childItems())
          walk(child);
      };
      walk(window->contentItem());
      QCoreApplication::quit();
    });
  }
#ifdef COHORT_DIAGNOSTICS
  if (const int at = args.indexOf("--ui-test"); at > 0 && at + 1 < args.size()) {
    const QString out = args.at(at + 1);
    QTimer::singleShot(0, &app, [window, out] { runUiTest(window, out); });
  }
#endif
  return app.exec();
}
