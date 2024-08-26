//#define IMGUI_DEFINE_MATH_OPERATORS
#include "polyline_new.h"

namespace ImGuiEx {

void ImDrawList_Polyline(ImDrawList* draw_list, const ImVec2* data, const int count, const ImU32 color, const ImDrawFlags draw_flags, float thickness, float miter_limit)
{
    if (count < 2 || thickness <= 0.0f) [[unlikely]]
        return;

    // Internal join types:
    //   - Miter: default join type, sharp point
    //   - MiterClip: clipped miter join, basically a bevel join with a limited length
    //   - Bevel: straight bevel join between segments
    //   - Butt: flat cap
    //   - ThickButt: flat cap with extra vertices, used for discontinuity between segments
    enum JoinType { Miter, MiterClip, Bevel, Butt, ThickButt };

    const bool        closed         = !!(draw_flags & ImDrawFlags_Closed) && (count > 2);
    const bool        antialias      = !!(draw_list->Flags & ImDrawListFlags_AntiAliasedLines);
    const ImDrawFlags join_flags     = draw_flags & ImDrawFlags_JoinMask_;
    const ImDrawFlags cap_flags      = draw_flags & ImDrawFlags_CapMask_;
    const ImDrawFlags join           = join_flags ? join_flags : ImDrawFlags_JoinDefault_;
    const ImDrawFlags cap            = cap_flags ? cap_flags : ImDrawFlags_CapDefault_;
    
    // Pick default join type based on join flags
    const int default_join_type       = join == ImDrawFlags_JoinBevel ? Bevel : (join == ImDrawFlags_JoinMiterClip ? MiterClip : Miter);
    const int default_join_limit_type = join == ImDrawFlags_JoinMiterClip ? MiterClip : Bevel;

    // Compute segment normals and lengths
    ImVec2* normals = nullptr;
    float* segments_length_sqr = nullptr;
    {
        const int segment_normal_count     = count;
        const int segment_length_sqr_count = count + 1;

        const int normals_bytes             = segment_normal_count * sizeof(ImVec2);
        const int segments_length_sqr_bytes = segment_length_sqr_count * sizeof(float);
        const int buffer_size               = normals_bytes + segments_length_sqr_bytes;
        const int buffer_item_count         = (buffer_size + sizeof(ImVec2) - 1) / sizeof(ImVec2);

        draw_list->_Data->TempBuffer.reserve_discard(buffer_item_count);
        normals = reinterpret_cast<ImVec2*>(draw_list->_Data->TempBuffer.Data);
        segments_length_sqr = reinterpret_cast<float*>(normals + segment_normal_count);

#define IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(i0, i1, n)              \
        do                                                          \
        {                                                           \
            float dx = data[i1].x - data[i0].x;                     \
            float dy = data[i1].y - data[i0].y;                     \
            float d2 = dx * dx + dy * dy;                           \
            if (d2 > 0.0f)                                          \
            {                                                       \
                float inv_len = ImRsqrt(d2);                        \
                dx *= inv_len;                                      \
                dy *= inv_len;                                      \
            }                                                       \
            normals[i0].x =  dy;                                    \
            normals[i0].y = -dx;                                    \
            segments_length_sqr[i1] = d2;                           \
        } while (false)

        for (int i = 0; i < count - 1; ++i)
            IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(i, i + 1, normals[i]);

        if (closed)
        {
            IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(count - 1, 0, normals[count - 1]);
        }
        else
        {
            normals[count - 1] = normals[count - 2];
            segments_length_sqr[0] = segments_length_sqr[count - 1];
        }

        segments_length_sqr[count] = segments_length_sqr[0];

#undef IM_POLYLINE_COMPUTE_SEGMENT_DETAILS
    }

#define IM_POLYLINE_VERTEX(N, X, Y)                             \
        draw_list->_VtxWritePtr[N].pos.x = X;                   \
        draw_list->_VtxWritePtr[N].pos.y = Y;                   \
        draw_list->_VtxWritePtr[N].uv    = uv;                  \
        draw_list->_VtxWritePtr[N].col   = color

#define IM_POLYLINE_TRIANGLE(N, A, B, C)                        \
        draw_list->_IdxWritePtr[N * 3 + 0] = idx_base + A;      \
        draw_list->_IdxWritePtr[N * 3 + 1] = idx_base + B;      \
        draw_list->_IdxWritePtr[N * 3 + 2] = idx_base + C

    // Most dimensions are squares of the actual values, fit nicely with trigonometric identities
    const float half_thickness     = thickness * 0.5f;
    const float half_thickness_sqr = half_thickness * half_thickness;

    const float clamped_miter_limit = ImMax(0.0f, miter_limit);
    const float miter_distance_limit = half_thickness * clamped_miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    // miter square formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
    const float miter_angle_limit = 2.0f / (clamped_miter_limit * clamped_miter_limit) - 1.0f;

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const auto uv             = draw_list->_Data->TexUvWhitePixel;
    const auto vtx_count      = count * 6 + 2; // top 6 vertices per join, 2 vertices per butt cap
    const auto idx_count      = count * 4 * 3; // top 4 triangles per join
    auto       idx_base       = draw_list->_VtxCurrentIdx;

    draw_list->PrimReserve(idx_count, vtx_count);

    const auto vtx_start = draw_list->_VtxWritePtr;
    const auto idx_start = draw_list->_IdxWritePtr;

    // Last two vertices in the vertex buffer are reserved to next segment to build upon
    // This is true for all segments.
    ImVec2 p0 =    data[closed ? count - 1 : 0];
    ImVec2 n0 = normals[closed ? count - 1 : 0];

    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_thickness, p0.y - n0.y * half_thickness);
    IM_POLYLINE_VERTEX(1, p0.x + n0.x * half_thickness, p0.y + n0.y * half_thickness);

