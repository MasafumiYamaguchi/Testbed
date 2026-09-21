#pragma once
#include "white/persistence.hpp"
#include <string>

namespace white {
// Draw within an existing ImGui window. Actions publish once through the shared
// EditorSession; caller disables actions during a drag or non-cell selection.
void draw_cell_actions_ui(EditorSession&,Id& selected,bool enabled,std::string& status);
}
