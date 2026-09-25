#pragma once
// The simulated user. Built into the diagnostic binary only
// (-DCOHORT_DIAGNOSTICS=ON), run with --ui-test OUTDIR against a fixture
// machine (COHORT_SYS_ROOT), and driven the way a person drives the window:
// clicks and drags at the coordinates of what is on screen, key presses, and
// waits for the fixture's files to say the kernel was told. It photographs
// each state into OUTDIR and exits non-zero on the first failure.
class QQuickWindow;
class QString;

void runUiTest(QQuickWindow *window, const QString &out);

// How long the window's thread is kept from drawing while the machine is
// sampled, on this machine: a timer asks for every frame for ten seconds with
// the Fans page showing, and the longest gap between ticks is reported. A
// stall over 50ms, three frames at 60Hz, fails.
void runLatencyTest(QQuickWindow *window);
