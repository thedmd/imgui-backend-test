#define IMGUI_DEFINE_MATH_OPERATORS
#include "polyline_pr2964.h"
#include <imgui_internal.h>

namespace ImGuiEx {

#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImRsqrt(d2); VX *= inv_len; VY *= inv_len; } } (void)0
#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

void ImDrawList_Polyline_PR2964(ImDrawList* draw_list, const ImVec2* points, int points_count, ImU32 col, ImDrawFlags flags, float thickness)
{
    if (points_count < 2 || (col & IM_COL32_A_MASK) == 0)
        return;

    auto& Flags = draw_list->Flags;
    auto& _FringeScale = draw_list->_FringeScale;
    auto& _Data = draw_list->_Data;
    auto& _VtxWritePtr = draw_list->_VtxWritePtr;
    auto& _IdxWritePtr = draw_list->_IdxWritePtr;
    auto& _VtxCurrentIdx = draw_list->_VtxCurrentIdx;

    const bool closed = (flags & ImDrawFlags_Closed) != 0;
    const ImVec2 opaque_uv = _Data->TexUvWhitePixel;
    const int count = closed ? points_count : points_count - 1; // The number of line segments we need to draw
    const bool thick_line = (thickness > _FringeScale);

    const float AA_SIZE = _FringeScale;
    const ImU32 col_trans = col & ~IM_COL32_A_MASK;
    const bool antialias = (Flags & ImDrawListFlags_AntiAliasedLines) != 0;

    if (antialias && !thick_line)
    {
        // Anti-aliased stroke approximation
        const int idx_count = count * 12;
        const int vtx_count = count * 6;      // FIXME-OPT: Not sharing edges
        draw_list->PrimReserve(idx_count, vtx_count);
        ImU32 col_faded = col;
        if (thickness < 1.0f)
            col_faded = col_trans | ((int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * thickness / _FringeScale) << IM_COL32_A_SHIFT);

        for (int i1 = 0; i1 < count; i1++)
        {
            const int i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            const ImVec2& p1 = points[i1];
            const ImVec2& p2 = points[i2];

            float dx = p2.x - p1.x;
            float dy = p2.y - p1.y;
            IM_NORMALIZE2F_OVER_ZERO(dx, dy);
            dx *= AA_SIZE;
            dy *= AA_SIZE;

            _VtxWritePtr[0].pos.x = p1.x + dy; _VtxWritePtr[0].pos.y = p1.y - dx; _VtxWritePtr[0].uv = opaque_uv; _VtxWritePtr[0].col = col_trans;
            _VtxWritePtr[1].pos.x = p1.x;      _VtxWritePtr[1].pos.y = p1.y;      _VtxWritePtr[1].uv = opaque_uv; _VtxWritePtr[1].col = col_faded;
            _VtxWritePtr[2].pos.x = p1.x - dy; _VtxWritePtr[2].pos.y = p1.y + dx; _VtxWritePtr[2].uv = opaque_uv; _VtxWritePtr[2].col = col_trans;

            _VtxWritePtr[3].pos.x = p2.x + dy; _VtxWritePtr[3].pos.y = p2.y - dx; _VtxWritePtr[3].uv = opaque_uv; _VtxWritePtr[3].col = col_trans;
            _VtxWritePtr[4].pos.x = p2.x;      _VtxWritePtr[4].pos.y = p2.y;      _VtxWritePtr[4].uv = opaque_uv; _VtxWritePtr[4].col = col_faded;
            _VtxWritePtr[5].pos.x = p2.x - dy; _VtxWritePtr[5].pos.y = p2.y + dx; _VtxWritePtr[5].uv = opaque_uv; _VtxWritePtr[5].col = col_trans;
            _VtxWritePtr += 6;

            _IdxWritePtr[0] = (ImDrawIdx)(_VtxCurrentIdx);     _IdxWritePtr[1] = (ImDrawIdx)(_VtxCurrentIdx + 1); _IdxWritePtr[2] = (ImDrawIdx)(_VtxCurrentIdx + 4);
            _IdxWritePtr[3] = (ImDrawIdx)(_VtxCurrentIdx);     _IdxWritePtr[4] = (ImDrawIdx)(_VtxCurrentIdx + 4); _IdxWritePtr[5] = (ImDrawIdx)(_VtxCurrentIdx + 3);
            _IdxWritePtr[6] = (ImDrawIdx)(_VtxCurrentIdx + 1); _IdxWritePtr[7] = (ImDrawIdx)(_VtxCurrentIdx + 2); _IdxWritePtr[8] = (ImDrawIdx)(_VtxCurrentIdx + 5);
            _IdxWritePtr[9] = (ImDrawIdx)(_VtxCurrentIdx + 1); _IdxWritePtr[10]= (ImDrawIdx)(_VtxCurrentIdx + 5); _IdxWritePtr[11]= (ImDrawIdx)(_VtxCurrentIdx + 4);
            _IdxWritePtr += 12;
            _VtxCurrentIdx += 6;
        }
    }
    else
    {
        // Precise line with bevels on acute angles
        const int max_n_vtx = antialias ? 6 : 3;
        const int max_n_idx = 3 * (antialias ? 9 : 3);
        const int vtx_count = points_count * max_n_vtx;
        const int idx_count = count * max_n_idx;
        draw_list->PrimReserve(idx_count, vtx_count);

        const float half_thickness = (antialias ? thickness - AA_SIZE : thickness) * 0.5f;
        const float half_thickness_aa = half_thickness + AA_SIZE;
        const unsigned int first_vtx_idx = _VtxCurrentIdx;

        // Declare temporary variables in outer scope. Declared without a default value would trigger static analyser, and doing it in inner-scope would be more wasteful.
        float mlx, mly, mrx, mry;                                   // Left and right miters
        float mlax = 0.0f, mlay = 0.0f, mrax = 0.0f, mray = 0.0f;   // Left and right miters including anti-aliasing
        float b1x = 0.0f, b1y = 0.0f, b2x = 0.0f, b2y = 0.0f;       // First and second bevel point
        float b1ax = 0.0f, b1ay = 0.0f, b2ax = 0.0f, b2ay = 0.0f;   // First and second bevel point including anti-aliasing

        float sqlen1 = 0.0f;
        float dx1 = 0.0f, dy1 = 0.0f;
        if (closed)
        {
            dx1 = points[0].x - points[points_count - 1].x;
            dy1 = points[0].y - points[points_count - 1].y;
            sqlen1 = dx1 * dx1 + dy1 * dy1;
            IM_NORMALIZE2F_OVER_ZERO(dx1, dy1);
        }

        for (int i1 = 0; i1 < points_count; i1++)
        {
            const int i2 = (i1 + 1 == points_count) ? 0 : i1 + 1;
            const ImVec2& p1 = points[i1];
            const ImVec2& p2 = points[i2];
            float dx2 = p1.x - p2.x;
            float dy2 = p1.y - p2.y;
            float sqlen2 = dx2 * dx2 + dy2 * dy2;
            IM_NORMALIZE2F_OVER_ZERO(dx2, dy2);

            if (!closed && i1 == 0)
            {
                dx1 = -dx2;
                dy1 = -dy2;
                sqlen1 = sqlen2;
            }
            else if (!closed && i1 == points_count - 1)
            {
                dx2 = -dx1;
                dy2 = -dy1;
                sqlen2 = sqlen1;
            }

            float miter_l_recip = dx1 * dy2 - dy1 * dx2;
            const bool bevel = (dx1 * dx2 + dy1 * dy2) > 1e-5f;
            if (ImFabs(miter_l_recip) > 1e-5f)
            {
                float miter_l = half_thickness / miter_l_recip;
                // Limit (inner) miter so it doesn't shoot away when miter is longer than adjacent line segments on acute angles
                if (bevel)
                {
                    // This is too aggressive (not exactly precise)
                    float min_sqlen = sqlen1 > sqlen2 ? sqlen2 : sqlen1;
                    float miter_sqlen = ((dx1 + dx2) * (dx1 + dx2) + (dy1 + dy2) * (dy1 + dy2)) * miter_l * miter_l;
                    if (miter_sqlen > min_sqlen)
                        miter_l *= ImSqrt(min_sqlen / miter_sqlen);
                }
                mlx = p1.x - (dx1 + dx2) * miter_l;
                mly = p1.y - (dy1 + dy2) * miter_l;
                mrx = p1.x + (dx1 + dx2) * miter_l;
                mry = p1.y + (dy1 + dy2) * miter_l;
                if (antialias)
                {
                    float miter_al = half_thickness_aa / miter_l_recip;
                    mlax = p1.x - (dx1 + dx2) * miter_al;
                    mlay = p1.y - (dy1 + dy2) * miter_al;
                    mrax = p1.x + (dx1 + dx2) * miter_al;
                    mray = p1.y + (dy1 + dy2) * miter_al;
                }
            }
            else
            {
                // Avoid degeneracy for (nearly) straight lines
                mlx = p1.x + dy1 * half_thickness;
                mly = p1.y - dx1 * half_thickness;
                mrx = p1.x - dy1 * half_thickness;
                mry = p1.y + dx1 * half_thickness;
                if (antialias)
                {
                    mlax = p1.x + dy1 * half_thickness_aa;
                    mlay = p1.y - dx1 * half_thickness_aa;
                    mrax = p1.x - dy1 * half_thickness_aa;
                    mray = p1.y + dx1 * half_thickness_aa;
                }
            }
            // The two bevel vertices if the angle is right or obtuse
            // miter_sign == 1, iff the outer (maybe bevelled) edge is on the right, -1 iff it is on the left
            int miter_sign = (miter_l_recip >= 0) - (miter_l_recip < 0);
            if (bevel)
            {
                // FIXME-OPT: benchmark if doing these computations only once in AA case saves cycles
                b1x = p1.x + (dx1 - dy1 * miter_sign) * half_thickness;
                b1y = p1.y + (dy1 + dx1 * miter_sign) * half_thickness;
                b2x = p1.x + (dx2 + dy2 * miter_sign) * half_thickness;
                b2y = p1.y + (dy2 - dx2 * miter_sign) * half_thickness;
                if (antialias)
                {
                    b1ax = p1.x + (dx1 - dy1 * miter_sign) * half_thickness_aa;
                    b1ay = p1.y + (dy1 + dx1 * miter_sign) * half_thickness_aa;
                    b2ax = p1.x + (dx2 + dy2 * miter_sign) * half_thickness_aa;
                    b2ay = p1.y + (dy2 - dx2 * miter_sign) * half_thickness_aa;
                }
            }

            // Set the previous line direction so it doesn't need to be recomputed
            dx1 = -dx2;
            dy1 = -dy2;
            sqlen1 = sqlen2;

            // Now that we have all the point coordinates, put them into buffers

            // Vertices for each point are ordered in vertex buffer like this (looking in the direction of the polyline):
            // - left vertex*
            // - right vertex*
            // - left vertex AA fringe*  (if antialias)
            // - right vertex AA fringe* (if antialias)
            // - the remaining vertex (if bevel)
            // - the remaining vertex AA fringe (if bevel and antialias)
            // (*) if there is bevel, these vertices are the ones on the incoming edge.
            // Having all the vertices of the incoming edge in predictable positions is important - we reference them
            // even if we don't know relevant line properties yet

            const int vertex_count = antialias ? (bevel ? 6 : 4) : (bevel ? 3 : 2); // FIXME: shorten the expression
            const unsigned int bi = antialias ? 4 : 2; // Outgoing edge bevel vertex index
            const bool bevel_l = bevel && miter_sign < 0;
            const bool bevel_r = bevel && miter_sign > 0;

            _VtxWritePtr[0].pos.x = bevel_l ? b1x : mlx; _VtxWritePtr[0].pos.y = bevel_l ? b1y : mly; _VtxWritePtr[0].uv = opaque_uv; _VtxWritePtr[0].col = col;
            _VtxWritePtr[1].pos.x = bevel_r ? b1x : mrx; _VtxWritePtr[1].pos.y = bevel_r ? b1y : mry; _VtxWritePtr[1].uv = opaque_uv; _VtxWritePtr[1].col = col;
            if (bevel)
            {
                _VtxWritePtr[bi].pos.x = b2x; _VtxWritePtr[bi].pos.y = b2y; _VtxWritePtr[bi].uv = opaque_uv; _VtxWritePtr[bi].col = col;
            }

            if (antialias)
            {
                _VtxWritePtr[2].pos.x = bevel_l ? b1ax : mlax; _VtxWritePtr[2].pos.y = bevel_l ? b1ay : mlay; _VtxWritePtr[2].uv = opaque_uv; _VtxWritePtr[2].col = col_trans;
                _VtxWritePtr[3].pos.x = bevel_r ? b1ax : mrax; _VtxWritePtr[3].pos.y = bevel_r ? b1ay : mray; _VtxWritePtr[3].uv = opaque_uv; _VtxWritePtr[3].col = col_trans;
                if (bevel)
                {
                    _VtxWritePtr[5].pos.x = b2ax; _VtxWritePtr[5].pos.y = b2ay; _VtxWritePtr[5].uv = opaque_uv; _VtxWritePtr[5].col = col_trans;
                }
            }
            _VtxWritePtr += vertex_count;

            if (i1 < count)
            {
                const int vtx_next_id = i1 < points_count - 1 ? _VtxCurrentIdx + vertex_count : first_vtx_idx;
                unsigned int l1i = _VtxCurrentIdx + (bevel_l ? bi : 0);
                unsigned int r1i = _VtxCurrentIdx + (bevel_r ? bi : 1);
                unsigned int l2i = vtx_next_id;
                unsigned int r2i = vtx_next_id + 1;
                unsigned int ebi = _VtxCurrentIdx + (bevel_l ? 0 : 1); // incoming edge bevel vertex index

                _IdxWritePtr[0] = (ImDrawIdx)l1i; _IdxWritePtr[1] = (ImDrawIdx)r1i; _IdxWritePtr[2] = (ImDrawIdx)r2i;
                _IdxWritePtr[3] = (ImDrawIdx)l1i; _IdxWritePtr[4] = (ImDrawIdx)r2i; _IdxWritePtr[5] = (ImDrawIdx)l2i;
                _IdxWritePtr += 6;

                if (bevel)
                {
                    _IdxWritePtr[0] = (ImDrawIdx)l1i; _IdxWritePtr[1] = (ImDrawIdx)r1i; _IdxWritePtr[2] = (ImDrawIdx)ebi;
                    _IdxWritePtr += 3;
                }

                if (antialias)
                {
                    unsigned int l1ai = _VtxCurrentIdx + (bevel_l ? 5 : 2);
                    unsigned int r1ai = _VtxCurrentIdx + (bevel_r ? 5 : 3);
                    unsigned int l2ai = vtx_next_id + 2;
                    unsigned int r2ai = vtx_next_id + 3;

                    _IdxWritePtr[0] = (ImDrawIdx)l1ai; _IdxWritePtr[1] = (ImDrawIdx)l1i; _IdxWritePtr[2] = (ImDrawIdx)l2i;
                    _IdxWritePtr[3] = (ImDrawIdx)l1ai; _IdxWritePtr[4] = (ImDrawIdx)l2i; _IdxWritePtr[5] = (ImDrawIdx)l2ai;
                    _IdxWritePtr[6] = (ImDrawIdx)r1ai; _IdxWritePtr[7] = (ImDrawIdx)r1i; _IdxWritePtr[8] = (ImDrawIdx)r2i;
                    _IdxWritePtr[9] = (ImDrawIdx)r1ai; _IdxWritePtr[10] = (ImDrawIdx)r2i; _IdxWritePtr[11] = (ImDrawIdx)r2ai;
                    _IdxWritePtr += 12;

                    if (bevel)
                    {
                        _IdxWritePtr[0] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 1 : 2));
                        _IdxWritePtr[1] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 3 : 0));
                        _IdxWritePtr[2] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 5 : 4));

                        _IdxWritePtr[3] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 1 : 2));
                        _IdxWritePtr[4] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 5 : 4));
                        _IdxWritePtr[5] = (ImDrawIdx)(_VtxCurrentIdx + (bevel_r ? 4 : 5));
                        _IdxWritePtr += 6;
                    }
                }
            }
            _VtxCurrentIdx += vertex_count;
        }

        const int unused_indices = (int)(draw_list->IdxBuffer.Data + draw_list->IdxBuffer.Size - _IdxWritePtr);
        const int unused_vertices = (int)(draw_list->VtxBuffer.Size - _VtxCurrentIdx - draw_list->_CmdHeader.VtxOffset);
        if (unused_indices > 0 || unused_vertices > 0)
            draw_list->PrimUnreserve(unused_indices, unused_vertices);
    }
}


} // namespace ImGuiEx