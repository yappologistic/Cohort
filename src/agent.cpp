#include "agent.h"
#include "machine.h"
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QProcess>
#include <unistd.h>

namespace agent {
namespace {

// One agent per user, and one per fixture machine under test, so an agent
// watching a fixture never answers for the real one or the other way round.
QString socketName() {
  QString name = "cohort-agent-" + QString::number(getuid());
  if (qEnvironmentVariableIsSet("COHORT_SYS_ROOT"))
    name += "-" + QString::number(qHash(qEnvironmentVariable("COHORT_SYS_ROOT")), 16);
  return name;
}

} // namespace

bool running() {
  QLocalSocket probe;
  probe.connectToServer(socketName());
  return probe.waitForConnected(150);
}

void start() {
  if (running())
    return;
  // Detached with nothing of the caller's attached: an agent that kept a
  // terminal's output open would keep that terminal's command from ending.
  QProcess process;
  process.setProgram(QCoreApplication::applicationFilePath());
  process.setArguments({"--background"});
  process.setStandardInputFile(QProcess::nullDevice());
  process.setStandardOutputFile(QProcess::nullDevice());
  process.setStandardErrorFile(QProcess::nullDevice());
  process.startDetached();
}

void stop() {
  QLocalSocket peer;
  peer.connectToServer(socketName());
  if (!peer.waitForConnected(150))
    return;
  peer.write("quit");
  peer.flush();
  peer.waitForBytesWritten(150);
}

int run(const std::filesystem::path &root) {
  Machine machine(root);
  // Turned off in Settings: the autostart entry still starts it, and it
  // leaves at once.
  if (!machine.background())
    return 0;
  // One agent at a time is decided by a lock file, not by the socket:
  // QLocalServer with UserAccessOption builds its socket elsewhere and
  // renames it over the name, so a second listen() takes the name from the
  // first instead of failing. QLockFile holds the owner's PID, so a lock
  // left by an agent that crashed is recognised as stale and taken over.
  QLockFile lock(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/" + socketName() + ".lock");
  lock.setStaleLockTime(0);
  if (!lock.tryLock(0))
    return 0;
  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
  QLocalServer::removeServer(socketName());
  if (!server.listen(socketName()))
    return 1;
  QObject::connect(&server, &QLocalServer::newConnection, [&server] {
    auto *client = server.nextPendingConnection();
    QObject::connect(client, &QLocalSocket::readyRead, client, [client] {
      if (client->readAll().contains("quit"))
        QCoreApplication::quit();
    });
    QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
  });
  machine.setRestoring(true);
  // Login is the first of the moments the settings may have been lost.
  machine.restore();
  return QCoreApplication::exec();
}

} // namespace agent
