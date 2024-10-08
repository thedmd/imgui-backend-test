//#define IMGUI_DEFINE_MATH_OPERATORS
#include "polyline_new.h"

#if ENABLE_TRACY
#include "Tracy.hpp"
#define ImZoneScoped ZoneScoped
#else
#define ImZoneScoped (void)0
#endif

namespace ImGuiEx {

#if (defined(__cplusplus) && (__cplusplus >= 202002L)) || (defined(_MSVC_LANG) && (_MSVC_LANG >= 202002L))
#define IM_LIKELY   [[likely]]
#define IM_UNLIKELY [[unlikely]]
#else
#define IM_LIKELY
#define IM_UNLIKELY
#endif

#if defined(IMGUI_ENABLE_SSE) && !defined(_DEBUG) // In debug mode 'ImSqrt' run faster than single step Newton-Raphson method
static inline float ImRsqrtSSE2Precise(float x)
{
    const float r = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    // Converge to more precise solution using single step of Newton-Raphson method, repeating increase precision
    return r * (1.5f - x * 0.5f * r * r);
}
#define ImRsqrtPrecise(x)                   ImRsqrtSSE2Precise(x)
#else
#define ImRsqrtPrecise(x)                   (1.0f / ImSqrt(x))
#endif

#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; IM_LIKELY if (d2 > 0.0f) { float inv_len = ImRsqrtPrecise(d2); VX *= inv_len; VY *= inv_len; } } (void)0
#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

#define IM_POLYLINE_VERTEX(N, X, Y, UV, C)                      \
    {                                                           \
        vtx_write[N].pos.x = X;                                 \
        vtx_write[N].pos.y = Y;                                 \
        vtx_write[N].uv    = UV;                                \
        vtx_write[N].col   = C;                                 \
    }

#define IM_POLYLINE_VERTEX_END(N)

#define IM_POLYLINE_TRIANGLE_BEGIN(N)

#if !defined(ImDrawIdx)
static_assert(4 * sizeof(ImDrawIdx) == sizeof(ImU64), "ImU64 must fit 4 indices");
#define IM_POLYLINE_TRIANGLE_EX(N, Z, A, B, C)                  \
        *(ImU64*)(idx_write) = (ImU64)((Z) + (A)) | ((ImU64)((Z) + (B)) << 16) | ((ImU64)((Z) + (C)) << 32); \
        idx_write += 3
#else
static_assert(2 * sizeof(ImDrawIdx) == sizeof(ImU64), "ImU64 must fit 2 indices");
#define IM_POLYLINE_TRIANGLE_EX(N, Z, A, B, C)                  \
        ((ImU64*)(idx_write))[0] = (ImU64)((Z) + (A)) | ((ImU64)((Z) + (B)) << 32); \
        ((ImU64*)(idx_write))[1] = (ImU64)((Z) + (C)); \
        idx_write += 3
#endif
#define IM_POLYLINE_TRIANGLE(N, A, B, C) IM_POLYLINE_TRIANGLE_EX(N, idx_start, A, B, C)

#define IM_POLYLINE_TRIANGLE_END(M)

#define IM_POLYLINE_BEVEL_NORMAL(BX, BY)    \
    {                                       \
        BX = n0.x + n1.x;                   \
        BY = n0.y + n1.y;                   \
        IM_NORMALIZE2F_OVER_ZERO(BX, BY);   \
    }

#define IM_POLYLINE_BEVEL_VECTORS(BX, BY, D0X, D0Y, D1X, D1Y, THICKNESS) \
    {                                                                    \
        D0X = (n0.x + BX) * 0.5f;                                        \
        D0Y = (n0.y + BY) * 0.5f;                                        \
        D1X = (n1.x + BX) * 0.5f;                                        \
        D1Y = (n1.y + BY) * 0.5f;                                        \
        IM_FIXNORMAL2F(D0X, D0Y);                                        \
        IM_FIXNORMAL2F(D1X, D1Y);                                        \
        D0X *= THICKNESS;                                                \
        D0Y *= THICKNESS;                                                \
        D1X *= THICKNESS;                                                \
        D1Y *= THICKNESS;                                                \
    }

#define IM_POLYLINE_BEVEL_GEOMETRY(PX, PY, D0X, D0Y, D1X, D1Y, THICKNESS) \
    {                                                                     \
        PX  = p1.x;                                                       \
        PY  = p1.y;                                                       \
        D0X = n0.x * THICKNESS;                                           \
        D0Y = n0.y * THICKNESS;                                           \
        D1X = n1.x * THICKNESS;                                           \
        D1Y = n1.y * THICKNESS;                                           \
    }

#define IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY(PX, PY, D0X, D0Y, D1X, D1Y, THICKNESS, LIMIT) \
    {                                                                                    \
        const float signed_miter_distance_limit = sin_theta < 0.0f ? LIMIT : -LIMIT;     \
        const float offset = (n0.x * (bevel_normal_x * LIMIT - n0.x * THICKNESS) + n0.y * (bevel_normal_y * LIMIT - n0.y * THICKNESS)) / (n0.y * bevel_normal_x - n0.x * bevel_normal_y); \
                                                                                         \
        PX  = p1.x - bevel_normal_x * signed_miter_distance_limit;                       \
        PY  = p1.y - bevel_normal_y * signed_miter_distance_limit;                       \
        D0X =  offset * bevel_normal_y;                                                  \
        D0Y = -offset * bevel_normal_x;                                                  \
        D1X = -offset * bevel_normal_y;                                                  \
        D1Y =  offset * bevel_normal_x;                                                  \
    }

#define IM_POLYLINE_ARC(C, R, A, AL)                                                    \
    {                                                                                   \
        const int path_size = draw_list->_Path.Size;                                    \
        draw_list->_Path.resize(path_size + 2);                                         \
        draw_list->_Path[path_size + 1] = C;                                            \
        const float a0 = A;                                                             \
        draw_list->PathArcTo(C, R, a0, a0 + (AL));                                      \
        int arc_vtx_count = draw_list->_Path.Size - path_size - 1;                      \
        draw_list->_Path[path_size].x = (float)arc_vtx_count;                           \
        draw_list->_Path[path_size].y = 0.0f;                                           \
        draw_list->_Path[0].y += draw_list->_Path[path_size].x;                         \
        ++arc_count;                                                                    \
    }


struct ImDrawList_Polyline_V3_Context
{
    const ImVec2* points;
    const ImVec2* normals;
    const float*  segments_length_sqr;
    int           point_count;
    ImU32         color;
    ImU32         fringe_color;
    float         thickness;
    float         fringe_thickness;
    float         fringe_width;
    float         miter_limit;
    ImDrawFlags   join;
    ImDrawFlags   cap;
    bool          closed;
};

#define IM_POLYLINE_MITER_ANGLE_LIMIT -0.9999619f // cos(179.5)



static inline void ImDrawList_Polyline_V3_EmitArcs(ImDrawList* draw_list, const ImDrawList_Polyline_V3_Context& context, const int arc_count, const ImU32 color)
{
    const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;

    const ImVec2* arc_data     = draw_list->_Path.Data;
    const ImVec2* arc_data_end = draw_list->_Path.Data + draw_list->_Path.Size;

    const int arc_total_vtx_count = (int)arc_data->y;
    const int arc_total_idx_count = (arc_total_vtx_count - arc_count * 2) * 3;

    draw_list->PrimReserve(arc_total_idx_count + 1, arc_total_vtx_count); // +1 to avoid write in non-reserved memory

    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int idx_start = draw_list->_VtxCurrentIdx;

    while (arc_data < arc_data_end)
    {
        const int arc_vtx_count = (int)arc_data->x;
        const int arc_tri_count = (arc_vtx_count - 2);
        const int arc_idx_count = arc_tri_count * 3;

        ++arc_data;

        IM_POLYLINE_VERTEX(0, arc_data[0].x, arc_data[0].y, uv, context.color);
        for (int i = 1; i < arc_vtx_count; ++i)
        {
            IM_POLYLINE_VERTEX(i, arc_data[i].x, arc_data[i].y, uv, color);
        }

        arc_data += arc_vtx_count;

        IM_POLYLINE_TRIANGLE_BEGIN(arc_idx_count);
        for (int i = 0; i < arc_tri_count; ++i)
        {
            IM_POLYLINE_TRIANGLE(i, 0, i + 2, i + 1);
        }
        IM_POLYLINE_TRIANGLE_END(arc_idx_count);

        vtx_write += arc_vtx_count;
        idx_start += arc_vtx_count;
    }

    const int used_idx_count = (int)(idx_write - draw_list->_IdxWritePtr);

    IM_ASSERT(used_idx_count >= 0);

    draw_list->_VtxWritePtr   = vtx_write;
    draw_list->_IdxWritePtr   = idx_write;
    draw_list->_VtxCurrentIdx = idx_start;

    draw_list->PrimUnreserve(arc_total_idx_count - used_idx_count + 1, 0);

    draw_list->_Path.Size = 0;
}

static inline void ImDrawList_Polyline_V3_EmitArcsWithFringe(ImDrawList* draw_list, const ImDrawList_Polyline_V3_Context& context, const int arc_count)
{
    const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;

    const ImVec2* arc_data     = draw_list->_Path.Data;
    const ImVec2* arc_data_end = draw_list->_Path.Data + draw_list->_Path.Size;

    const int arc_total_pts_count = (int)arc_data->y;
    const int arc_total_vtx_count = arc_total_pts_count * 2 - arc_count; // 1 center vertex per arc, 2 fringe vertices per arc
    const int arc_total_idx_count = (arc_total_pts_count - arc_count * 2) * 3 * 3; // 3 indices per triangle, 3 triangles per arc segment

    draw_list->PrimReserve(arc_total_idx_count + 1, arc_total_vtx_count); // +1 to avoid write in non-reserved memory

    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int idx_start = draw_list->_VtxCurrentIdx;

    const float thickness_scale = context.thickness / context.fringe_thickness;

    while (arc_data < arc_data_end)
    {
        const int arc_pts_count = (int)arc_data->x;
        const int arc_vtx_count = arc_pts_count * 2 - 1;
        const int arc_tri_count = (arc_pts_count - 2) * 3;
        const int arc_idx_count = arc_tri_count * 3;

        ++arc_data;

        IM_POLYLINE_VERTEX(0, arc_data[0].x, arc_data[0].y, uv, context.color);
        for (int i = 1; i < arc_pts_count; ++i)
        {
            const ImVec2 outer = arc_data[i];
            const ImVec2 inner = ImVec2((outer.x - arc_data->x) * thickness_scale + arc_data->x, (outer.y - arc_data->y) * thickness_scale + arc_data->y);

            IM_POLYLINE_VERTEX(i * 2 - 1,  inner.x,  inner.y, uv, context.color);
            IM_POLYLINE_VERTEX(i * 2 + 0, outer.x, outer.y, uv, context.fringe_color);
        }

        arc_data += arc_pts_count;

        IM_POLYLINE_TRIANGLE_BEGIN(arc_idx_count);
        for (int i = 0; i < arc_tri_count / 3; ++i)
        {
            const int base = i * 2;
            IM_POLYLINE_TRIANGLE(i,        0, base + 3, base + 1);
            IM_POLYLINE_TRIANGLE(i, base + 1, base + 3, base + 4);
            IM_POLYLINE_TRIANGLE(i, base + 1, base + 4, base + 2);
        }
        IM_POLYLINE_TRIANGLE_END(arc_idx_count);

        vtx_write += arc_vtx_count;
        idx_start += arc_vtx_count;
    }

    const int used_idx_count = (int)(idx_write - draw_list->_IdxWritePtr);

    IM_ASSERT(used_idx_count >= 0);

    draw_list->_VtxWritePtr   = vtx_write;
    draw_list->_IdxWritePtr   = idx_write;
    draw_list->_VtxCurrentIdx = idx_start;

    draw_list->PrimUnreserve(arc_total_idx_count - used_idx_count + 1, 0);

    draw_list->_Path.Size = 0;
}

// Simplified version of thick line renderer that does render only fringe
// Differences:
//   - miter-clip always collapse to bevel
//   - geometry does not have solid part in the middle
//   - join picking logic is simplified
static inline void ImDrawList_Polyline_V3_Thin_AntiAliased(ImDrawList* draw_list, const ImDrawList_Polyline_V3_Context& context)
{
    enum JoinType { Miter, Butt, Bevel, Round, ThickButt };

    const JoinType default_join       = (context.join == ImDrawFlags_JoinBevel) ? Bevel : (context.join == ImDrawFlags_JoinRound ? Round : Miter);
    const JoinType default_join_limit = (context.join == ImDrawFlags_JoinRound) ? Round : Bevel;

    const float half_thickness           = context.fringe_thickness * 0.5f;
    const float miter_distance_limit     = half_thickness * context.miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const ImVec2 uv         = draw_list->_Data->TexUvWhitePixel;
    const int    vtx_count  = (context.point_count * 6 + 3);          // top 6 vertices per join, 3 vertices for butt cap
    const int    idx_count  = (context.point_count * 6 + 2) * 3 + 1;  // top 6 triangles per join, 2 for square cap, 1 index to avoid write in non-reserved memory

    draw_list->PrimReserve(idx_count, vtx_count);

    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int idx_start = draw_list->_VtxCurrentIdx;

    int arc_count = 0;

    ImVec2 p0 = context.points [context.closed ? context.point_count - 1 : 0];
    ImVec2 n0 = context.normals[context.closed ? context.point_count - 1 : 0];

    ImVec2 p1, n1;

    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_thickness, p0.y - n0.y * half_thickness, uv, context.fringe_color);
    IM_POLYLINE_VERTEX(1, p0.x,                         p0.y,                         uv, context.color);
    IM_POLYLINE_VERTEX(2, p0.x + n0.x * half_thickness, p0.y + n0.y * half_thickness, uv, context.fringe_color);

