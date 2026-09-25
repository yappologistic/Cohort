#pragma once
// Commands for keybinds and scripts, which change one thing and exit:
//
//   cohort --mode quiet|balanced|performance|extreme|custom|next
//   cohort --charge conservation|standard|rapid
//   cohort --lighting off|static|breath|wave|smooth
//   cohort --backlight off|low|high|0|1|2
//   cohort --status
//
// Each goes through the same Machine and helper as the window, waits for the
// kernel's answer to be read back, and exits 0 when the change took, 1 when
// it was refused (with the reason on stderr), and 64 for a word it does not
// know.
#include <QStringList>
#include <filesystem>

namespace cli {

// Whether the arguments ask for a command rather than the window.
bool wanted(const QStringList &arguments);
int run(const QStringList &arguments, const std::filesystem::path &root);

} // namespace cli
