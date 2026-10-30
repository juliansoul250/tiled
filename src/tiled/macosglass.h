/* Native macOS sidebar material for this fork. GPL-2.0-or-later. */
#pragma once

#include "tilededitor_global.h"

class QWidget;

namespace Tiled::MacGlass {
// Install before showing a top-level window. No-op on non-macOS platforms.
TILED_EDITOR_EXPORT void install(QWidget *window);
TILED_EDITOR_EXPORT void refresh(QWidget *window);
// Queries the installed AppKit view, not just a requested Qt property.
TILED_EDITOR_EXPORT bool isActive(QWidget *window);
}