    draw_list->_VtxWritePtr += 2;

    int last_join_type = Butt;

    for (int i = closed ? 0 : 1; i < count; ++i)
    {
        const ImVec2 p1 =    data[i];
        const ImVec2 n1 = normals[i];

        // theta - angle between segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;
        const float sin_theta = n0.y * n1.x - n0.x * n1.y;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const float miter_scale_factor = cos_theta > -0.995f ? half_thickness / (1.0f + cos_theta) : FLT_MAX;
        const float miter_offset_x = (n0.x + n1.x) * miter_scale_factor;
        const float miter_offset_y = (n0.y + n1.y) * miter_scale_factor;

        // always have to know if join miter is limited
        const float miter_distance_sqr = miter_offset_x * miter_offset_x + miter_offset_y * miter_offset_y;
        const bool  limit_miter        = (miter_distance_sqr > miter_distance_limit_sqr) || (cos_theta < miter_angle_limit);

        // check for discontinuity, miter distance greater than segment lengths will mangle geometry
        // so we create disconnect and create overlapping geometry just to keep overall shape correct
        const float segment_length_sqr      = segments_length_sqr[i];
        const float next_segment_length_sqr = segments_length_sqr[i + 1];
        const bool  disconnected       = (segment_length_sqr < miter_distance_sqr) || (next_segment_length_sqr < miter_distance_sqr) || (segment_length_sqr < half_thickness_sqr);

        // select join type
        int preferred_join_type = Butt;
        if (closed || !(i == count - 1)) [[likely]]
        {
            preferred_join_type = limit_miter ? default_join_limit_type : default_join_type;

            // MiterClip need to be 'clamped' to Bevel if too short or to Miter if clip is not necessary
            if (preferred_join_type == MiterClip)
            {
                const float miter_clip_min_distance_sqr = 0.5f * half_thickness_sqr * (cos_theta + 1);

                if (miter_distance_limit_sqr < miter_clip_min_distance_sqr)
                    preferred_join_type = Bevel;
                else if (miter_distance_sqr < miter_distance_limit_sqr)
                    preferred_join_type = Miter;
            }
        }

        // Actual join used does account for discontinuity
        const int join_type = disconnected ? ThickButt : preferred_join_type;

#if 0
        float xxx = sin_theta / (1.0f + cos_theta);

        ImDrawList* draw_list_2 = ImGui::GetForegroundDrawList();
        ImGuiTextBuffer debug_text;
        debug_text.appendf("i: %d, a: %.2f, c: %.2f, m: %.2f (max: %.2f), s: %.2f, limit: %d, disc: %d, xxx: %.2f\n",
            i,
            ImAcos(ImClamp(cos_theta, -1.0f, 1.0f)) * 180 / IM_PI,
            sin_theta,
            ImSqrt(miter_distance_sqr), ImSqrt(miter_distance_limit_sqr),
            ImSqrt(segment_length_sqr),
            limit_miter ? 1 : 0,
            disconnected ? 1 : 0,
            xxx
        );
        draw_list_2->AddText(ImVec2(10, 10 + i * ImGui::GetTextLineHeight()), IM_COL32(255, 255, 255, 255), debug_text.begin());
#endif

        int new_vtx_count = 0;

        // Joins try to emit as little vertices as possible.
        // Last two vertices will be reused by next segment.
        if (join_type == Miter) [[likely]]
        {
            IM_POLYLINE_VERTEX(0, p1.x - miter_offset_x, p1.y - miter_offset_y);
            IM_POLYLINE_VERTEX(1, p1.x + miter_offset_x, p1.y + miter_offset_y);
            new_vtx_count = 2;
        }
        else if (join_type == Bevel) [[likely]]
        {
            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(0, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness);
                IM_POLYLINE_VERTEX(1, p1.x - miter_offset_x,        p1.y - miter_offset_y);
                IM_POLYLINE_VERTEX(2, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness);
            }
            else
            {
                IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness);
                IM_POLYLINE_VERTEX(1, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness);
                IM_POLYLINE_VERTEX(2, p1.x + miter_offset_x,        p1.y + miter_offset_y);
            }
            new_vtx_count = 3;
        }
        else if (join_type == MiterClip) [[unlikely]]
        {
            // Note: MiterClip does require sqrt() because we have to compute point on the clip line
            //       which is not the same as miter offset point

            ImVec2 clip_line_direction = ImVec2(n0.x + n1.x, n0.y + n1.y);
            const float clip_line_normal_sqr = clip_line_direction.x * clip_line_direction.x + clip_line_direction.y * clip_line_direction.y;
            if (clip_line_normal_sqr > 0.0f) [[likely]]
            {
                const float clip_line_inv_len = ImRsqrt(clip_line_normal_sqr);
                clip_line_direction.x *= clip_line_inv_len;
                clip_line_direction.y *= clip_line_inv_len;
            }

            const auto clip_projection = n0.y * clip_line_direction.x - n0.x * clip_line_direction.y;

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(0, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness);
                IM_POLYLINE_VERTEX(1, p1.x - miter_offset_x,        p1.y - miter_offset_y);
                IM_POLYLINE_VERTEX(2, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness);

                // We do not care for degenerate case when two segments are parallel to each
                // other because this case will always be handled by discontinuity in ThickButt
                if (clip_projection != 0) [[likely]]
                {
                    const auto clip_line_point   = ImVec2(p1.x + clip_line_direction.x * miter_distance_limit, p1.y + clip_line_direction.y * miter_distance_limit);
                    const auto clip_point_offset = (n0.x * (clip_line_point.x - draw_list->_VtxWritePtr[0].pos.x) + n0.y * (clip_line_point.y - draw_list->_VtxWritePtr[0].pos.y)) / clip_projection;

                    draw_list->_VtxWritePtr[0].pos.x = clip_line_point.x + (clip_point_offset * clip_line_direction.y);
                    draw_list->_VtxWritePtr[0].pos.y = clip_line_point.y - (clip_point_offset * clip_line_direction.x);
                    draw_list->_VtxWritePtr[2].pos.x = clip_line_point.x - (clip_point_offset * clip_line_direction.y);
                    draw_list->_VtxWritePtr[2].pos.y = clip_line_point.y + (clip_point_offset * clip_line_direction.x);
                }
            }
            else
            {
                IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness);
                IM_POLYLINE_VERTEX(1, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness);
                IM_POLYLINE_VERTEX(2, p1.x + miter_offset_x,        p1.y + miter_offset_y);

                // We do not care for degenerate case when two segments are parallel to each
                // other because this case will always be handled by discontinuity in ThickButt
                if (clip_projection != 0) [[likely]]
                {
                    const auto clip_line_point = ImVec2(p1.x - clip_line_direction.x * miter_distance_limit, p1.y - clip_line_direction.y * miter_distance_limit);
                    const auto clip_point_offset = (n0.x * (clip_line_point.x - draw_list->_VtxWritePtr[0].pos.x) + n0.y * (clip_line_point.y - draw_list->_VtxWritePtr[0].pos.y)) / clip_projection;

                    draw_list->_VtxWritePtr[0].pos.x = clip_line_point.x + (clip_point_offset * clip_line_direction.y);
                    draw_list->_VtxWritePtr[0].pos.y = clip_line_point.y - (clip_point_offset * clip_line_direction.x);
                    draw_list->_VtxWritePtr[1].pos.x = clip_line_point.x - (clip_point_offset * clip_line_direction.y);
                    draw_list->_VtxWritePtr[1].pos.y = clip_line_point.y + (clip_point_offset * clip_line_direction.x);
                }
            }
            new_vtx_count = 3;
        }
        else if (join_type == Butt) [[unlikely]]
        {
            IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness);
            IM_POLYLINE_VERTEX(1, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness);
            IM_POLYLINE_VERTEX(2, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness);
            IM_POLYLINE_VERTEX(3, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness);
            new_vtx_count = 4;
        }
        else if (join_type == ThickButt) [[unlikely]]
        {
            // 2 and 3 vertices are reserved for individual join types
            IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness);
            IM_POLYLINE_VERTEX(1, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness);
            IM_POLYLINE_VERTEX(4, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness);
            IM_POLYLINE_VERTEX(5, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness);
            new_vtx_count = 6;

            // ThickButt is always a discontinuity, yet we care here to fill the joins
            // and reuse of ThickButt vertices
            if (preferred_join_type == Bevel) [[unlikely]]
            {
                IM_POLYLINE_VERTEX(2, p1.x, p1.y);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_TRIANGLE(0, 3, 7, 4);
                }
                else
                {
                    IM_POLYLINE_TRIANGLE(0, 2, 4, 6);
                }

                draw_list->_IdxWritePtr += 3;
            }
            else if (preferred_join_type == Miter) [[unlikely]]
            {
                IM_POLYLINE_VERTEX(2, p1.x, p1.y);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_VERTEX(3, p1.x + miter_offset_x, p1.y + miter_offset_y);
                    IM_POLYLINE_TRIANGLE(0, 3, 5, 4);
                    IM_POLYLINE_TRIANGLE(1, 5, 4, 7);
                    draw_list->_IdxWritePtr += 6;
                }
                else
                {
                    IM_POLYLINE_VERTEX(3, p1.x - miter_offset_x, p1.y - miter_offset_y);
                    IM_POLYLINE_TRIANGLE(0, 2, 5, 4);
                    IM_POLYLINE_TRIANGLE(1, 5, 4, 6);
                    draw_list->_IdxWritePtr += 6;
                }
            }
            else if (preferred_join_type == MiterClip) [[unlikely]]
            {
                IM_POLYLINE_VERTEX(2, p1.x, p1.y);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_TRIANGLE(0, 3, 7, 4);
                }
                else
                {
                    IM_POLYLINE_TRIANGLE(0, 2, 4, 6);
                }

                draw_list->_IdxWritePtr += 3;
            }
        }

        if (join_type == Bevel || join_type == MiterClip)
        {
            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_TRIANGLE(0, 0, 2, 3);
                IM_POLYLINE_TRIANGLE(1, 0, 1, 2);
                IM_POLYLINE_TRIANGLE(2, 3, 2, 4);
            }
            else
            {
                IM_POLYLINE_TRIANGLE(0, 0, 4, 2);
                IM_POLYLINE_TRIANGLE(1, 0, 1, 4);
                IM_POLYLINE_TRIANGLE(2, 2, 4, 3);
            }
            draw_list->_IdxWritePtr += 9;
        }
        else
        {
            IM_POLYLINE_TRIANGLE(0, 0, 3, 2);
            IM_POLYLINE_TRIANGLE(1, 0, 1, 3);
            draw_list->_IdxWritePtr += 6;
        }

        draw_list->_VtxWritePtr += new_vtx_count;
        idx_base += new_vtx_count;

        p0 = p1;
        n0 = n1;

        last_join_type = join_type;
    }

    if (closed)
    {
        idx_base += 2;

        vtx_start[0].pos = draw_list->_VtxWritePtr[-2].pos;
        vtx_start[1].pos = draw_list->_VtxWritePtr[-1].pos;
    }
    else
    {
        draw_list->_VtxWritePtr -= 2;

        if (cap == ImDrawFlags_CapSquare) [[unlikely]]
        {
            const ImVec2 n0 = normals[0];
            const ImVec2 n1 = normals[count - 1];

            vtx_start[0].pos.x                += n0.y * half_thickness;
            vtx_start[0].pos.y                -= n0.x * half_thickness;
            vtx_start[1].pos.x                += n0.y * half_thickness;
            vtx_start[1].pos.y                -= n0.x * half_thickness;
            draw_list->_VtxWritePtr[-2].pos.x -= n1.y * half_thickness;
            draw_list->_VtxWritePtr[-2].pos.y += n1.x * half_thickness;
            draw_list->_VtxWritePtr[-1].pos.x -= n1.y * half_thickness;
            draw_list->_VtxWritePtr[-1].pos.y += n1.x * half_thickness;
        }
    }

    const int used_vtx_count = static_cast<int>(draw_list->_VtxWritePtr - vtx_start);
    const int used_idx_count = static_cast<int>(draw_list->_IdxWritePtr - idx_start);
    const int unused_vtx_count = vtx_count - used_vtx_count;
    const int unused_idx_count = idx_count - used_idx_count;

    draw_list->PrimUnreserve(unused_idx_count, unused_vtx_count);

    draw_list->_VtxCurrentIdx = idx_base;

#undef IM_POLYLINE_VERTEX
#undef IM_POLYLINE_TRIANGLE
}


} // namespace ImGuiEx