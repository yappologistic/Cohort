#pragma once
// cohort --background: keeps the settings applied while the window is
// closed.
//
// The firmware forgets some of what Cohort sets: it loads each power mode's
// own fan curve on every mode change and on boot, and the keyboard can come
// back from a restart in its default lighting. The agent is a windowless
// Machine that puts them back, at login, after resuming from sleep and after
// a mode change, and follows the charger for the automatic power mode.
//
// It is started at login by the XDG autostart entry
// (assets/io.github.yappologistic.Cohort.Agent.desktop), and by the window
// when it finds none running, for desktops that do not run autostart
// entries. It exits at once when the person has turned it off. One runs per
// user; it answers on a local socket, which is how the window finds it and
// how it is told to stop.
#include <QString>
#include <filesystem>

namespace agent {

bool running();
// Starts one if none is running.
void start();
// Tells a running one to stop.
void stop();
// The agent's own main loop.
int run(const std::filesystem::path &root);

} // namespace agent