    for (int i = context.closed ? 0 : 1; i < context.point_count; ++i, p0 = p1, n0 = n1)
    {
        p1 = context.points[i];
        n1 = context.normals[i];

        // theta is the angle between two segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const float  miter_scale_factor = (cos_theta > IM_POLYLINE_MITER_ANGLE_LIMIT) ? half_thickness / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const float  miter_offset_x     = (n0.x + n1.x) * miter_scale_factor;
        const float  miter_offset_y     = (n0.y + n1.y) * miter_scale_factor;
        const float  miter_distance_sqr = miter_offset_x * miter_offset_x + miter_offset_y * miter_offset_y;

        const bool   overlap          = (context.segments_length_sqr[i] < miter_distance_sqr) || (context.segments_length_sqr[i + 1] < miter_distance_sqr) || (cos_theta <= IM_POLYLINE_MITER_ANGLE_LIMIT);
        const bool   continuous       = context.closed || i != context.point_count - 1;

        const JoinType preferred_join = continuous ? (miter_distance_sqr > miter_distance_limit_sqr ? default_join_limit : default_join) : Butt;
        const JoinType join           = overlap ? (continuous ? ThickButt : Butt) : preferred_join;

        //
        // Miter and Butt joins have same geometry, only difference is in location of the vertices
        // 
        //   3    4    5
        //   +----+----+
        //   |  .'|'.  |
        //   |.'  |  '.|
        //   + ~  +  ~ +
        //   0    1    2
        //
        IM_LIKELY if (join == Miter)
        {
            IM_POLYLINE_VERTEX(3, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(4, p1.x,                  p1.y,                  uv, context.color);
            IM_POLYLINE_VERTEX(5, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.fringe_color);
            vtx_write += 3;

            IM_POLYLINE_TRIANGLE_BEGIN(12);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 4);
            IM_POLYLINE_TRIANGLE(1, 0, 4, 3);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 4);
            IM_POLYLINE_TRIANGLE(3, 2, 5, 4);
            IM_POLYLINE_TRIANGLE_END(12);
            idx_start += 3;
        }
        else if (join == Butt)
        {
            IM_POLYLINE_VERTEX(3, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(4, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(5, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.fringe_color);
            vtx_write += 3;

            IM_POLYLINE_TRIANGLE_BEGIN(12);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 4);
            IM_POLYLINE_TRIANGLE(1, 0, 4, 3);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 4);
            IM_POLYLINE_TRIANGLE(3, 2, 5, 4);
            IM_POLYLINE_TRIANGLE_END(12);
            idx_start += 3;
        }
        else if (join == Bevel)
        {
            //
            // Bevel geometry depends on the sign of the bend direction.
            //
            // Left bevel:              Right bevel:
            //
            //         4  .+          |          +.  6
            //          +'  ~         |         ~  '+
            //        .'|    .+       |       +.    |'.
            //      .' 5|  .'   ~     |     ~   '.  |5 '.
            //   3 +----+:'      .+   |   +.      ':+----+ 3
            //     |   '|''.   .'     |     '.   .''|'   |
            //     |  ' | ' '+'       |       '+' ' | '  |
            //     |.'  |  '.|6       |       4|.'  |  '.|
            //     + ~  +  ~ +        |        + ~  +  ~ +
            //     0    1    2        |        0    1    2
            //
            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            float bevel_normal_x, bevel_normal_y;
            IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

            float dir_0_x, dir_0_y, dir_1_x, dir_1_y;
            IM_POLYLINE_BEVEL_VECTORS(bevel_normal_x, bevel_normal_y, dir_0_x, dir_0_y, dir_1_x, dir_1_y, half_thickness);

            if (sin_theta < 0.0f)
            {
                // Left bevel
                IM_POLYLINE_VERTEX(3, p1.x -        dir_0_x, p1.y -        dir_0_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(4, p1.x -        dir_1_x, p1.y -        dir_1_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(5, p1.x,                  p1.y,                  uv, context.color);
                IM_POLYLINE_VERTEX(6, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.fringe_color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(15);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 3);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 5);
                IM_POLYLINE_TRIANGLE(3, 2, 6, 5);
                IM_POLYLINE_TRIANGLE(4, 3, 5, 4);
                IM_POLYLINE_TRIANGLE_END(15);
                idx_start += 4;
            }
            else
            {
                // Right bevel
                IM_POLYLINE_VERTEX(3, p1.x +        dir_0_x, p1.y +        dir_0_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(4, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(5, p1.x,                  p1.y,                  uv, context.color);
                IM_POLYLINE_VERTEX(6, p1.x +        dir_1_x, p1.y +        dir_1_y, uv, context.fringe_color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(15);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 5);
                IM_POLYLINE_TRIANGLE(3, 2, 3, 5);
                IM_POLYLINE_TRIANGLE(4, 3, 5, 6);
                IM_POLYLINE_TRIANGLE_END(15);
                idx_start += 4;
            }
        }
        else IM_UNLIKELY if (join == Round)
        {
            //
            // Round geometry depends on the sign of the bend direction.
            //
            // Left bevel:              Right bevel:
            //
            //         4  .+          |          +.  6
            //        .-+'  ~         |         ~  '+-.
            //      .'  |    .+       |       +.    |  '.
            //     :    |5 .'   ~     |     ~   '. 5|    :
            //   3 +----+:'      .+   |   +.      ':+----+ 3
            //     |   '|''.   .'     |     '.   .''|'   |
            //     |  ' | ' '+'       |       '+' ' | '  |
            //     |.'  |  '.|6       |       4|.'  |  '.|
            //     + ~  +  ~ +        |        + ~  +  ~ +
            //     0    1    2        |        0    1    2
            //
            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                // Left bevel
                IM_POLYLINE_VERTEX(3, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(4, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(5, p1.x,                         p1.y,                         uv, context.color);
                IM_POLYLINE_VERTEX(6, p1.x + miter_offset_x,        p1.y + miter_offset_y,        uv, context.fringe_color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(13);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 3);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 5);
                IM_POLYLINE_TRIANGLE(3, 2, 6, 5);
                IM_POLYLINE_TRIANGLE_END(13);
                idx_start += 4;
            }
            else
            {
                IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                // Right bevel
                IM_POLYLINE_VERTEX(3, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(4, p1.x - miter_offset_x,        p1.y - miter_offset_y,        uv, context.fringe_color);
                IM_POLYLINE_VERTEX(5, p1.x,                         p1.y,                         uv, context.color);
                IM_POLYLINE_VERTEX(6, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.fringe_color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(13);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 5);
                IM_POLYLINE_TRIANGLE(3, 2, 3, 5);
                IM_POLYLINE_TRIANGLE_END(13);
                idx_start += 4;
            }
        }
        else if (join == ThickButt)
        {
            // Thick butt end one segment with Butt cap and begin next segment with Butt cap
            //
            // Two segments do overlap causing overdraw.
            //
            //   5'       ,+         
            //  +    6  .'   ~        
            //        +'      .+      
            //         '.   .'   ~    
            //      +----+:'--+   .+  
            //     3|   7| '.4| .'    
            //      |    |   '+'      
            //      |    |    |8      
            //      + ~  +  ~ +       
            //      0    1    2
            //
            // Gap between segments is filled according to preferred join type.
            // Vertex 5' is reserved for join geometry.

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            IM_POLYLINE_VERTEX(3, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(4, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(5, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(6, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(7, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(8, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.fringe_color);

            IM_POLYLINE_TRIANGLE_BEGIN(12);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 7);
            IM_POLYLINE_TRIANGLE(1, 0, 7, 3);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 7);
            IM_POLYLINE_TRIANGLE(3, 2, 4, 7);
            IM_POLYLINE_TRIANGLE_END(12);

            IM_UNLIKELY if ((cos_theta <= IM_POLYLINE_MITER_ANGLE_LIMIT) && ((preferred_join == Miter) || (preferred_join == Bevel)))
            {
                // Thin square cap
                // 
                //        7
                //   ~ ~  +  ~ ~
                //   |  .' '.  |
                //   |.'     '.|
                //   +---------+
                //   6         8
                //
                // 1 join triangles
                //
                vtx_write[6].pos.x -= n1.y * context.fringe_width;
                vtx_write[6].pos.y += n1.x * context.fringe_width;
                vtx_write[8].pos.x -= n1.y * context.fringe_width;
                vtx_write[8].pos.y += n1.x * context.fringe_width;

                IM_POLYLINE_TRIANGLE_BEGIN(3);
                IM_POLYLINE_TRIANGLE(0, 6, 8, 7);
                IM_POLYLINE_TRIANGLE_END(3);
            }
            else if (preferred_join == Miter)
            {
                // Fill gap between segments with Miter join
                // 
                // Left Miter join:        Right Miter join:
                // 
                //            ,          |              ,
                //       6  .'    ~      |          ~    '.  8
                //    5'  .+.            |                .+.  '5
                //      +:.  '.     .'   |       '.     .'  .:+
                //      |  ''..:. .'     |         '. .:..''  |
                //      +--------+-- ~   |       ~ --+--------+
                //     3|       7|       |           |7       |4
                //

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_VERTEX(5, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.fringe_color);

                    IM_POLYLINE_TRIANGLE_BEGIN(6);
                    IM_POLYLINE_TRIANGLE(0, 3, 7, 5);
                    IM_POLYLINE_TRIANGLE(1, 5, 7, 6);
                    IM_POLYLINE_TRIANGLE_END(6);
                }
                else
                {
                    IM_POLYLINE_VERTEX(5, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.fringe_color);

                    IM_POLYLINE_TRIANGLE_BEGIN(6);
                    IM_POLYLINE_TRIANGLE(0, 4, 5, 7);
                    IM_POLYLINE_TRIANGLE(1, 5, 8, 7);
                    IM_POLYLINE_TRIANGLE_END(6);
                }
            }
            else if (preferred_join == Bevel)
            {
                // Fill gap between segments with Bevel join
                //
                // Left Bevel join:       Right Bevel join:
                //
                //            ,          |             ,
                //       6  .'    ~      |         ~    '.  8
                //         +.            |               .+
                //        '  '.     .'   |      '.     .'  '
                //       '     '. .'     |        '. .'     '
                //      +--------+-- ~   |      ~ --+--------+
                //     3|       7|       |          |7       |4
                //

                float bevel_normal_x, bevel_normal_y;
                IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

                float dir_0_x, dir_0_y, dir_1_x, dir_1_y;
                IM_POLYLINE_BEVEL_VECTORS(bevel_normal_x, bevel_normal_y, dir_0_x, dir_0_y, dir_1_x, dir_1_y, half_thickness);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_VERTEX(3, p1.x - dir_0_x, p1.y - dir_0_y, uv, context.fringe_color);
                    IM_POLYLINE_VERTEX(6, p1.x - dir_1_x, p1.y - dir_1_y, uv, context.fringe_color);

                    IM_POLYLINE_TRIANGLE_BEGIN(3);
                    IM_POLYLINE_TRIANGLE(0, 3, 7, 6);
                    IM_POLYLINE_TRIANGLE_END(3);
                }
                else
                {
                    IM_POLYLINE_VERTEX(4, p1.x + dir_0_x, p1.y + dir_0_y, uv, context.fringe_color);
                    IM_POLYLINE_VERTEX(8, p1.x + dir_1_x, p1.y + dir_1_y, uv, context.fringe_color);

                    IM_POLYLINE_TRIANGLE_BEGIN(3);
                    IM_POLYLINE_TRIANGLE(1, 7, 4, 8);
                    IM_POLYLINE_TRIANGLE_END(3);
                }
            }
            else IM_UNLIKELY if (preferred_join == Round)
            {
                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
                else
                {
                    IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
            }

            vtx_write += 6;
            idx_start += 6;
        }
    }

    if (context.closed)
    {
        draw_list->_VtxWritePtr[0].pos = vtx_write[0].pos;
        draw_list->_VtxWritePtr[1].pos = vtx_write[1].pos;
        draw_list->_VtxWritePtr[2].pos = vtx_write[2].pos;
    }
    else
    {
        if (context.cap == ImDrawFlags_CapButt)
        {
            // Form a square cap by moving Butt cap corner vertices
            // along the direction of the segment they belong to.
            //
            // Gap is filled with extra triangle.
            //
            //   0 +-----------+ 2
            //     |'.       .'|
            //     |  '. 1 .'  |
            //     |    '+'    |
            //     | ~ ~ ~ ~ ~ |
            //

            const float half_thickness_sqr = half_thickness * half_thickness;

            const float s_begin = context.segments_length_sqr[1];
            const float t_begin = (s_begin < half_thickness_sqr ? ImSqrt(s_begin) : half_thickness) * 0.5f;
            ImVec2 n_begin = context.normals[0];
            n_begin.x *= t_begin;
            n_begin.y *= t_begin;

            const float s_end = context.segments_length_sqr[context.point_count];
            const float t_end = (s_end < half_thickness_sqr ? ImSqrt(s_end) : half_thickness) * 0.5f;
            ImVec2      n_end = context.normals[context.point_count - 1];
            n_end.x *= t_end;
            n_end.y *= t_end;

            ImDrawVert* vtx_start = draw_list->_VtxWritePtr;

            vtx_start[0].pos.x -=  n_begin.y;
            vtx_start[0].pos.y +=  n_begin.x;
            vtx_start[1].pos.x -= -n_begin.y;
            vtx_start[1].pos.y += -n_begin.x;
            vtx_start[2].pos.x -=  n_begin.y;
            vtx_start[2].pos.y +=  n_begin.x;

            vtx_write[0].pos.x +=  n_end.y;
            vtx_write[0].pos.y -=  n_end.x;
            vtx_write[1].pos.x += -n_end.y;
            vtx_write[1].pos.y -= -n_end.x;
            vtx_write[2].pos.x +=  n_end.y;
            vtx_write[2].pos.y -=  n_end.x;

            IM_POLYLINE_TRIANGLE_BEGIN(6);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 2);
            IM_POLYLINE_TRIANGLE_EX(1, draw_list->_VtxCurrentIdx, 0, 2, 1);
            IM_POLYLINE_TRIANGLE_END(6);
        }
        else if (context.cap == ImDrawFlags_CapSquare)
        {
            // Form a square cap by moving Butt cap corner vertices
            // along the direction of the segment they belong to.
            //
            // Gap is filled with extra triangle.
            //
            //   0 +-----------+ 2
            //     |'.       .'|
            //     |  '. 1 .'  |
            //     |    '+'    |
            //     | ~ ~ ~ ~ ~ |
            //                 

            ImVec2 n_begin = context.normals[0];
            n_begin.x *= half_thickness;
            n_begin.y *= half_thickness;

            ImVec2 n_end   = context.normals[context.point_count - 1];
            n_end.x *= half_thickness;
            n_end.y *= half_thickness;

            ImDrawVert* vtx_start = draw_list->_VtxWritePtr;

            vtx_start[0].pos.x -= n_begin.y;
            vtx_start[0].pos.y += n_begin.x;
            vtx_start[2].pos.x -= n_begin.y;
            vtx_start[2].pos.y += n_begin.x;

            vtx_write[0].pos.x += n_end.y;
            vtx_write[0].pos.y -= n_end.x;
            vtx_write[2].pos.x += n_end.y;
            vtx_write[2].pos.y -= n_end.x;

            IM_POLYLINE_TRIANGLE_BEGIN(6);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 2);
            IM_POLYLINE_TRIANGLE_EX(1, draw_list->_VtxCurrentIdx, 0, 2, 1);
            IM_POLYLINE_TRIANGLE_END(6);
        }
        else IM_UNLIKELY if (context.cap == ImDrawFlags_CapRound)
        {
            // Form a round cap by adding a circle at the end of the line
            // 
            //        ..---..
            //      .'       '.
            //     :  r        :
            //   0 +-----+-----+ 2
            //     |     1     |
            //     | ~ ~ ~ ~ ~ |
            //

            ImVec2 n0 = context.normals[0];
            ImVec2 n1 = context.normals[context.point_count - 1];

            float angle0 = ImAtan2(n0.y, n0.x);
            float angle1 = ImAtan2(n1.y, n1.x);

            IM_POLYLINE_ARC(context.points[0],                       half_thickness, angle0,  IM_PI);
            IM_POLYLINE_ARC(context.points[context.point_count - 1], half_thickness, angle1, -IM_PI);
        }
    }

    vtx_write += 3;
    idx_start += 3;

    const int used_vtx_count = (int)(vtx_write - draw_list->_VtxWritePtr);
    const int used_idx_count = (int)(idx_write - draw_list->_IdxWritePtr);

    draw_list->_VtxWritePtr   = vtx_write;
    draw_list->_IdxWritePtr   = idx_write;
    draw_list->_VtxCurrentIdx = idx_start;

    draw_list->PrimUnreserve(idx_count - used_idx_count, vtx_count - used_vtx_count);

    IM_UNLIKELY if (arc_count > 0)
        ImDrawList_Polyline_V3_EmitArcs(draw_list, context, arc_count, context.fringe_color);
}

static inline void ImDrawList_Polyline_V3_Thick_AntiAliased(ImDrawList* draw_list, const ImDrawList_Polyline_V3_Context& context)
{
    enum JoinType { Miter, Butt, Bevel, MiterClip, Round, ThickButt };

    const JoinType default_join       = (context.join == ImDrawFlags_JoinBevel) ? Bevel : (context.join == ImDrawFlags_JoinRound     ?     Round : (context.join == ImDrawFlags_JoinMiterClip ? MiterClip : Miter));
    const JoinType default_join_limit = (context.join == ImDrawFlags_JoinRound) ? Round : (context.join == ImDrawFlags_JoinMiterClip ? MiterClip : Bevel);

    const float half_thickness           = context.thickness * 0.5f;
    const float half_thickness_sqr       = half_thickness * half_thickness;
    const float miter_distance_limit     = half_thickness * context.miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    const float half_fringe_thickness           = context.fringe_thickness * 0.5f;
    const float fringe_miter_distance_limit     = half_fringe_thickness * context.miter_limit;
    const float fringe_miter_distance_limit_sqr = fringe_miter_distance_limit * fringe_miter_distance_limit;

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const ImVec2 uv         = draw_list->_Data->TexUvWhitePixel;
    const int    vtx_count  = (context.point_count * 17 + 4);         // top 17 vertices per join, 3 vertices for butt cap
    const int    idx_count  = (context.point_count * 15 + 4) * 3 + 1; // top 15 triangles per join, 4 for square cap, 1 index to avoid write in non-reserved memory

    draw_list->PrimReserve(idx_count, vtx_count);

    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int idx_start = draw_list->_VtxCurrentIdx;

    int arc_count = 0;

    ImVec2 p0 = context.points [context.closed ? context.point_count - 1 : 0];
    ImVec2 n0 = context.normals[context.closed ? context.point_count - 1 : 0];

    ImVec2 p1, n1;

    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_fringe_thickness, p0.y - n0.y * half_fringe_thickness, uv, context.fringe_color);
    IM_POLYLINE_VERTEX(1, p0.x - n0.x *        half_thickness, p0.y - n0.y *        half_thickness, uv, context.color);
    IM_POLYLINE_VERTEX(2, p0.x + n0.x *        half_thickness, p0.y + n0.y *        half_thickness, uv, context.color);
    IM_POLYLINE_VERTEX(3, p0.x + n0.x * half_fringe_thickness, p0.y + n0.y * half_fringe_thickness, uv, context.fringe_color);

    for (int i = context.closed ? 0 : 1; i < context.point_count; ++i, p0 = p1, n0 = n1)
    {
        p1 = context.points[i];
        n1 = context.normals[i];

        // theta is the angle between two segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const float  miter_scale_factor = (cos_theta > IM_POLYLINE_MITER_ANGLE_LIMIT) ? 1.0f / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const float  miter_offset_x     = (n0.x + n1.x) * half_thickness * miter_scale_factor;
        const float  miter_offset_y     = (n0.y + n1.y) * half_thickness * miter_scale_factor;
        const float  miter_distance_sqr = miter_offset_x * miter_offset_x + miter_offset_y * miter_offset_y;

        const float  fringe_miter_offset_x = (n0.x + n1.x) * half_fringe_thickness * miter_scale_factor;
        const float  fringe_miter_offset_y = (n0.y + n1.y) * half_fringe_thickness * miter_scale_factor;
        const float  fringe_miter_distance_sqr = fringe_miter_offset_x * fringe_miter_offset_x + fringe_miter_offset_y * fringe_miter_offset_y;

        const bool   overlap    = (context.segments_length_sqr[i] < fringe_miter_distance_sqr) || (context.segments_length_sqr[i + 1] < fringe_miter_distance_sqr) || (cos_theta <= IM_POLYLINE_MITER_ANGLE_LIMIT);
        const bool   continuous = context.closed || i != context.point_count - 1;

        JoinType preferred_join = Butt;
        if (continuous)
        {
            preferred_join = (miter_distance_sqr > miter_distance_limit_sqr ? default_join_limit : default_join);

            if (preferred_join == MiterClip)
            {
                const float miter_clip_min_distance_sqr = 0.5f * half_thickness_sqr * (cos_theta + 1);

                if (miter_distance_limit_sqr < miter_clip_min_distance_sqr)
                    preferred_join = Bevel;
                else if (miter_distance_sqr > 0 && (miter_distance_sqr < miter_distance_limit_sqr))
                    preferred_join = Miter;
                else if (fringe_miter_distance_sqr > 0 && (fringe_miter_distance_sqr < fringe_miter_distance_limit_sqr))
                    preferred_join = Miter;
            }
        }
        const JoinType join = overlap ? (continuous ? ThickButt : Butt) : preferred_join;

        //
        // Miter and Butt joins have same geometry, only difference is in location of the vertices
        // 
        //   4  5    6  7
        //   +--+----+--+
        //   | '|  .'| '|
        //   |' |.'  |' |
        //   + ~+ ~ ~+~ +
        //   0  1    2  3
        //
        IM_LIKELY if (join == Miter)
        {
            IM_POLYLINE_VERTEX(4, p1.x - fringe_miter_offset_x, p1.y - fringe_miter_offset_y, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(5, p1.x -        miter_offset_x, p1.y -        miter_offset_y, uv, context.color);
            IM_POLYLINE_VERTEX(6, p1.x +        miter_offset_x, p1.y +        miter_offset_y, uv, context.color);
            IM_POLYLINE_VERTEX(7, p1.x + fringe_miter_offset_x, p1.y + fringe_miter_offset_y, uv, context.fringe_color);
            vtx_write += 4;

            IM_POLYLINE_TRIANGLE_BEGIN(18);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
            IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 6);
            IM_POLYLINE_TRIANGLE(3, 1, 6, 5);
            IM_POLYLINE_TRIANGLE(4, 2, 3, 7);
            IM_POLYLINE_TRIANGLE(4, 2, 7, 6);
            IM_POLYLINE_TRIANGLE_END(18);
            idx_start += 4;
        }
        else if (join == Butt)
        {
            IM_POLYLINE_VERTEX(4, p1.x - n1.x * half_fringe_thickness, p1.y - n1.y * half_fringe_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(5, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(6, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(7, p1.x + n1.x * half_fringe_thickness, p1.y + n1.y * half_fringe_thickness, uv, context.fringe_color);
            vtx_write += 4;

            IM_POLYLINE_TRIANGLE_BEGIN(18);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
            IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 6);
            IM_POLYLINE_TRIANGLE(3, 1, 6, 5);
            IM_POLYLINE_TRIANGLE(4, 2, 3, 7);
            IM_POLYLINE_TRIANGLE(4, 2, 7, 6);
            IM_POLYLINE_TRIANGLE_END(18);
            idx_start += 4;
        }
        else if (join == Bevel || join == MiterClip)
        {
            //
            // Bevel geometry depends on the sign of the bend direction.
            //
            //  Left bevel:                Right bevel:
            //
            //                           |
            //                .+ 6       |        9 +.
            //              .' |         |          |:'.
            //            .'.':+ 7       |        8 +.'.'.
            //          .'.'.' |         |          | '.'.'.
            //        .'.'.'   |         |          |   '.'.'.
            //      .:''.'     | 8       |        7 |     '.: '.
            //     +---+------:+.        |         .+------:+---+
            //    4|  '|5   .' | '.      |       .'.|    .' |4 '|5
            //     | ' |  .'   |  .+ 9   |    6 + . |  .'   | ' |
            //     |'  |.'     |.' |     |      |.  |.'     |'  |
            //     + ~ + ~ ~ ~ + ~ +     |      + ~ + ~ ~ ~ + ~ +
            //     0   1       2   3     |      0   1       2   3
            //
            // 6 vertices, 9 triangles

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            float bevel_normal_x, bevel_normal_y;
            IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

            float dir_0_x, dir_0_y, dir_1_x, dir_1_y;
            IM_POLYLINE_BEVEL_VECTORS(bevel_normal_x, bevel_normal_y, dir_0_x, dir_0_y, dir_1_x, dir_1_y, context.fringe_width);

            float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
            if (join == Bevel)
            {
                IM_POLYLINE_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness);
            }
            else
            {
                IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness, miter_distance_limit);
            }

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(4, pt_x -        dir_0_x - d0_x, pt_y -        dir_0_y - d0_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(5, pt_x -                  d0_x, pt_y -                  d0_y, uv, context.color);
                IM_POLYLINE_VERTEX(6, pt_x -        dir_1_x - d1_x, pt_y -        dir_1_y - d1_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(7, pt_x -                  d1_x, pt_y -                  d1_y, uv, context.color);
                IM_POLYLINE_VERTEX(8, p1.x +        miter_offset_x, p1.y +        miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX(9, p1.x + fringe_miter_offset_x, p1.y + fringe_miter_offset_y, uv, context.fringe_color);
                vtx_write += 6;

                IM_POLYLINE_TRIANGLE_BEGIN(27);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 8);
                IM_POLYLINE_TRIANGLE(3, 1, 8, 5);
                IM_POLYLINE_TRIANGLE(4, 2, 3, 9);
                IM_POLYLINE_TRIANGLE(5, 2, 9, 8);
                IM_POLYLINE_TRIANGLE(6, 5, 8, 7);
                IM_POLYLINE_TRIANGLE(7, 4, 5, 7);
                IM_POLYLINE_TRIANGLE(8, 4, 7, 6);
                IM_POLYLINE_TRIANGLE_END(27);
                idx_start += 6;
            }
            else
            {
                IM_POLYLINE_VERTEX(4, pt_x +                  d0_x, pt_y +                  d0_y, uv, context.color);
                IM_POLYLINE_VERTEX(5, pt_x +        dir_0_x + d0_x, pt_y +        dir_0_y + d0_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(6, p1.x - fringe_miter_offset_x, p1.y - fringe_miter_offset_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX(7, p1.x -        miter_offset_x, p1.y -        miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX(8, pt_x +                  d1_x, pt_y +                  d1_y, uv, context.color);
                IM_POLYLINE_VERTEX(9, pt_x +        dir_1_x + d1_x, pt_y +        dir_1_y + d1_y, uv, context.fringe_color);
                vtx_write += 6;

                IM_POLYLINE_TRIANGLE_BEGIN(27);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 7);
                IM_POLYLINE_TRIANGLE(1, 0, 7, 6);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 4);
                IM_POLYLINE_TRIANGLE(3, 1, 4, 7);
                IM_POLYLINE_TRIANGLE(4, 2, 3, 5);
                IM_POLYLINE_TRIANGLE(5, 2, 5, 4);
                IM_POLYLINE_TRIANGLE(6, 7, 4, 8);
                IM_POLYLINE_TRIANGLE(7, 4, 5, 9);
                IM_POLYLINE_TRIANGLE(8, 4, 9, 8);
                IM_POLYLINE_TRIANGLE_END(27);
                idx_start += 6;
            }
        }
        else IM_UNLIKELY if (join == Round)
        {
            //
            // Round geometry depends on the sign of the bend direction.
            //
            //  Left arc:                  Right arc:
            //
            //                           |
            //         ..--+ 7           |           10 +--..
            //       .'    |             |              |    '.
            //      :      + 8           |            9 +      :
            //     |    5  |'            |             '|  5    |
            //     +---+---+6'           |            '4+---+---+
            //    4|  '|''..'.'  9       |        8  '.'..''|'  |6
            //     | ' |    '':+.        |         .+:''    | ' |
            //     | ' |    .' | '.      |       .' | '.    | ' |
            //     | ' |  .'   |  .+ 10  |    7 +.  |   '.  | ' |
            //     |'  |.'     |.' |     |      | '.|     '.|  '|
            //     + ~ + ~ ~ ~ + ~ +     |      + ~ + ~ ~ ~ + ~ +
            //     0   1       2   3     |      0   1       2   3
            //
            // 7 vertices, 8 triangles

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_ARC(p1, half_fringe_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                IM_POLYLINE_VERTEX( 4, p1.x - n0.x * half_fringe_thickness, p1.y - n0.y * half_fringe_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX( 5, p1.x - n0.x *        half_thickness, p1.y - n0.y *        half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX( 6, p1.x,                                p1.y,                                uv, context.color);
                IM_POLYLINE_VERTEX( 7, p1.x - n1.x * half_fringe_thickness, p1.y - n1.y * half_fringe_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX( 8, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX( 9, p1.x +               miter_offset_x, p1.y +               miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX(10, p1.x +        fringe_miter_offset_x, p1.y +        fringe_miter_offset_y, uv, context.fringe_color);
                vtx_write += 7;

                IM_POLYLINE_TRIANGLE_BEGIN(24);
                IM_POLYLINE_TRIANGLE(0, 0,  1,  5);
                IM_POLYLINE_TRIANGLE(1, 0,  5,  4);
                IM_POLYLINE_TRIANGLE(2, 1,  2,  9);
                IM_POLYLINE_TRIANGLE(3, 1,  9,  5);
                IM_POLYLINE_TRIANGLE(4, 5,  9,  6);
                IM_POLYLINE_TRIANGLE(5, 2,  3, 10);
                IM_POLYLINE_TRIANGLE(6, 2, 10,  9);
                IM_POLYLINE_TRIANGLE(7, 6,  9,  8);
                IM_POLYLINE_TRIANGLE_END(24);
                idx_start += 7;
            }
            else
            {
                IM_POLYLINE_ARC(p1, half_fringe_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                IM_POLYLINE_VERTEX( 4, p1.x,                                p1.y,                                uv, context.color);
                IM_POLYLINE_VERTEX( 5, p1.x + n0.x *        half_thickness, p1.y + n0.y *        half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX( 6, p1.x + n0.x * half_fringe_thickness, p1.y + n0.y * half_fringe_thickness, uv, context.fringe_color);
                IM_POLYLINE_VERTEX( 7, p1.x -        fringe_miter_offset_x, p1.y -        fringe_miter_offset_y, uv, context.fringe_color);
                IM_POLYLINE_VERTEX( 8, p1.x -               miter_offset_x, p1.y -               miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX( 9, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX(10, p1.x + n1.x * half_fringe_thickness, p1.y + n1.y * half_fringe_thickness, uv, context.fringe_color);
                vtx_write += 7;

                IM_POLYLINE_TRIANGLE_BEGIN(24);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 7);
                IM_POLYLINE_TRIANGLE(1, 1, 8, 7);
                IM_POLYLINE_TRIANGLE(2, 1, 2, 8);
                IM_POLYLINE_TRIANGLE(3, 8, 2, 5);
                IM_POLYLINE_TRIANGLE(4, 8, 5, 4);
                IM_POLYLINE_TRIANGLE(5, 8, 4, 9);
                IM_POLYLINE_TRIANGLE(6, 2, 3, 5);
                IM_POLYLINE_TRIANGLE(7, 3, 6, 5);
                IM_POLYLINE_TRIANGLE_END(24);
                idx_start += 7;
            }
        }
        else if (join == ThickButt)
        {
            // Thick butt end one segment with Butt cap and begin next segment with Butt cap
            //
            // Two segments do overlap causing overdraw.
            //
            //          .+
            //        .'  ,+
            //    13+'  .'  ~
            //       '+'     .+
            //       14'.  .'   ~
            //   +--+----X----+--+.+
            //   |4 |5     '. |6.|7 .+
            //   |  |        '+' |.'
            //   |  |       15|'+|16
            //   +  + ~     ~ +  +
            //   0  1         2  3
            // 
            // 17 vertices, 15 triangles total, 6 triangles (base)
            // 
            // Vertices 8, 9, 10, 11, 12 are used to fill the gap between segments.
            // Gap between segments is filled according to preferred join type.
            //

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            IM_POLYLINE_VERTEX( 4, p1.x - n0.x * half_fringe_thickness, p1.y - n0.y * half_fringe_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX( 5, p1.x - n0.x *        half_thickness, p1.y - n0.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX( 6, p1.x + n0.x *        half_thickness, p1.y + n0.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX( 7, p1.x + n0.x * half_fringe_thickness, p1.y + n0.y * half_fringe_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX( 8, p1.x,                                p1.y,                                uv, context.color);
            IM_POLYLINE_VERTEX( 9, p1.x,                                p1.y,                                uv, context.color);
            IM_POLYLINE_VERTEX(10, p1.x,                                p1.y,                                uv, context.color);
            IM_POLYLINE_VERTEX(11, p1.x,                                p1.y,                                uv, context.color);
            IM_POLYLINE_VERTEX(12, p1.x,                                p1.y,                                uv, context.color);
            IM_POLYLINE_VERTEX(13, p1.x - n1.x * half_fringe_thickness, p1.y - n1.y * half_fringe_thickness, uv, context.fringe_color);
            IM_POLYLINE_VERTEX(14, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(15, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(16, p1.x + n1.x * half_fringe_thickness, p1.y + n1.y * half_fringe_thickness, uv, context.fringe_color);

            IM_POLYLINE_TRIANGLE_BEGIN(18);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
            IM_POLYLINE_TRIANGLE(1, 0, 5, 4);
            IM_POLYLINE_TRIANGLE(2, 1, 2, 6);
            IM_POLYLINE_TRIANGLE(3, 1, 6, 5);
            IM_POLYLINE_TRIANGLE(4, 2, 3, 7);
            IM_POLYLINE_TRIANGLE(4, 2, 7, 6);
            IM_POLYLINE_TRIANGLE_END(18);

            if (preferred_join == Miter)
            {
                // Fill gap between segments with Miter join
                // 
                // Left Miter join:        Right Miter join:
                // 
                //                      13         |         16
                //    +----------------+           |           +----------------+
                //   8 \''..  9....'''' \          |          /......''''''::''/ 8
                //      \   ''+----------+ 14      |      15 +----------+'' ,'/
                //       \    :\       .' \        |        /      ,,''/9 ,' /
                //        \  :  \    .'    \       |       /   ,,''   / ,'  /
                //         \:    \ .'       \ p1   |   p1 /..''      /,'   /
                //          +-----+----------x-~   |   ~-x----------+-----+
                //          4     5        10 \    |    / 10        6     7
                //                             ~   |   ~
                // 
                // Used 3 vertices to fill the gap between segments.
                // 
                // 6 join triangles, 12 total
                //

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_VERTEX(8, p1.x - fringe_miter_offset_x, p1.y - fringe_miter_offset_y, uv, context.fringe_color);
                    IM_POLYLINE_VERTEX(9, p1.x -        miter_offset_x, p1.y -        miter_offset_y, uv, context.color);
                    // 10 is already set to p1

                    IM_POLYLINE_TRIANGLE_BEGIN(18);
                    IM_POLYLINE_TRIANGLE(0, 5, 14, 10);
                    IM_POLYLINE_TRIANGLE(1, 5, 14,  9);
                    IM_POLYLINE_TRIANGLE(2, 4,  5,  9);
                    IM_POLYLINE_TRIANGLE(3, 4,  9,  8);
                    IM_POLYLINE_TRIANGLE(4, 9, 13,  8);
                    IM_POLYLINE_TRIANGLE(5, 9, 14, 13);
                    IM_POLYLINE_TRIANGLE_END(18);
                }
                else
                {
                    IM_POLYLINE_VERTEX(8, p1.x + fringe_miter_offset_x, p1.y + fringe_miter_offset_y, uv, context.fringe_color);
                    IM_POLYLINE_VERTEX(9, p1.x +        miter_offset_x, p1.y +        miter_offset_y, uv, context.color);
                    // 10 is already set to p1

                    IM_POLYLINE_TRIANGLE_BEGIN(18);
                    IM_POLYLINE_TRIANGLE(0, 10,  6,  9);
                    IM_POLYLINE_TRIANGLE(1, 10,  9, 15);
                    IM_POLYLINE_TRIANGLE(2,  6,  7,  8);
                    IM_POLYLINE_TRIANGLE(3,  6,  8,  9);
                    IM_POLYLINE_TRIANGLE(4,  9,  8, 15);
                    IM_POLYLINE_TRIANGLE(5, 15,  8, 16);
                    IM_POLYLINE_TRIANGLE_END(18);
                }
            }
            else if (preferred_join == Bevel || preferred_join == MiterClip)
            {
                IM_UNLIKELY if (cos_theta <= IM_POLYLINE_MITER_ANGLE_LIMIT)
                {
                    // Here segments form almost 180 degrees angle, join form thin or thick square cap

                    if (preferred_join == Bevel)
                    {
                        // Thin square cap
                        // 
                        //  13  14   15  16
                        //   +---+----+---+
                        //   |  ,' ..''.  |
                        //   |.:.''     '.|
                        //   +------------+
                        //   8            9
                        //
                        // 4 join triangles, 10 total
                        //

                        IM_POLYLINE_VERTEX(8, p1.x - n1.x * half_fringe_thickness - n1.y * context.fringe_width, p1.y - n1.y * half_fringe_thickness + n1.x * context.fringe_width, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX(9, p1.x + n1.x * half_fringe_thickness - n1.y * context.fringe_width, p1.y + n1.y * half_fringe_thickness + n1.x * context.fringe_width, uv, context.fringe_color);

                        IM_POLYLINE_TRIANGLE_BEGIN(12);
                        IM_POLYLINE_TRIANGLE(0, 8, 14, 13);
                        IM_POLYLINE_TRIANGLE(1, 8, 15, 14);
                        IM_POLYLINE_TRIANGLE(2, 8,  9, 15);
                        IM_POLYLINE_TRIANGLE(3, 9, 16, 15);
                        IM_POLYLINE_TRIANGLE_END(12);
                    }
                    else
                    {
                        // Square cap
                        // 
                        //  13  14   15  16
                        //   +---+----+---+
                        //   |'. |..''| .'|
                        //   | 9'+----+'10|
                        //   |  ,' ..''.  |
                        //   |.:.''     '.|
                        //   +------------+
                        //   8           11
                        //
                        // 8 join triangles, 14 total
                        //

                        const float inner = miter_distance_limit;
                        const float outer = inner + context.fringe_width;

                        IM_POLYLINE_VERTEX( 8, p1.x - n1.x * half_fringe_thickness - n1.y * outer, p1.y - n1.y * half_fringe_thickness + n1.x * outer, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX( 9, p1.x - n1.x *        half_thickness - n1.y * inner, p1.y - n1.y *        half_thickness + n1.x * inner, uv, context.color);
                        IM_POLYLINE_VERTEX(10, p1.x + n1.x *        half_thickness - n1.y * inner, p1.y + n1.y *        half_thickness + n1.x * inner, uv, context.color);
                        IM_POLYLINE_VERTEX(11, p1.x + n1.x * half_fringe_thickness - n1.y * outer, p1.y + n1.y * half_fringe_thickness + n1.x * outer, uv, context.fringe_color);

                        IM_POLYLINE_TRIANGLE_BEGIN(24);
                        IM_POLYLINE_TRIANGLE(0,  9, 14, 13);
                        IM_POLYLINE_TRIANGLE(1,  8,  9, 13);
                        IM_POLYLINE_TRIANGLE(2,  8, 10,  9);
                        IM_POLYLINE_TRIANGLE(3,  8, 11, 10);
                        IM_POLYLINE_TRIANGLE(4, 10, 11, 16);
                        IM_POLYLINE_TRIANGLE(5, 10, 16, 15);
                        IM_POLYLINE_TRIANGLE(6,  9, 10, 15);
                        IM_POLYLINE_TRIANGLE(7,  9, 15, 14);
                        IM_POLYLINE_TRIANGLE_END(24);
                    }
                }
                else if (preferred_join == Bevel)
                {
                    // Fill gap between segments with Bevel join
                    //
                    // Left Bevel join:             Right Bevel join:
                    //
                    //                13          |          16
                    //            9  +            |            +  9 
                    //             +: \           |           / :+
                    //           .'' '.+ 14       |       15 +.' ''.
                    //         .' '  .' \         |         / '.  ' '.
                    //    8  .'  ' .'    \        |        /    '. '  '.  8
                    //     +:.. '.'       \ p1    |    p1 /       '.' ..:+
                    //    +----:+----------x-~    |    ~-x----------+:----+
                    //    4     5        10 \     |     / 10        6     7
                    //                       ~    |    ~
                    //
                    // Used 3 vertices to fill the gap between segments.
                    //
                    // 5 join triangles, 11 total
                    //

                    float bevel_normal_x, bevel_normal_y;
                    IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

                    float dir_0_x, dir_0_y, dir_1_x, dir_1_y;
                    IM_POLYLINE_BEVEL_VECTORS(bevel_normal_x, bevel_normal_y, dir_0_x, dir_0_y, dir_1_x, dir_1_y, context.fringe_width);

                    float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
                    IM_POLYLINE_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness);

                    if (sin_theta < 0.0f)
                    {
                        IM_POLYLINE_VERTEX(8, pt_x - dir_0_x - d0_x, pt_y - dir_0_y - d0_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX(9, pt_x - dir_1_x - d1_x, pt_y - dir_1_y - d1_y, uv, context.fringe_color);
                        // 10 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(15);
                        IM_POLYLINE_TRIANGLE(0, 5, 10, 14);
                        IM_POLYLINE_TRIANGLE(1, 5,  8,  4);
                        IM_POLYLINE_TRIANGLE(2, 9, 14, 13);
                        IM_POLYLINE_TRIANGLE(3, 5, 14,  9);
                        IM_POLYLINE_TRIANGLE(4, 5,  9,  8);
                        IM_POLYLINE_TRIANGLE_END(15);
                    }
                    else
                    {
                        IM_POLYLINE_VERTEX(8, pt_x + dir_0_x + d0_x, pt_y + dir_0_y + d0_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX(9, pt_x + dir_1_x + d1_x, pt_y + dir_1_y + d1_y, uv, context.fringe_color);
                        // 10 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(15);
                        IM_POLYLINE_TRIANGLE(0, 6, 15, 10);
                        IM_POLYLINE_TRIANGLE(1, 6,  7,  8);
                        IM_POLYLINE_TRIANGLE(2, 9, 16, 15);
                        IM_POLYLINE_TRIANGLE(3, 6,  8,  9);
                        IM_POLYLINE_TRIANGLE(4, 6,  9, 15);
                        IM_POLYLINE_TRIANGLE_END(15);
                    }
                }
                else
                {
                    // Fill gap between segments with clipped Bevel join
                    //
                    // Left Bevel join:                     Right Bevel join:
                    //
                    //                      13            |             16
                    //               10  ..+              |               +..  10
                    //                 +'   \             |              /   '+
                    //               .' .'.. \ 14         |          15 / ..'. '.
                    //             .'    .  :.+           |            +.:  .    '.
                    //           .'    .. +'11 \          |           / 11'+ ..    '.
                    //         .'  ..'' .' '.   \         |          /   .' '. ''..  '.
                    //       .'..''  9.'     '.  \        |         /  .'     '.9  ''..'.
                    //     8+.....'''+....     '. \       |        / .'     ....+'''.....+8
                    //     :   ''.. :     '''''..:.\ p1   |    p1 /.:..'''''     : ..''   :
                    //    +--------+----------------x-~   |    ~-x----------------+--------+
                    //    4        5              12 \    |     / 12              6        7
                    //                                ~   |    ~
                    //
                    // Used 5 vertices to fill the gap between segments.
                    //
                    // 9 join triangles, 15 total
                    //

                    float bevel_normal_x, bevel_normal_y;
                    IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

                    float dir_0_x, dir_0_y, dir_1_x, dir_1_y;
                    IM_POLYLINE_BEVEL_VECTORS(bevel_normal_x, bevel_normal_y, dir_0_x, dir_0_y, dir_1_x, dir_1_y, context.fringe_width);

                    float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
                    IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness, miter_distance_limit);

                    if (sin_theta < 0.0f)
                    {
                        IM_POLYLINE_VERTEX( 8, pt_x - dir_0_x - d0_x, pt_y - dir_0_y - d0_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX( 9, pt_x -           d0_x, pt_y -           d0_y, uv, context.color);
                        IM_POLYLINE_VERTEX(10, pt_x - dir_1_x - d1_x, pt_y - dir_1_y - d1_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX(11, pt_x -           d1_x, pt_y -           d1_y, uv, context.color);
                        // 12 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(27);
                        IM_POLYLINE_TRIANGLE(0, 12, 14, 11);
                        IM_POLYLINE_TRIANGLE(1, 12, 11,  9);
                        IM_POLYLINE_TRIANGLE(2, 12,  9,  5);
                        IM_POLYLINE_TRIANGLE(3,  5,  9,  8);
                        IM_POLYLINE_TRIANGLE(4,  5,  8,  4);
                        IM_POLYLINE_TRIANGLE(5, 14, 13, 10);
                        IM_POLYLINE_TRIANGLE(6, 14, 10, 11);
                        IM_POLYLINE_TRIANGLE(7,  8,  9, 11);
                        IM_POLYLINE_TRIANGLE(8,  8, 11, 10);
                        IM_POLYLINE_TRIANGLE_END(27);
                    }
                    else
                    {
                        IM_POLYLINE_VERTEX( 8, pt_x + dir_0_x + d0_x, pt_y + dir_0_y + d0_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX( 9, pt_x +           d0_x, pt_y +           d0_y, uv, context.color);
                        IM_POLYLINE_VERTEX(10, pt_x + dir_1_x + d1_x, pt_y + dir_1_y + d1_y, uv, context.fringe_color);
                        IM_POLYLINE_VERTEX(11, pt_x +           d1_x, pt_y +           d1_y, uv, context.color);
                        // 12 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(27);
                        IM_POLYLINE_TRIANGLE(0, 12,  6,  9);
                        IM_POLYLINE_TRIANGLE(1, 12,  9, 11);
                        IM_POLYLINE_TRIANGLE(2, 12, 11, 15);
                        IM_POLYLINE_TRIANGLE(3,  6,  7,  8);
                        IM_POLYLINE_TRIANGLE(4,  6,  8,  9);
                        IM_POLYLINE_TRIANGLE(5, 15, 11, 10);
                        IM_POLYLINE_TRIANGLE(6, 15, 10, 16);
                        IM_POLYLINE_TRIANGLE(7, 11,  9,  8);
                        IM_POLYLINE_TRIANGLE(8, 11,  8, 10);
                        IM_POLYLINE_TRIANGLE_END(27);
                    }
                }
            }
            else IM_UNLIKELY if (preferred_join == Round)
            {
                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_ARC(p1, half_fringe_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
                else
                {
                    IM_POLYLINE_ARC(p1, half_fringe_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
            }

            vtx_write += 13;
            idx_start += 13;
        }
    }

    if (context.closed)
    {
        draw_list->_VtxWritePtr[0].pos = vtx_write[0].pos;
        draw_list->_VtxWritePtr[1].pos = vtx_write[1].pos;
        draw_list->_VtxWritePtr[2].pos = vtx_write[2].pos;
        draw_list->_VtxWritePtr[3].pos = vtx_write[3].pos;
    }
    else
    {
        if (context.cap == ImDrawFlags_CapButt)
        {
            // Form a square cap by moving Butt cap corner vertices
            // along the direction of the segment they belong to.
            //
            // Gap is filled with two extra triangles.
            //
            //   0 +------------------+ 3
            //     |'.          ...':'|
            //     |  '. 1...'''2 .'  |
            //     |    '+------+'    |
            //     | ~ ~ ~ ~ ~ ~ ~ ~ ~|
            //

            const float fringe_width_sqr = context.fringe_width * context.fringe_width;

            const float s_begin = context.segments_length_sqr[1];
            const float t_begin = (s_begin < fringe_width_sqr ? ImSqrt(s_begin) : context.fringe_width) * 0.5f;
            ImVec2 n_begin = context.normals[0];
            n_begin.x *= t_begin;
            n_begin.y *= t_begin;

            const float s_end = context.segments_length_sqr[context.point_count];
            const float t_end = (s_end < fringe_width_sqr ? ImSqrt(s_end) : context.fringe_width) * 0.5f;
            ImVec2      n_end = context.normals[context.point_count - 1];
            n_end.x *= t_end;
            n_end.y *= t_end;

            ImDrawVert* vtx_start = draw_list->_VtxWritePtr;

            vtx_start[0].pos.x -= n_begin.y;
            vtx_start[0].pos.y += n_begin.x;
            vtx_start[1].pos.x += n_begin.y;
            vtx_start[1].pos.y -= n_begin.x;
            vtx_start[2].pos.x += n_begin.y;
            vtx_start[2].pos.y -= n_begin.x;
            vtx_start[3].pos.x -= n_begin.y;
            vtx_start[3].pos.y += n_begin.x;

            vtx_write[0].pos.x += n_end.y;
            vtx_write[0].pos.y -= n_end.x;
            vtx_write[1].pos.x -= n_end.y;
            vtx_write[1].pos.y += n_end.x;
            vtx_write[2].pos.x -= n_end.y;
            vtx_write[2].pos.y += n_end.x;
            vtx_write[3].pos.x += n_end.y;
            vtx_write[3].pos.y -= n_end.x;

            IM_POLYLINE_TRIANGLE_BEGIN(12);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 1, 2, 3);
            IM_POLYLINE_TRIANGLE_EX(2, draw_list->_VtxCurrentIdx, 0, 3, 1);
            IM_POLYLINE_TRIANGLE_EX(3, draw_list->_VtxCurrentIdx, 1, 3, 2);
            IM_POLYLINE_TRIANGLE_END(12);
        }
        else IM_UNLIKELY if (context.cap == ImDrawFlags_CapSquare)
        {
            // Form a square cap by moving Butt cap corner vertices
            // along the direction of the segment they belong to.
            //
            // Gap is filled with two extra triangles.
            //
            //   0 +------------------+ 3
            //     |'.          ...':'|
            //     |  '. 1...'''2 .'  |
            //     |    '+------+'    |
            //     | ~ ~ ~ ~ ~ ~ ~ ~ ~|
            //

            ImVec2 n_begin = context.normals[0];
            ImVec2 n_end   = context.normals[context.point_count - 1];

            ImDrawVert* vtx_start = draw_list->_VtxWritePtr;

            vtx_start[0].pos.x -= n_begin.y * half_fringe_thickness;
            vtx_start[0].pos.y += n_begin.x * half_fringe_thickness;
            vtx_start[1].pos.x -= n_begin.y *        half_thickness;
            vtx_start[1].pos.y += n_begin.x *        half_thickness;
            vtx_start[2].pos.x -= n_begin.y *        half_thickness;
            vtx_start[2].pos.y += n_begin.x *        half_thickness;
            vtx_start[3].pos.x -= n_begin.y * half_fringe_thickness;
            vtx_start[3].pos.y += n_begin.x * half_fringe_thickness;

            vtx_write[0].pos.x += n_end.y * half_fringe_thickness;
            vtx_write[0].pos.y -= n_end.x * half_fringe_thickness;
            vtx_write[1].pos.x += n_end.y *        half_thickness;
            vtx_write[1].pos.y -= n_end.x *        half_thickness;
            vtx_write[2].pos.x += n_end.y *        half_thickness;
            vtx_write[2].pos.y -= n_end.x *        half_thickness;
            vtx_write[3].pos.x += n_end.y * half_fringe_thickness;
            vtx_write[3].pos.y -= n_end.x * half_fringe_thickness;

            IM_POLYLINE_TRIANGLE_BEGIN(12);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 1, 2, 3);
            IM_POLYLINE_TRIANGLE_EX(2, draw_list->_VtxCurrentIdx, 0, 3, 1);
            IM_POLYLINE_TRIANGLE_EX(3, draw_list->_VtxCurrentIdx, 1, 3, 2);
            IM_POLYLINE_TRIANGLE_END(12);
        }
        else IM_UNLIKELY if (context.cap == ImDrawFlags_CapRound)
        {
            // Form a round cap by adding a circle at the end of the line
            // 
            //        ..---..
            //      .'       '.
            //     :  r        :
            //   0 +-----+-----+ 2
            //     |     1     |
            //     | ~ ~ ~ ~ ~ |
            //

            ImVec2 n0 = context.normals[0];
            ImVec2 n1 = context.normals[context.point_count - 1];

            float angle0 = ImAtan2(n0.y, n0.x);
            float angle1 = ImAtan2(n1.y, n1.x);

            IM_POLYLINE_ARC(context.points[0],                       half_fringe_thickness, angle0,  IM_PI);
            IM_POLYLINE_ARC(context.points[context.point_count - 1], half_fringe_thickness, angle1, -IM_PI);
        }
    }

    vtx_write += 4;
    idx_start += 4;

    const int used_vtx_count = (int)(vtx_write - draw_list->_VtxWritePtr);
    const int used_idx_count = (int)(idx_write - draw_list->_IdxWritePtr);

    draw_list->_VtxWritePtr   = vtx_write;
    draw_list->_IdxWritePtr   = idx_write;
    draw_list->_VtxCurrentIdx = idx_start;

    draw_list->PrimUnreserve(idx_count - used_idx_count, vtx_count - used_vtx_count);

    IM_UNLIKELY if (arc_count > 0)
        ImDrawList_Polyline_V3_EmitArcsWithFringe(draw_list, context, arc_count);
}

static inline void ImDrawList_Polyline_V3_NotAntiAliased(ImDrawList* draw_list, const ImDrawList_Polyline_V3_Context& context)
{
    enum JoinType { Miter, Butt, Bevel, MiterClip, Round, ThickButt };

    const JoinType default_join       = (context.join == ImDrawFlags_JoinBevel) ? Bevel : (context.join == ImDrawFlags_JoinRound     ?     Round : (context.join == ImDrawFlags_JoinMiterClip ? MiterClip : Miter));
    const JoinType default_join_limit = (context.join == ImDrawFlags_JoinRound) ? Round : (context.join == ImDrawFlags_JoinMiterClip ? MiterClip : Bevel);

    const float half_thickness           = context.thickness * 0.5f;
    const float half_thickness_sqr       = half_thickness * half_thickness;
    const float miter_distance_limit     = half_thickness * context.miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const ImVec2 uv         = draw_list->_Data->TexUvWhitePixel;
    const int    vtx_count = (context.point_count * 7 + 2);      // top 7 vertices per join, 2 for butt cap
    const int    idx_count  = (context.point_count * 5) * 3 + 1; // top 5 triangles per join, 1 index to avoid write in non-reserved memory

    draw_list->PrimReserve(idx_count, vtx_count);

    ImDrawVert*  vtx_write = draw_list->_VtxWritePtr;
    ImDrawIdx*   idx_write = draw_list->_IdxWritePtr;
    unsigned int idx_start = draw_list->_VtxCurrentIdx;

    int arc_count = 0;

    ImVec2 p0 = context.points [context.closed ? context.point_count - 1 : 0];
    ImVec2 n0 = context.normals[context.closed ? context.point_count - 1 : 0];

    ImVec2 p1, n1;

    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_thickness, p0.y - n0.y * half_thickness, uv, context.color);
    IM_POLYLINE_VERTEX(1, p0.x + n0.x * half_thickness, p0.y + n0.y * half_thickness, uv, context.color);

    for (int i = context.closed ? 0 : 1; i < context.point_count; ++i, p0 = p1, n0 = n1)
    {
        p1 = context.points[i];
        n1 = context.normals[i];

        // theta is the angle between two segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const float  miter_scale_factor = (cos_theta > IM_POLYLINE_MITER_ANGLE_LIMIT) ? 1.0f / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const float  miter_offset_x     = (n0.x + n1.x) * half_thickness * miter_scale_factor;
        const float  miter_offset_y     = (n0.y + n1.y) * half_thickness * miter_scale_factor;
        const float  miter_distance_sqr = miter_offset_x * miter_offset_x + miter_offset_y * miter_offset_y;

        const bool   overlap    = (context.segments_length_sqr[i] < miter_distance_sqr) || (context.segments_length_sqr[i + 1] < miter_distance_sqr) || (cos_theta <= IM_POLYLINE_MITER_ANGLE_LIMIT);
        const bool   continuous = context.closed || i != context.point_count - 1;

        JoinType preferred_join = Butt;
        if (continuous)
        {
            preferred_join = (miter_distance_sqr > miter_distance_limit_sqr ? default_join_limit : default_join);

            if (preferred_join == MiterClip)
            {
                const float miter_clip_min_distance_sqr = 0.5f * half_thickness_sqr * (cos_theta + 1);

                if (miter_distance_limit_sqr < miter_clip_min_distance_sqr)
                    preferred_join = Bevel;
                else if (miter_distance_sqr > 0 && (miter_distance_sqr < miter_distance_limit_sqr))
                    preferred_join = Miter;
            }
        }
        const JoinType join = overlap ? (continuous ? ThickButt : Butt) : preferred_join;

        //
        // Miter and Butt joins have same geometry, only difference is in location of the vertices
        // 
        //   2    3
        //   +----+
        //   |  .'|
        //   |.'  |
        //   + ~ ~+
        //   0    1
        //
        IM_LIKELY if (join == Miter)
        {
            IM_POLYLINE_VERTEX(2, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.color);
            IM_POLYLINE_VERTEX(3, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.color);
            vtx_write += 2;

            IM_POLYLINE_TRIANGLE_BEGIN(6);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 0, 3, 2);
            IM_POLYLINE_TRIANGLE_END(6);
            idx_start += 2;
        }
        else if (join == Butt)
        {
            IM_POLYLINE_VERTEX(2, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(3, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.color);
            vtx_write += 2;

            IM_POLYLINE_TRIANGLE_BEGIN(6);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 0, 3, 2);
            IM_POLYLINE_TRIANGLE_END(6);
            idx_start += 2;
        }
        else if (join == Bevel || join == MiterClip)
        {
            //
            // Bevel geometry depends on the sign of the bend direction.
            //
            //  Left bevel:                Right bevel:
            //
            //                  |
            //           .+ 3   |   4 +.
            //         .' |     |     | '.
            //       .'   |     |     |   '.
            //     .'     |     |     |     '.
            //    +-------+ 4   |   3 +-------+ 2
            //    |2   .' |     |     |    .' |
            //    |  .'   |     |     |  .'   |
            //    |.'     |     |     |.'     |
            //    + ~ ~ ~ +     |     + ~ ~ ~ +
            //    0       1     |     0       1
            //
            // 3 vertices, 3 triangles

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
            if (join == Bevel)
            {
                IM_POLYLINE_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness);
            }
            else
            {
                float bevel_normal_x, bevel_normal_y;
                IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

                IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness, miter_distance_limit);
            }

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(2, pt_x -           d0_x, pt_y -           d0_y, uv, context.color);
                IM_POLYLINE_VERTEX(3, pt_x -           d1_x, pt_y -           d1_y, uv, context.color);
                IM_POLYLINE_VERTEX(4, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.color);
                vtx_write += 3;

                IM_POLYLINE_TRIANGLE_BEGIN(18);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 4);
                IM_POLYLINE_TRIANGLE(1, 0, 4, 2);
                IM_POLYLINE_TRIANGLE(2, 2, 4, 3);
                IM_POLYLINE_TRIANGLE_END(18);
                idx_start += 3;
            }
            else
            {
                IM_POLYLINE_VERTEX(2, pt_x +           d0_x, pt_y +           d0_y, uv, context.color);
                IM_POLYLINE_VERTEX(3, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX(4, pt_x +           d1_x, pt_y +           d1_y, uv, context.color);
                vtx_write += 3;

                IM_POLYLINE_TRIANGLE_BEGIN(18);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 2);
                IM_POLYLINE_TRIANGLE(1, 0, 2, 3);
                IM_POLYLINE_TRIANGLE(2, 3, 2, 4);
                IM_POLYLINE_TRIANGLE_END(18);
                idx_start += 3;
            }
        }
        else IM_UNLIKELY if (join == Round)
        {
            //
            // Round geometry depends on the sign of the bend direction.
            //
            //  Left arc:                  Right arc:
            //
            //                           |
            //         ..--+ 4           |         5 +--..
            //       .'    |'            |          '|    '.
            //      :      |  '          |         ' |      :
            //     |     3 |   '         |       '   | 2     |
            //   2 +-------+     '       |     '     +-------+ 3
            //     |'''...  ''..  ' 5    |   .'  ..''...''.''|
            //     |      ''''''..:+     | 4 +:''''''  .''   |
            //     |            ...|     |   |      .''      |
            //     |      ...'''   |     |   |   .''         |
            //     |...'''         |     |   |.''            |
            //     + ~ ~ ~ ~ ~ ~ ~ +     |   + ~ ~ ~ ~ ~ ~ ~ +
            //     0               1     |   0               1
            //
            // 4 vertices, 4 triangles
            //

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                IM_POLYLINE_VERTEX(2, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX(3, p1.x,                         p1.y,                         uv, context.color);
                IM_POLYLINE_VERTEX(4, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX(5, p1.x +        miter_offset_x, p1.y +        miter_offset_y, uv, context.color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(12);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 5);
                IM_POLYLINE_TRIANGLE(1, 0, 5, 2);
                IM_POLYLINE_TRIANGLE(2, 2, 5, 3);
                IM_POLYLINE_TRIANGLE(4, 3, 5, 4);
                IM_POLYLINE_TRIANGLE_END(12);
                idx_start += 4;
            }
            else
            {
                IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));

                IM_POLYLINE_VERTEX(2, p1.x,                         p1.y,                         uv, context.color);
                IM_POLYLINE_VERTEX(3, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness, uv, context.color);
                IM_POLYLINE_VERTEX(4, p1.x -        miter_offset_x, p1.y -        miter_offset_y, uv, context.color);
                IM_POLYLINE_VERTEX(5, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.color);
                vtx_write += 4;

                IM_POLYLINE_TRIANGLE_BEGIN(12);
                IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
                IM_POLYLINE_TRIANGLE(1, 0, 3, 4);
                IM_POLYLINE_TRIANGLE(2, 4, 3, 2);
                IM_POLYLINE_TRIANGLE(3, 4, 2, 5);
                IM_POLYLINE_TRIANGLE_END(12);
                idx_start += 4;
            }
        }
        else if (join == ThickButt)
        {
            // Thick butt end one segment with Butt cap and begin next segment with Butt cap
            //
            // Two segments do overlap causing overdraw.
            // 
            //       .'
            //  7  .'   ~   
            //    +.      ~ 
            //      '.       ~
            //  2+----X----+3  .'
            //   |      '.'| .'
            //   |    ..' '+'       + 4' + 5' + 6'
            //   | .''     |8
            //   +'~     ~ +
            //   0         1
            // 
            // 7 vertices, 5 triangles total, 2 triangles (base)
            // 
            // Vertices 4, 5, 6 are used to fill the gap between segments.
            // Gap between segments is filled according to preferred join type.
            //

            const float sin_theta = n0.y * n1.x - n0.x * n1.y;

            IM_POLYLINE_VERTEX(2, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(3, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(4, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(5, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(6, p1.x,                         p1.y,                         uv, context.color);
            IM_POLYLINE_VERTEX(7, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, uv, context.color);
            IM_POLYLINE_VERTEX(8, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, uv, context.color);

            IM_POLYLINE_TRIANGLE_BEGIN(6);
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 0, 3, 2);
            IM_POLYLINE_TRIANGLE_END(6);

            if (preferred_join == Miter)
            {
                // Fill gap between segments with Miter join
                // 
                // Left Miter join:          Right Miter join:
                // 
                //                         |
                //    +----------+ 7       |       8 +----------+
                //   4 \       .' \        |        /      ,,''/ 4
                //      \    .'    \       |       /   ,,''   /
                //       \ .'       \ p1   |   p1 /..''      /
                //        +----------x-~   |   ~-x----------+
                //        2         5 \    |    / 5         3
                //                     ~   |   ~
                // 
                // Used 3 vertices to fill the gap between segments.
                // 
                // 2 join triangles, 4 total
                //

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_VERTEX(4, p1.x - miter_offset_x, p1.y - miter_offset_y, uv, context.color);
                    // 5 is already set to p1

                    IM_POLYLINE_TRIANGLE_BEGIN(6);
                    IM_POLYLINE_TRIANGLE(0, 2, 5, 7);
                    IM_POLYLINE_TRIANGLE(1, 2, 7, 4);
                    IM_POLYLINE_TRIANGLE_END(6);
                }
                else
                {
                    IM_POLYLINE_VERTEX(4, p1.x + miter_offset_x, p1.y + miter_offset_y, uv, context.color);
                    // 5 is already set to p1

                    IM_POLYLINE_TRIANGLE_BEGIN(6);
                    IM_POLYLINE_TRIANGLE(0, 5, 3, 4);
                    IM_POLYLINE_TRIANGLE(1, 5, 4, 8);
                    IM_POLYLINE_TRIANGLE_END(6);
                }
            }
            else if (preferred_join == Bevel || preferred_join == MiterClip)
            {
                if (preferred_join == Bevel)
                {
                    // Fill gap between segments with Bevel join
                    //
                    // Left Bevel join:             Right Bevel join:
                    //
                    //                      |
                    //                      |
                    //          .+ 7        |        8 +
                    //         .' \         |         / '.
                    //       .'    \        |        /    '.
                    //     .'       \ p1    |    p1 /       '.
                    //    +----------x-~    |    ~-x----------+
                    //    2         4 \     |     / 4         3
                    //                 ~    |    ~
                    //
                    // Used 1 vertex fill the gap between segments.
                    //
                    // 1 join triangles, 3 total
                    //

                    if (cos_theta > IM_POLYLINE_MITER_ANGLE_LIMIT)
                    {
                        float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
                        IM_POLYLINE_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness);

                        if (sin_theta < 0.0f)
                        {
                            IM_POLYLINE_TRIANGLE_BEGIN(3);
                            IM_POLYLINE_TRIANGLE(0, 2, 4, 7);
                            IM_POLYLINE_TRIANGLE_END(3);
                        }
                        else
                        {
                            IM_POLYLINE_TRIANGLE_BEGIN(3);
                            IM_POLYLINE_TRIANGLE(0, 4, 3, 8);
                            IM_POLYLINE_TRIANGLE_END(3);
                        }
                    }
                }
                else if (cos_theta > IM_POLYLINE_MITER_ANGLE_LIMIT)
                {
                    // Fill gap between segments with clipped Bevel join
                    //
                    // Left Bevel join:                     Right Bevel join:
                    //
                    //                           |
                    //                 7         |           8 
                    //           5 ..+           |            +..  5
                    //           +'11 \          |           /   '+
                    //         .' '.   \         |          /   .' '.
                    //     4 .'     '.  \        |         /  .'     '. 4
                    //      +....     '. \       |        / .'     ....+
                    //     :     '''''..:.\ p1   |    p1 /.:..'''''     :
                    //    +----------------x-~   |    ~-x----------------+
                    //    2               6 \    |     / 6               3
                    //                       ~   |    ~
                    //
                    // Used 5 vertices to fill the gap between segments.
                    //
                    // 3 join triangles, 7 total
                    //

                    float bevel_normal_x, bevel_normal_y;
                    IM_POLYLINE_BEVEL_NORMAL(bevel_normal_x, bevel_normal_y);

                    float pt_x, pt_y, d0_x, d0_y, d1_x, d1_y;
                    IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY(pt_x, pt_y, d0_x, d0_y, d1_x, d1_y, half_thickness, miter_distance_limit);

                    if (sin_theta < 0.0f)
                    {
                        IM_POLYLINE_VERTEX(4, pt_x - d0_x, pt_y - d0_y, uv, context.color);
                        IM_POLYLINE_VERTEX(5, pt_x - d1_x, pt_y - d1_y, uv, context.color);
                        // 6 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(18);
                        IM_POLYLINE_TRIANGLE(0, 6, 4, 2);
                        IM_POLYLINE_TRIANGLE(1, 6, 5, 4);
                        IM_POLYLINE_TRIANGLE(2, 6, 7, 5);
                        IM_POLYLINE_TRIANGLE_END(18);
                    }
                    else
                    {
                        IM_POLYLINE_VERTEX(4, pt_x + d0_x, pt_y + d0_y, uv, context.color);
                        IM_POLYLINE_VERTEX(5, pt_x + d1_x, pt_y + d1_y, uv, context.color);
                        // 6 is already set to p1

                        IM_POLYLINE_TRIANGLE_BEGIN(18);
                        IM_POLYLINE_TRIANGLE(0, 6, 3, 4);
                        IM_POLYLINE_TRIANGLE(1, 6, 4, 5);
                        IM_POLYLINE_TRIANGLE(2, 6, 5, 8);
                        IM_POLYLINE_TRIANGLE_END(18);
                    }
                }
                else
                {
                    const float inner = miter_distance_limit;

                    IM_POLYLINE_VERTEX(7, p1.x - n1.x * half_thickness - n1.y * inner, p1.y - n1.y * half_thickness + n1.x * inner, uv, context.color);
                    IM_POLYLINE_VERTEX(8, p1.x + n1.x * half_thickness - n1.y * inner, p1.y + n1.y * half_thickness + n1.x * inner, uv, context.color);
                }
            }
            else IM_UNLIKELY if (preferred_join == Round)
            {
                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(-n0.y, -n0.x), ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
                else
                {
                    IM_POLYLINE_ARC(p1, half_thickness, ImAtan2(n0.y, n0.x), -ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)));
                }
            }

            vtx_write += 7;
            idx_start += 7;
        }

        p0 = p1;
        n0 = n1;
    }

    if (context.closed)
    {
        draw_list->_VtxWritePtr[0].pos = vtx_write[0].pos;
        draw_list->_VtxWritePtr[1].pos = vtx_write[1].pos;
    }
    else
    {
        IM_UNLIKELY if (context.cap == ImDrawFlags_CapSquare)
        {
            ImVec2 n_begin = context.normals[0];
            n_begin.x *= half_thickness;
            n_begin.y *= half_thickness;

            ImVec2 n_end = context.normals[context.point_count - 1];
            n_end.x *= half_thickness;
            n_end.y *= half_thickness;

            ImDrawVert* vtx_start = draw_list->_VtxWritePtr;

            vtx_start[0].pos.x -= n_begin.y;
            vtx_start[0].pos.y += n_begin.x;
            vtx_start[1].pos.x -= n_begin.y;
            vtx_start[1].pos.y += n_begin.x;

            vtx_write[0].pos.x += n_end.y;
            vtx_write[0].pos.y -= n_end.x;
            vtx_write[1].pos.x += n_end.y;
            vtx_write[1].pos.y -= n_end.x;
        }
        else IM_UNLIKELY if (context.cap == ImDrawFlags_CapRound)
        {
            ImVec2 n0 = context.normals[0];
            ImVec2 n1 = context.normals[context.point_count - 1];

            float angle0 = ImAtan2(n0.y, n0.x);
            float angle1 = ImAtan2(n1.y, n1.x);

            IM_POLYLINE_ARC(context.points[0],                       half_thickness, angle0,  IM_PI);
            IM_POLYLINE_ARC(context.points[context.point_count - 1], half_thickness, angle1, -IM_PI);
        }
    }

    vtx_write += 2;
    idx_start += 2;

    const int used_vtx_count = (int)(vtx_write - draw_list->_VtxWritePtr);
    const int used_idx_count = (int)(idx_write - draw_list->_IdxWritePtr);

    draw_list->_VtxWritePtr   = vtx_write;
    draw_list->_IdxWritePtr   = idx_write;
    draw_list->_VtxCurrentIdx = idx_start;

    draw_list->PrimUnreserve(idx_count - used_idx_count, vtx_count - used_vtx_count);

    IM_UNLIKELY if (arc_count > 0)
        ImDrawList_Polyline_V3_EmitArcs(draw_list, context, arc_count, context.color);
}

#undef IM_POLYLINE_VERTEX
#undef IM_POLYLINE_TRIANGLE_BEGIN
#undef IM_POLYLINE_TRIANGLE
#undef IM_POLYLINE_TRIANGLE_END
#undef IM_POLYLINE_BEVEL_NORMAL
#undef IM_POLYLINE_BEVEL_VECTORS
#undef IM_POLYLINE_BEVEL_GEOMETRY
#undef IM_POLYLINE_CLIPPED_BEVEL_GEOMETRY
#undef IM_POLYLINE_ARC

void ImDrawList_Polyline_V3(ImDrawList* draw_list, const ImVec2* data, const int count, const ImU32 color, const ImDrawFlags draw_flags, const float thickness, const float miter_limit)
{
#if 1
    draw_list->AddPolyline(data, count, color, draw_flags, thickness, miter_limit);
#else
    IM_UNLIKELY if (count < 2 || thickness <= 0.0f || !(color && IM_COL32_A_MASK))
        return;

    ImDrawList_Polyline_V3_Context context;
    context.points      = data;
    context.point_count = count;
    context.closed      = (draw_flags & ImDrawFlags_Closed) && (count > 2);
    context.join        = (draw_flags & ImDrawFlags_JoinMask_) ? (draw_flags & ImDrawFlags_JoinMask_) : ImDrawFlags_JoinDefault_;
    context.cap         = (draw_flags & ImDrawFlags_CapMask_) ? (draw_flags & ImDrawFlags_CapMask_) : ImDrawFlags_CapDefault_;
    context.miter_limit = miter_limit > 0.0f ? miter_limit : 0.0f;

    // Compute normals and squares of segment lengths for each segment
    {
        draw_list->_Data->TempBuffer.reserve_discard(count + (count + 2) / 2); // 'count' normals and 'count + 1 + 1 (to round up)' segment lengths
        ImVec2* normals             = draw_list->_Data->TempBuffer.Data;
        float*  segments_length_sqr = (float*)(normals + count);

        const int segment_count = count - 1;

        int i = 0;

#if defined(IMGUI_ENABLE_SSE)
        // SSE2 path intentionally is trying to use as few variables as possible to make
        // compiler emit less instructions to store and load them from memory in debug
        // configuration. Optimized build should be able to keep all variables in registers.
        IM_LIKELY if (segment_count >= 4)
        {
            // Process 4 segments at once, single sqrt call is used to compute 4 normals
            for (; i < segment_count / 4; i += 4)
            {
                __m128 diff_01 = _mm_sub_ps(_mm_loadu_ps(&data[i + 1].x), _mm_loadu_ps(&data[i].x));
                __m128 dxy2_01 = _mm_mul_ps(diff_01, diff_01);
                __m128 diff_23 = _mm_sub_ps(_mm_loadu_ps(&data[i + 3].x), _mm_loadu_ps(&data[i + 2].x));
                __m128 dxy2_23 = _mm_mul_ps(diff_23, diff_23);
                __m128 d2_01   = _mm_add_ps(_mm_shuffle_ps(dxy2_01, dxy2_01, _MM_SHUFFLE(2, 0, 2, 0)), _mm_shuffle_ps(dxy2_01, dxy2_01, _MM_SHUFFLE(3, 1, 3, 1)));
                __m128 d2_23   = _mm_add_ps(_mm_shuffle_ps(dxy2_23, dxy2_23, _MM_SHUFFLE(2, 0, 2, 0)), _mm_shuffle_ps(dxy2_23, dxy2_23, _MM_SHUFFLE(3, 1, 3, 1)));
                __m128 d       = _mm_sqrt_ps(_mm_shuffle_ps(d2_01, d2_23, _MM_SHUFFLE(1, 0, 1, 0)));
                __m128 dir_01  = _mm_and_ps(_mm_div_ps(diff_01, _mm_shuffle_ps(d, d, _MM_SHUFFLE(1, 1, 0, 0))), _mm_cmpgt_ps(_mm_shuffle_ps(d2_01, d2_01, _MM_SHUFFLE(1, 1, 0, 0)), _mm_set1_ps(0.0f)));
                __m128 dir_23  = _mm_and_ps(_mm_div_ps(diff_23, _mm_shuffle_ps(d, d, _MM_SHUFFLE(3, 3, 2, 2))), _mm_cmpgt_ps(_mm_shuffle_ps(d2_23, d2_23, _MM_SHUFFLE(1, 1, 0, 0)), _mm_set1_ps(0.0f)));
                _mm_storeu_ps(&normals[i    ].x, _mm_mul_ps(_mm_shuffle_ps(dir_01, dir_01, _MM_SHUFFLE(2, 3, 0, 1)), _mm_set_ps(1.0f, -1.0f, 1.0f, -1.0f)));
                _mm_storeu_ps(&normals[i + 2].x, _mm_mul_ps(_mm_shuffle_ps(dir_23, dir_23, _MM_SHUFFLE(2, 3, 0, 1)), _mm_set_ps(1.0f, -1.0f, 1.0f, -1.0f)));
                _mm_storeu_ps(&segments_length_sqr[i + 1], _mm_shuffle_ps(d2_01, d2_23, _MM_SHUFFLE(1, 0, 1, 0)));
            }
        }

        IM_LIKELY if (segment_count >= 2)
        {
            // Process 2 segments at once
            for (; i < segment_count / 2; i += 2)
            {
                __m128 diff  = _mm_sub_ps(_mm_loadu_ps(&data[i + 1].x), _mm_loadu_ps(&data[i].x));
                __m128 dxy2  = _mm_mul_ps(diff, diff);
                __m128 d2    = _mm_add_ps(_mm_shuffle_ps(dxy2, dxy2, _MM_SHUFFLE(2, 0, 2, 0)), _mm_shuffle_ps(dxy2, dxy2, _MM_SHUFFLE(3, 1, 3, 1)));
                __m128 d     = _mm_sqrt_ps(d2);
                __m128 dir   = _mm_and_ps(_mm_div_ps(diff, _mm_shuffle_ps(d, d, _MM_SHUFFLE(1, 1, 0, 0))), _mm_cmpgt_ps(_mm_shuffle_ps(d2, d2, _MM_SHUFFLE(1, 1, 0, 0)), _mm_set1_ps(0.0f)));
                _mm_storeu_ps(&normals[i].x, _mm_mul_ps(_mm_shuffle_ps(dir, dir, _MM_SHUFFLE(2, 3, 0, 1)), _mm_set_ps(1.0f, -1.0f, 1.0f, -1.0f)));
                _mm_storeu_ps(&segments_length_sqr[i + 1], d2);
            }
        }

        // Remaining segment will fallback to scalar code
#endif

#define IM_POLYLINE_COMPUTE_NORMALS_AND_SEGMENTS_SQUARE_LENGTHS(i0, i1, normal_out, segments_length_sqr_out)    \
        {                                                                                                       \
            const float dx = data[i1].x - data[i0].x;                                                           \
            const float dy = data[i1].y - data[i0].y;                                                           \
            const float d2 = dx * dx + dy * dy;                                                                 \
            const float inv_length = d2 > 0 ? ImRsqrtPrecise(d2) : 0.0f;                                        \
            normal_out.x = -dy * inv_length;                                                                    \
            normal_out.y =  dx * inv_length;                                                                    \
            segments_length_sqr_out = d2;                                                                       \
        }

        for (; i < segment_count; ++i)
            IM_POLYLINE_COMPUTE_NORMALS_AND_SEGMENTS_SQUARE_LENGTHS(i, i + 1, normals[i], segments_length_sqr[i + 1]);
        
        if (context.closed)
        {
            IM_POLYLINE_COMPUTE_NORMALS_AND_SEGMENTS_SQUARE_LENGTHS(count - 1, 0, normals[count - 1], segments_length_sqr[0]);
        }
        else
        {
            normals[count - 1]     = normals[count - 2];
            segments_length_sqr[0] = segments_length_sqr[count - 1];
        }

        segments_length_sqr[count] = segments_length_sqr[0];

#if 0 // #debug: validate if SSE2 path produce same result as scalar method
        int errors = 0;
        for (i = 0; i < count - 1; ++i)
        {
            ImVec2 n;
            float s;
            IM_POLYLINE_COMPUTE_NORMALS_AND_SEGMENTS_SQUARE_LENGTHS(i, i + 1, n, s);

            auto diff_n_x = n.x - normals[i].x;
            auto diff_n_y = n.y - normals[i].y;
            auto diff_s   = s - segments_length_sqr[i + 1];

            if (fabsf(diff_n_x) > 0.0001f)
                errors++;
            if (fabsf(diff_n_y) > 0.0001f)
                errors++;
            if (fabsf(diff_s) > 0.0001f)
                errors++;
        }
        IM_ASSERT(errors == 0);
#endif

#undef IM_POLYLINE_COMPUTE_NORMALS_AND_SEGMENTS_SQUARE_LENGTHS

        context.normals             = normals;
        context.segments_length_sqr = segments_length_sqr;
    }

    // _Path is used to store queue arcs for round caps and joins
    draw_list->_Path.Size = 0;

    if (draw_list->Flags & ImDrawListFlags_AntiAliasedLines)
    {
        context.color        = color;
        context.thickness    = thickness;
        context.fringe_width = draw_list->_FringeScale;
        context.fringe_color = color & ~IM_COL32_A_MASK;

        context.thickness -= draw_list->_FringeScale;
        IM_UNLIKELY if (context.thickness < 0.0f)
        {
            // Blend color alpha using fringe 
            const ImU32 alpha = (ImU32)(((color >> IM_COL32_A_SHIFT) & 0xFF) * (1.0f - ImSaturate(-context.thickness / draw_list->_FringeScale)));
            if (alpha == 0)
                return;

            context.color            = (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
            context.fringe_width    += context.thickness;
            context.fringe_thickness = context.fringe_width * 2.0f;
            context.thickness        = 0.0f;
        }
        else
        {
            context.fringe_thickness = context.thickness + context.fringe_width * 2.0f;
        }

        if (context.thickness > 0.0f)
            ImDrawList_Polyline_V3_Thick_AntiAliased(draw_list, context);
        else
            ImDrawList_Polyline_V3_Thin_AntiAliased(draw_list, context);
    }
    else
    {
        context.color            = color;
        context.fringe_color     = 0;
        context.thickness        = thickness;
        context.fringe_thickness = 0;
        context.fringe_width     = 0;

        ImDrawList_Polyline_V3_NotAntiAliased(draw_list, context);
    }
#endif
}


} // namespace ImGuiEx