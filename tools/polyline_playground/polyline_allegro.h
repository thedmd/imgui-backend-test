#pragma once

#include <imgui.h>
#include <imgui_internal.h>


using ALLEGRO_COLOR = ImU32;

enum ALLEGRO_LINE_JOIN
{
    ALLEGRO_LINE_JOIN_NONE,
    ALLEGRO_LINE_JOIN_BEVEL,
    ALLEGRO_LINE_JOIN_ROUND,
    ALLEGRO_LINE_JOIN_MITER,
    ALLEGRO_LINE_JOIN_MITRE = ALLEGRO_LINE_JOIN_MITER
};

enum ALLEGRO_LINE_CAP
{
    ALLEGRO_LINE_CAP_NONE,
    ALLEGRO_LINE_CAP_SQUARE,
    ALLEGRO_LINE_CAP_ROUND,
    ALLEGRO_LINE_CAP_TRIANGLE,
    ALLEGRO_LINE_CAP_CLOSED
};

void imgui_al_draw_polyline(ImDrawList* draw_list, const float* vertices, int vertex_stride,
    int vertex_count, int join_style, int cap_style,
    ALLEGRO_COLOR color, float thickness, float miter_limit);
