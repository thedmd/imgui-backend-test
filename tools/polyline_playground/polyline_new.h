#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace ImGuiEx {

enum ImDrawFlagsExtra_
{
//    ImDrawFlags_JoinMiter       = 1 << 9,
//    ImDrawFlags_JoinMiterClip   = 2 << 9,
//    ImDrawFlags_JoinBevel       = 3 << 9,
//    ImDrawFlags_JoinRound       = 4 << 9,
//    ImDrawFlags_JoinDefault_    = ImDrawFlags_JoinMiter,
//    ImDrawFlags_JoinMask_       = 7 << 9,
//
//    ImDrawFlags_CapNone         = 1 << 12,
//    ImDrawFlags_CapButt         = 2 << 12,
//    ImDrawFlags_CapSquare       = 3 << 12,
//    ImDrawFlags_CapRound        = 4 << 12,
//    ImDrawFlags_CapDefault_     = ImDrawFlags_CapButt,
//    ImDrawFlags_CapMask_        = 7 << 12,
};

void ImDrawList_Polyline(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, ImDrawFlags draw_flags, float thickness, float miter_limit = 2.0f);
void ImDrawList_Polyline_Optimized(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, ImDrawFlags draw_flags, float thickness, float miter_limit = 2.0f);
void ImDrawList_Polyline_V3(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, ImDrawFlags draw_flags, float thickness, float miter_limit = 2.0f);

} // namespace ImGuiEx