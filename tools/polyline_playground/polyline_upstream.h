#pragma once

#include <imgui.h>

namespace ImGuiEx {

void ImDrawList_Polyline_Upstream(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, ImDrawFlags draw_flags, float thickness);

} // namespace ImGuiEx