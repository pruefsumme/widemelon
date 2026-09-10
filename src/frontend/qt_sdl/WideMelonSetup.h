// WideMelon's small native startup dialog.
// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CLI.h"

class QWidget;

namespace WideMelon
{

// Shows the WideMelon configuration dialog unless a profile was supplied through
// the environment for automated/headless use. Returns false when the user
// cancels startup.
bool Configure(CLI::CommandLineOptions& options);

// Opens the saved WideMelon profile from the regular melonDS window. Renderer
// dimensions are process-wide, so changes take effect on the next launch.
void OpenSettings(QWidget* parent);

// Applies a profile supplied through WIDEMELON_* environment variables.
void ApplyEnvironmentProfile();

}
