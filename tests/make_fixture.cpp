// Writes one of the fixture machines to a directory, for captures:
//   cohort-fixture DIR mainline|legion|older
// then COHORT_SYS_ROOT=DIR cohort shows that machine.
#include "fixture.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc != 3) {
    std::fputs("usage: cohort-fixture DIR mainline|legion|older\n", stderr);
    return 64;
  }
  const QString dir = QString::fromLocal8Bit(argv[1]);
  const QByteArray kind = argv[2];
  if (kind == "mainline")
    fixture::mainline(dir);
  else if (kind == "legion")
    fixture::withLegion(dir);
  else if (kind == "older")
    fixture::olderKernel(dir);
  else
    return 64;
  return 0;
}
