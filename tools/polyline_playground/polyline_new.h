#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ImGuiEx {

enum ImDrawFlagsExtra
{
    ImDrawFlags_JoinBevel = 1 << 9,
    ImDrawFlags_JoinRound = 2 << 9,
    ImDrawFlags_JoinMiter = 3 << 9,
    ImDrawFlags_JoinMask_ = 3 << 9,

    ImDrawFlags_CapSquare = 1 << 11,
    ImDrawFlags_CapRound  = 2 << 11,
    ImDrawFlags_CapMask_  = 3 << 11,
};

void ImDrawList_Polyline(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, ImDrawFlags draw_flags, float thickness, float miter_limit = 20.0f);

} // namespace ImGuiEx