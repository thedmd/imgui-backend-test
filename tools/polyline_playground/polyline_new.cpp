//#define IMGUI_DEFINE_MATH_OPERATORS
#include "polyline_new.h"

namespace ImGuiEx {

#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImRsqrt(d2); VX *= inv_len; VY *= inv_len; } } (void)0
//#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImRsqrtPrecise(d2); VX *= inv_len; VY *= inv_len; } } (void)0
//#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float len = ImSqrt(d2); VX /= len; VY /= len; } } (void)0
#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

static void ImDrawList_Polyline_NoAA(ImDrawList* draw_list, const ImVec2* data, const int count, const ImU32 color, const ImDrawFlags draw_flags, const float thickness, float miter_limit)
{
    [[unlikely]] if (count < 2 || thickness <= 0.0f || !(color && IM_COL32_A_MASK))
        return;

    // Internal join types:
    //   - Miter: default join type, sharp point
    //   - MiterClip: clipped miter join, basically a bevel join with a limited length
    //   - Bevel: straight bevel join between segments
    //   - Butt: flat cap
    //   - ThickButt: flat cap with extra vertices, used for discontinuity between segments
    enum JoinType { Miter, MiterClip, Bevel, Butt, ThickButt };

    const bool        closed         = !!(draw_flags & ImDrawFlags_Closed) && (count > 2);
    const ImDrawFlags join_flags     = draw_flags & ImDrawFlags_JoinMask_;
    const ImDrawFlags cap_flags      = draw_flags & ImDrawFlags_CapMask_;
    const ImDrawFlags join           = join_flags ? join_flags : ImDrawFlags_JoinDefault_;
    const ImDrawFlags cap            = cap_flags ? cap_flags : ImDrawFlags_CapDefault_;

    // Pick default join type based on join flags
    const JoinType default_join_type       = join == ImDrawFlags_JoinBevel ? Bevel : (join == ImDrawFlags_JoinMiterClip ? MiterClip : Miter);
    const JoinType default_join_limit_type = join == ImDrawFlags_JoinMiterClip ? MiterClip : Bevel;

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

#define IM_POLYLINE_VERTEX(N, X, Y, C)                          \
    {                                                           \
        draw_list->_VtxWritePtr[N].pos.x = X;                   \
        draw_list->_VtxWritePtr[N].pos.y = Y;                   \
        draw_list->_VtxWritePtr[N].uv    = uv;                  \
        draw_list->_VtxWritePtr[N].col   = C;                   \
    }

#define IM_POLYLINE_TRIANGLE(N, A, B, C)                        \
    {                                                           \
        draw_list->_IdxWritePtr[N * 3 + 0] = idx_base + A;      \
        draw_list->_IdxWritePtr[N * 3 + 1] = idx_base + B;      \
        draw_list->_IdxWritePtr[N * 3 + 2] = idx_base + C;      \
    }

    // Most dimensions are squares of the actual values, fit nicely with trigonometric identities
    const float half_thickness        = thickness * 0.5f;
    const float half_thickness_sqr    = half_thickness * half_thickness;

    const float clamped_miter_limit      = ImMax(0.0f, miter_limit);
    const float miter_distance_limit     = half_thickness * clamped_miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    const float miter_angle_limit = -0.9999619f; // cos(179.5)

    const float miter_clip_projection_tollerance = 0.001f; // 

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const auto uv        = draw_list->_Data->TexUvWhitePixel;
    const auto vtx_count = (count * 7 + 2);  // top 7 vertices per join, 2 vertices per butt cap
    const auto idx_count = (count * 9) * 3;  // top 9 triangles per join
    auto       idx_base  = draw_list->_VtxCurrentIdx;

    draw_list->PrimReserve(idx_count, vtx_count);

    const auto vtx_start = draw_list->_VtxWritePtr;
    const auto idx_start = draw_list->_IdxWritePtr;

    // Last two vertices in the vertex buffer are reserved to next segment to build upon
    // This is true for all segments.
    ImVec2 p0 =    data[closed ? count - 1 : 0];
    ImVec2 n0 = normals[closed ? count - 1 : 0];

    //
    // Butt cap
    //
    //   ~        /\       ~
    //   |                 |
    //   +--------x--------+
    //   0        p0       1
    // 
    // 4 vertices
    //
    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_thickness, p0.y - n0.y * half_thickness, color);
    IM_POLYLINE_VERTEX(1, p0.x + n0.x * half_thickness, p0.y + n0.y * half_thickness, color);

    draw_list->_VtxWritePtr += 2;

    int last_join_type = Butt;

    for (int i = closed ? 0 : 1; i < count; ++i)
    {
        const ImVec2 p1 =    data[i];
        const ImVec2 n1 = normals[i];

        // theta - angle between segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;
        const float sin_theta = n0.y * n1.x - n0.x * n1.y;

        // when angle converge to cos(180), join will be squared
        const bool  allow_miter = cos_theta > miter_angle_limit;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const ImVec2 n01                       = { n0.x + n1.x, n0.y + n1.y };

        const float  miter_scale_factor        = allow_miter ? half_thickness / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const ImVec2 miter_offset              = { n01.x * miter_scale_factor, n01.y * miter_scale_factor };
        const float  miter_distance_sqr        = allow_miter ? miter_offset.x * miter_offset.x + miter_offset.y * miter_offset.y : FLT_MAX; // avoid loosing FLT_MAX due to n01 being zero

        // always have to know if join miter is limited
        const bool   limit_miter               = (miter_distance_sqr > miter_distance_limit_sqr) || !allow_miter;

        // check for discontinuity, miter distance greater than segment lengths will mangle geometry
        // so we create disconnect and create overlapping geometry just to keep overall shape correct
        const float  segment_length_sqr        = segments_length_sqr[i];
        const float  next_segment_length_sqr   = segments_length_sqr[i + 1];
        const bool   disconnected              = (segment_length_sqr < miter_distance_sqr) || (next_segment_length_sqr < miter_distance_sqr) || (segment_length_sqr < half_thickness_sqr);
        const bool   is_continuation           = closed || !(i == count - 1);

        // select join type
        JoinType preferred_join_type = Butt;
        [[likely]] if (is_continuation)
        {
            preferred_join_type = limit_miter ? default_join_limit_type : default_join_type;

            // MiterClip need to be 'clamped' to Bevel if too short or to Miter if clipping is not necessary
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
        const JoinType join_type = disconnected ? (is_continuation ? ThickButt : Butt) : preferred_join_type;

        int new_vtx_count = 0;

        // Joins try to emit as little vertices as possible.
        // Last two vertices will be reused by next segment.
        [[likely]] if (join_type == Miter)
        {
            //
            // Miter join is skew to the left or right, order of vertices does
            // not change.
            // 
            //   0
            //   +   
            //   |'-_  
            //   |   '-_
            //   |      '-x
            //   |         '-_   
            //   |            '.
            //   |              '-_ 1
            //   |           ......+  
            //   |.....''''''      |
            //   + ~ ~ ~ ~ ~ ~ ~ ~ +
            //  -2                -1
            //
            // 4 vertices
            //

            IM_POLYLINE_VERTEX(0, p1.x - miter_offset.x, p1.y - miter_offset.y, color);
            IM_POLYLINE_VERTEX(1, p1.x + miter_offset.x, p1.y + miter_offset.y, color);

            new_vtx_count = 2;
        }
        else [[unlikely]] if (join_type == Butt)
        {
            //
            // Butt cap
            //
            //   ~        /\       ~
            //   |                 |
            //   +--------x--------+
            //   2        p1       3
            // 
            //   0        p0       1
            //   +--------x--------+
            //   |                 |
            //   ~        \/       ~
            //
            // 8 vertices
            //

            IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_thickness, p1.y - n0.y * half_thickness, color);
            IM_POLYLINE_VERTEX(1, p1.x + n0.x * half_thickness, p1.y + n0.y * half_thickness, color);

            IM_POLYLINE_VERTEX(2, p1.x - n1.x * half_thickness, p1.y - n1.y * half_thickness, color);
            IM_POLYLINE_VERTEX(3, p1.x + n1.x * half_thickness, p1.y + n1.y * half_thickness, color);

            new_vtx_count = 4;
        }
        else [[unlikely]] if (join_type == Bevel || join_type == MiterClip)
        {
            //
            // Left turn                            Right turn
            //                                      
            //                                                  
            //                                                 
            //                                                  
            //           2 +                                    + 2
            //            . \                                  / .
            //           .   \                                /   .
            //        < .     \                              /     . >
            //         .   x   \                            /   x   .
            //      1 .         \                          /         . 5
            //      .+...........+                        +...........+  
            //            /\    0                          0    /\           
            //                                                              
            //
            // 3 vertices
            //

            ImVec2 bevel_normal = ImVec2(n01.x, n01.y);
            IM_NORMALIZE2F_OVER_ZERO(bevel_normal.x, bevel_normal.y);

            const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

            ImVec2 right, left, miter;

            const float  clip_projection = n0.y * bevel_normal.x - n0.x * bevel_normal.y;
            const bool   allow_clip      = join_type == MiterClip ? ImAbs(clip_projection) >= miter_clip_projection_tollerance : false;

            [[likely]] if (!allow_clip)
            {
                right = { p1.x + sign * n0.x * half_thickness, p1.y + sign * n0.y * half_thickness };
                left  = { p1.x + sign * n1.x * half_thickness, p1.y + sign * n1.y * half_thickness };
                miter = { p1.x - sign *        miter_offset.x, p1.y - sign *        miter_offset.y };
            }
            else
            {
                const ImVec2 point_on_clip_line       = ImVec2(p1.x + sign * bevel_normal.x * miter_distance_limit, p1.y + sign * bevel_normal.y * miter_distance_limit);
                const ImVec2 point_on_bevel_edge      = ImVec2(p1.x + sign *           n0.x *       half_thickness, p1.y + sign *           n0.y *       half_thickness);
                const ImVec2 offset                   = ImVec2(point_on_clip_line.x - point_on_bevel_edge.x, point_on_clip_line.y - point_on_bevel_edge.y);
                const float  distance_to_intersection = (n0.x * offset.x + n0.y * offset.y) / clip_projection;
                const ImVec2 offset_to_intersection   = ImVec2(-distance_to_intersection * bevel_normal.y, distance_to_intersection * bevel_normal.x);

                right = { point_on_clip_line.x - offset_to_intersection.x, point_on_clip_line.y - offset_to_intersection.y };
                left  = { point_on_clip_line.x + offset_to_intersection.x, point_on_clip_line.y + offset_to_intersection.y };
                miter = { p1.x                 - sign *    miter_offset.x, p1.y                 - sign *    miter_offset.y };
            }

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(0, right.x, right.y, color);
                IM_POLYLINE_VERTEX(1, miter.x, miter.y, color);
                IM_POLYLINE_VERTEX(2,  left.x,  left.y, color);
            }
            else
            {
                IM_POLYLINE_VERTEX(0, right.x, right.y, color);
                IM_POLYLINE_VERTEX(1,  left.x,  left.y, color);
                IM_POLYLINE_VERTEX(2, miter.x, miter.y, color);
            }

            new_vtx_count = 3;
        }
        else [[unlikely]] if (join_type == ThickButt)
        {
            //   ~        /\   ~   ~
            //   |             |   |
            //   +--------x----+---+
            //   5                 6
            //
            //   2, 3, 4 - extra vertices to plug discontinuity by joins
            //
            //   0        p0       1
            //   +--------x--------+
            //   |                 |
            //   ~        \/       ~
            //
            // 7 vertices
            //

            IM_POLYLINE_VERTEX(0, p1.x - n0.x *        half_thickness, p1.y - n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(1, p1.x + n0.x *        half_thickness, p1.y + n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(2, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX(3, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX(4, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX(5, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(6, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, color);

            new_vtx_count = 7;

            // there are 2 vertices in the previous segment, to make indices less confusing
            // we shift base do 0 will be first vertex of ThickButt, later we undo that
            // and segments will be connected properly
            idx_base += 2;

            [[likely]] if (preferred_join_type == Miter)
            {
                //
                // Miter join between two discontinuous segments
                // 
                // Left turn                                     Right turn
                // 
                //             ~                      3      |      3                      ~               
                //           ~ ~ --+-------------------+     |     +-------------------+-- ~ ~             
                //             ~  6|                 .'|     |     |                 .'|5  ~               
                //             ~   |               .'  |     |     |               .'  |   ~               
                //             ~   |             .'    |     |     |             .'    |   ~               
                //             ~   |           .'      |     |     |           .'      |   ~               
                //       <     ~   |         .'        |     |     |         .'        |   ~     <         
                //     to next ~   |       .'          |     |     |       .'          |   ~ to next       
                //             ~   |     .'            |     |     |     .'            |   ~               
                //             ~   |   .'              |     |     |   .'              |   ~               
                //             ~   | .'                |     |     | .'                |   ~               
                //   +-------------+-------------------+     |     +-------------------+-------------+     
                //  0|         ~  2|                  1|     |     |0                  |2  ~         |1    
                // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   |   ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   
                //   |         ~   |                   |     |     |                   |   ~         |     
                //   ~         ~   |                   ~     |     ~                   |   ~         ~     
                //             ~   |        /\               |               \/        |   ~               
                //   overlap   ~   |   from previous         |         from previous   |   ~   overlap     
                //           ~ ~ --+                         |                         +-- ~ ~             
                //             ~  5                          |                          6  ~               
                //
                // 2 of 3 extra vertices allocated
                // 2 extra triangles (4 total per join)
                //

                const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

                IM_POLYLINE_VERTEX(2, p1.x,                         p1.y,                         color);
                IM_POLYLINE_VERTEX(3, p1.x + sign * miter_offset.x, p1.y + sign * miter_offset.y, color);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_TRIANGLE(0, 2, 1, 3);
                    IM_POLYLINE_TRIANGLE(1, 2, 3, 6);
                }
                else
                {
                    IM_POLYLINE_TRIANGLE(0, 0, 2, 5);
                    IM_POLYLINE_TRIANGLE(1, 0, 5, 3);
                }

                draw_list->_IdxWritePtr += 6;
            }
            else [[unlikely]] if (preferred_join_type == Bevel || preferred_join_type == MiterClip)
            {
                //
                // Bevel join between two discontinuous segments
                // 
                // Left turn                                     Right turn
                // 
                //             ~            4                |                4            ~               
                //           ~ ~ --+---------+               |               +---------+-- ~ ~             
                //             ~  6|        . '.             |             .' .        |5  ~               
                //             ~   |       .    '.           |           .'    .       |   ~               
                //             ~   |      .       '.         |         .'       .      |   ~               
                //             ~   |     .          '.       |       .'          .     |   ~               
                //       <     ~   |    .           . '+ 3   |   3 +' .           .    |   ~     <         
                //     to next ~   |   .        . '    |     |     |    ' .        .   |   ~ to next       
                //             ~   |  .     . '        |     |     |        ' .     .  |   ~               
                //             ~   | .  . '            |     |     |            ' .  . |   ~               
                //             ~   |. '                |     |     |                ' .|   ~               
                //   +-------------+-------------------+     |     +-------------------+-------------+     
                //  0|         ~  2|                  1|     |     |0                  |2  ~         |1    
                // ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   |   ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   
                //   |         ~   |                   |     |     |                   |   ~         |     
                //   ~         ~   |                   ~     |     ~                   |   ~         ~     
                //             ~   |        /\               |               \/        |   ~               
                //   overlap   ~   |   from previous         |         from previous   |   ~   overlap     
                //           ~ ~ --+                         |                         +-- ~ ~             
                //             ~  5                          |                          6  ~               
                //
                // 3 of 3 extra vertices allocated
                // 3 extra triangles (5 total per join)
                //

                const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

                ImVec2 bevel_normal = n01;
                IM_NORMALIZE2F_OVER_ZERO(bevel_normal.x, bevel_normal.y);

                IM_POLYLINE_VERTEX(4, p1.x, p1.y, color);

                [[likely]] if (preferred_join_type == Bevel)
                {
                    if (sin_theta < 0.0f)
                    {
                        // Left turn, for full bevel we skip collapsed triangles (same vertices: 3 == 0, 4 == 5)
                        IM_POLYLINE_TRIANGLE(0,  2, 1, 6);
                    }
                    else
                    {
                        // Right turn, for full bevel we skip collapsed triangles (same vertices: 5 == 1, 8 == 10)
                        IM_POLYLINE_TRIANGLE(0,  2, 5, 0);
                    }
                        
                    draw_list->_IdxWritePtr += 3;
                }
                else
                {
                    [[likely]] if (allow_miter)
                    {
                        // clip bevel to miter distance

                        const float  clip_projection          = n0.y * bevel_normal.x - n0.x * bevel_normal.y;
                        const ImVec2 point_on_clip_line       = ImVec2(p1.x + sign * bevel_normal.x * miter_distance_limit, p1.y + sign * bevel_normal.y * miter_distance_limit);
                        const ImVec2 point_on_bevel_edge      = ImVec2(p1.x + sign *           n0.x *       half_thickness, p1.y + sign *           n0.y *       half_thickness);
                        const ImVec2 offset                   = ImVec2(point_on_clip_line.x - point_on_bevel_edge.x, point_on_clip_line.y - point_on_bevel_edge.y);
                        const float  distance_to_intersection = (n0.x * offset.x + n0.y * offset.y) / clip_projection;
                        const ImVec2 offset_to_intersection   = ImVec2(-distance_to_intersection * bevel_normal.y, distance_to_intersection * bevel_normal.x);

                        IM_POLYLINE_VERTEX(3, point_on_clip_line.x - offset_to_intersection.x, point_on_clip_line.y - offset_to_intersection.y, color);
                        IM_POLYLINE_VERTEX(4, point_on_clip_line.x + offset_to_intersection.x, point_on_clip_line.y + offset_to_intersection.y, color);
                    }
                    else
                    {
                        // handle 180 degrees turn, bevel is square

                        const ImVec2 normal    = ImVec2(n1.x, n1.y);
                        const ImVec2 direction = ImVec2(normal.y, -normal.x);

                        IM_POLYLINE_VERTEX(3, p1.x + direction.x * miter_distance_limit - sign * normal.x * half_thickness, p1.y + direction.y * miter_distance_limit - sign * normal.y * half_thickness, color);
                        IM_POLYLINE_VERTEX(4, p1.x + direction.x * miter_distance_limit + sign * normal.x * half_thickness, p1.y + direction.y * miter_distance_limit + sign * normal.y * half_thickness, color);
                    }

                    if (sin_theta < 0.0f)
                    {
                        IM_POLYLINE_TRIANGLE(0, 2, 1, 3);
                        IM_POLYLINE_TRIANGLE(1, 2, 3, 4);
                        IM_POLYLINE_TRIANGLE(2, 2, 4, 6);
                    }
                    else
                    {
                        IM_POLYLINE_TRIANGLE(0, 2, 5, 4);
                        IM_POLYLINE_TRIANGLE(1, 2, 4, 3);
                        IM_POLYLINE_TRIANGLE(2, 2, 3, 0);
                    }

                    draw_list->_IdxWritePtr += 9;
                }
            }

            // Restore index base (see commend next to 'idx_base += 2' above)
            idx_base -= 2;
        }

        [[unlikely]] if (join_type == Bevel || join_type == MiterClip)
        {
                // Left turn                         Right turn
                //
                //                  4              |              3
                //                   +             |             +
                //                 .' \            |            / '.
                //               .'    \           |           /    '.
                //             .'       \          |          /       '.
                //           .'          \         |         /          '.
                //         .'             \        |        /             '.
                //       .'                \       |       /                '.
                //   3 .'         ..........+ 2    |    2 +..........         '. 4
                //    +.....''''''   ....'''|      |      |'''....   ''''''.....+
                //    |       ....'''       |      |      |       '''....       |
                //    |....'''              |      |      |              '''....|
                //    + ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ +      |      + ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ +
                //    0                     1      |      0                     1
                //
                // 3 triangles
                //

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_TRIANGLE( 0, 0, 1, 2);
                IM_POLYLINE_TRIANGLE( 1, 0, 2, 3);
                IM_POLYLINE_TRIANGLE( 2, 3, 2, 4);
            }
            else
            {
                IM_POLYLINE_TRIANGLE( 0, 0, 1, 2);
                IM_POLYLINE_TRIANGLE( 1, 1, 4, 2);
                IM_POLYLINE_TRIANGLE( 2, 4, 3, 2);

            }

            draw_list->_IdxWritePtr += 9;
        }
        else
        {
            //
            //   2                 3
            //   +--------x--------+
            //   |                .|
            //   |              .' |
            //   |            .'   |
            //   |          .'     |
            //   |        .'       |
            //   |      .'         |
            //   |    .'           |
            //   |  .'             |
            //   |.'               |
            //   +-----------------+
            //   0                 1
            //
            // 2 triangles
            //
            IM_POLYLINE_TRIANGLE(0, 0, 1, 3);
            IM_POLYLINE_TRIANGLE(1, 0, 3, 2);

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

        [[unlikely]] if (cap == ImDrawFlags_CapSquare)
        {
            const ImVec2 n0 = normals[0];
            const ImVec2 n1 = normals[count - 1];

            vtx_start[0].pos.x += n0.y * half_thickness;
            vtx_start[0].pos.y -= n0.x * half_thickness;
            vtx_start[1].pos.x += n0.y * half_thickness;
            vtx_start[1].pos.y -= n0.x * half_thickness;

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

    IM_ASSERT(unused_idx_count >= 0);
    IM_ASSERT(unused_vtx_count >= 0);

    draw_list->PrimUnreserve(unused_idx_count, unused_vtx_count);

    draw_list->_VtxCurrentIdx = idx_base;

#if 0
    [[unlikely]] if (!closed && cap == ImDrawFlags_CapRound)
    {
        const ImVec2 p0 = data[0];
        const ImVec2 p1 = data[count - 1];
        const ImVec2 n0 = normals[0];
        const ImVec2 n1 = normals[count - 1];

        const auto begin_angle = ImAtan2(n0.y, n0.x);
        const auto   end_angle = ImAtan2(n1.y, n1.x);

        const auto half_thickness_to_half_fringe_thickness = half_fringe_thickness / half_thickness;

        draw_list->PathClear();
        draw_list->PathArcTo({ 0, 0 }, half_thickness, begin_angle - IM_PI, begin_angle);
        const int begin_point_count = draw_list->_Path.Size;
        //draw_list->PathArcTo({ 0, 0 }, half_thickness, end_angle, end_angle + IM_PI);
        //const int end_point_count = draw_list->_Path.Size - begin_point_count;

        const int vtx_count = (begin_point_count /*+ end_point_count*/) * 2;
        const int idx_count = (begin_point_count /*+ end_point_count */- 1) * 9;

        draw_list->PrimReserve(idx_count, vtx_count);

        const ImVec2* arc_data = draw_list->_Path.Data;
        for (int i = 0; i < begin_point_count; i++)
        {
            const ImVec2 point = *arc_data++;

            IM_POLYLINE_VERTEX(0, p0.x + point.x,                                           p0.y + point.y,                                           color);
            IM_POLYLINE_VERTEX(1, p0.x + point.x * half_thickness_to_half_fringe_thickness, p0.y + point.y * half_thickness_to_half_fringe_thickness, color_border);

            [[likely]] if (i < begin_point_count - 1)
            {
                IM_POLYLINE_TRIANGLE(0, 0, 2, 1);
                IM_POLYLINE_TRIANGLE(1, 1, 2, 3);
                draw_list->_IdxWritePtr[6] = idx_base;
                draw_list->_IdxWritePtr[7] = draw_list->_VtxCurrentIdx + begin_point_count * 2 - 2;
                draw_list->_IdxWritePtr[8] = idx_base + 2;

                draw_list->_IdxWritePtr += 9;

            }
            
            draw_list->_VtxWritePtr += 2;
            idx_base += 2;
        }

        draw_list->_VtxCurrentIdx = idx_base;
    }
#endif

#undef IM_POLYLINE_VERTEX
#undef IM_POLYLINE_TRIANGLE
}

static void ImDrawList_Polyline_AA(ImDrawList* draw_list, const ImVec2* data, const int count, ImU32 color_, const ImDrawFlags draw_flags, float thickness, float miter_limit)
{
    [[unlikely]] if (count < 2 || thickness <= 0.0f || !(color_ && IM_COL32_A_MASK))
        return;

    float fringe_width = draw_list->_FringeScale;

    // For thin line fade transparency and shrink fringe
    thickness -= fringe_width;
    [[unlikely]] if (thickness < 0.0f)
    {
        const float opacity = 1.0f - ImClamp(-thickness / fringe_width, 0.0f, 1.0f);

        ImU32 alpha = (ImU32)(((color_ >> IM_COL32_A_SHIFT) & 0xFF) * opacity);
        [[unlikely]] if (alpha == 0)
            return;

        color_ = (color_ & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);

        fringe_width += thickness;
        thickness = 0.0f;
    }

    const float fringe_thickness = thickness + fringe_width * 2.0f;

    const ImU32 color        = color_;
    const ImU32 color_border = color_ & ~IM_COL32_A_MASK;

    // Internal join types:
    //   - Miter: default join type, sharp point
    //   - MiterClip: clipped miter join, basically a bevel join with a limited length
    //   - Bevel: straight bevel join between segments
    //   - Butt: flat cap
    //   - ThickButt: flat cap with extra vertices, used for discontinuity between segments
    enum JoinType { Miter, MiterClip, Bevel, Butt, ThickButt };

    const bool        closed         = !!(draw_flags & ImDrawFlags_Closed) && (count > 2);
    const ImDrawFlags join_flags     = draw_flags & ImDrawFlags_JoinMask_;
    const ImDrawFlags cap_flags      = draw_flags & ImDrawFlags_CapMask_;
    const ImDrawFlags join           = join_flags ? join_flags : ImDrawFlags_JoinDefault_;
    const ImDrawFlags cap            = cap_flags ? cap_flags : ImDrawFlags_CapDefault_;

    // Pick default join type based on join flags
    const JoinType default_join_type       = join == ImDrawFlags_JoinBevel ? Bevel : (join == ImDrawFlags_JoinMiterClip ? MiterClip : Miter);
    const JoinType default_join_limit_type = join == ImDrawFlags_JoinMiterClip ? MiterClip : Bevel;

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

#define IM_POLYLINE_VERTEX(N, X, Y, C)                          \
    {                                                           \
        draw_list->_VtxWritePtr[N].pos.x = X;                   \
        draw_list->_VtxWritePtr[N].pos.y = Y;                   \
        draw_list->_VtxWritePtr[N].uv    = uv;                  \
        draw_list->_VtxWritePtr[N].col   = C;                   \
    }

#define IM_POLYLINE_TRIANGLE(N, A, B, C)                        \
    {                                                           \
        draw_list->_IdxWritePtr[N * 3 + 0] = idx_base + A;      \
        draw_list->_IdxWritePtr[N * 3 + 1] = idx_base + B;      \
        draw_list->_IdxWritePtr[N * 3 + 2] = idx_base + C;      \
    }

    // Most dimensions are squares of the actual values, fit nicely with trigonometric identities
    const float half_thickness        = thickness * 0.5f;
    const float half_thickness_sqr    = half_thickness * half_thickness;
    const float half_fringe_thickness = fringe_thickness * 0.5f;

    const float clamped_miter_limit      = ImMax(0.0f, miter_limit);
    const float miter_distance_limit     = half_thickness * clamped_miter_limit;
    const float miter_distance_limit_sqr = miter_distance_limit * miter_distance_limit;

    const float miter_angle_limit = -0.9999619f; // cos(179.5)

    const float miter_clip_projection_tollerance = 0.001f; // 

    // Reserve vertices and indices for worst case scenario
    // Unused vertices and indices will be released after the loop
    const auto uv        = draw_list->_Data->TexUvWhitePixel;
    const auto vtx_count = (count * 13 + 4);     // top 13 vertices per join, 4 vertices per butt cap
    const auto idx_count = (count * 15 + 4) * 3; // top 15 triangles per join, 4 triangles for square cap
    auto       idx_base  = draw_list->_VtxCurrentIdx;

    draw_list->PrimReserve(idx_count, vtx_count);

    const auto vtx_start = draw_list->_VtxWritePtr;
    const auto idx_start = draw_list->_IdxWritePtr;

    // Last two vertices in the vertex buffer are reserved to next segment to build upon
    // This is true for all segments.
    ImVec2 p0 =    data[closed ? count - 1 : 0];
    ImVec2 n0 = normals[closed ? count - 1 : 0];

    //
    // Butt cap
    //
    //   ~   ~    /\   ~   ~
    //   |   |         |   |
    //   +---+----x----+---+
    //   0   1    p0   2   3
    // 
    // 4 vertices
    //
    IM_POLYLINE_VERTEX(0, p0.x - n0.x * half_fringe_thickness,  p0.y - n0.y * half_fringe_thickness, color_border);
    IM_POLYLINE_VERTEX(1, p0.x - n0.x *        half_thickness,  p0.y - n0.y *        half_thickness, color);
    IM_POLYLINE_VERTEX(2, p0.x + n0.x *        half_thickness,  p0.y + n0.y *        half_thickness, color);
    IM_POLYLINE_VERTEX(3, p0.x + n0.x * half_fringe_thickness,  p0.y + n0.y * half_fringe_thickness, color_border);

    draw_list->_VtxWritePtr += 4;

    int last_join_type = Butt;

    for (int i = closed ? 0 : 1; i < count; ++i)
    {
        const ImVec2 p1 =    data[i];
        const ImVec2 n1 = normals[i];

        // theta - angle between segments
        const float cos_theta = n0.x * n1.x + n0.y * n1.y;
        const float sin_theta = n0.y * n1.x - n0.x * n1.y;

        // when angle converge to cos(180), join will be squared
        const bool  allow_miter = cos_theta > miter_angle_limit;

        // miter offset formula is derived here: https://www.angusj.com/clipper2/Docs/Trigonometry.htm
        const ImVec2 n01                       = { n0.x + n1.x, n0.y + n1.y };

        const float  miter_scale_factor        = allow_miter ? half_thickness / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const ImVec2 miter_offset              = { n01.x * miter_scale_factor, n01.y * miter_scale_factor };
        const float  miter_distance_sqr        = allow_miter ? miter_offset.x * miter_offset.x + miter_offset.y * miter_offset.y : FLT_MAX; // avoid loosing FLT_MAX due to n01 being zero

        const float  fringe_miter_scale_factor = allow_miter ? half_fringe_thickness / (1.0f + cos_theta) : FLT_MAX; // avoid division by zero
        const ImVec2 fringe_miter_offset       = { n01.x * fringe_miter_scale_factor, n01.y * fringe_miter_scale_factor };
        const float  fringe_miter_distance_sqr = allow_miter ? fringe_miter_offset.x * fringe_miter_offset.x + fringe_miter_offset.y * fringe_miter_offset.y : FLT_MAX; // avoid loosing FLT_MAX due to n01 being zero

        // always have to know if join miter is limited
        const bool   limit_miter               = (miter_distance_sqr > miter_distance_limit_sqr) || !allow_miter;

        // check for discontinuity, miter distance greater than segment lengths will mangle geometry
        // so we create disconnect and create overlapping geometry just to keep overall shape correct
        const float  segment_length_sqr        = segments_length_sqr[i];
        const float  next_segment_length_sqr   = segments_length_sqr[i + 1];
        const bool   disconnected              = (segment_length_sqr < fringe_miter_distance_sqr) || (next_segment_length_sqr < fringe_miter_distance_sqr) || (segment_length_sqr < half_thickness_sqr);
        const bool   is_continuation           = closed || !(i == count - 1);

        // select join type
        JoinType preferred_join_type = Butt;
        [[likely]] if (is_continuation)
        {
            preferred_join_type = limit_miter ? default_join_limit_type : default_join_type;

            // MiterClip need to be 'clamped' to Bevel if too short or to Miter if clipping is not necessary
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
        const JoinType join_type = disconnected ? (is_continuation ? ThickButt : Butt) : preferred_join_type;

        int new_vtx_count = 0;

        // Joins try to emit as little vertices as possible.
        // Last two vertices will be reused by next segment.
        [[likely]] if (join_type == Miter)
        {
            //
            // Miter join is skew to the left or right, order of vertices does
            // not change.
            // 
            //   0
            //   +   
            //   |'-_ 1
            //   |  .+-_
            //   |  .|  '-x
            //   | . |     '-_  2
            //   | . |        .+
            //   | . |      .  |'-_ 3
            //   |.  |    .    |  .+  
            //   |.  |  .      | . |
            //   +~ ~+~ ~ ~ ~ ~+ ~ +
            //  -4  -3        -2  -1
            //
            // 4 vertices
            //

            IM_POLYLINE_VERTEX(0, p1.x - fringe_miter_offset.x, p1.y - fringe_miter_offset.y, color_border);
            IM_POLYLINE_VERTEX(1, p1.x -        miter_offset.x, p1.y -        miter_offset.y, color);
            IM_POLYLINE_VERTEX(2, p1.x +        miter_offset.x, p1.y +        miter_offset.y, color);
            IM_POLYLINE_VERTEX(3, p1.x + fringe_miter_offset.x, p1.y + fringe_miter_offset.y, color_border);

            new_vtx_count = 4;
        }
        else [[unlikely]] if (join_type == Butt)
        {
            //
            // Butt cap
            //
            //   ~   ~    /\   ~   ~
            //   |   |         |   |
            //   +---+----x----+---+
            //   3   4    p1   5   6
            // 
            //   0   1    p0   2   3
            //   +---+----x----+---+
            //   |   |         |   |
            //   ~   ~    \/   ~   ~
            //
            // 8 vertices
            //

            IM_POLYLINE_VERTEX(0, p1.x - n0.x * half_fringe_thickness, p1.y - n0.y * half_fringe_thickness, color_border);
            IM_POLYLINE_VERTEX(1, p1.x - n0.x *        half_thickness, p1.y - n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(2, p1.x + n0.x *        half_thickness, p1.y + n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(3, p1.x + n0.x * half_fringe_thickness, p1.y + n0.y * half_fringe_thickness, color_border);

            IM_POLYLINE_VERTEX(4, p1.x - n1.x * half_fringe_thickness, p1.y - n1.y * half_fringe_thickness, color_border);
            IM_POLYLINE_VERTEX(5, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(6, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(7, p1.x + n1.x * half_fringe_thickness, p1.y + n1.y * half_fringe_thickness, color_border);

            new_vtx_count = 8;
        }
        else [[unlikely]] if (join_type == Bevel || join_type == MiterClip)
        {
            ImVec2 bevel_normal = ImVec2(n01.x, n01.y);
            IM_NORMALIZE2F_OVER_ZERO(bevel_normal.x, bevel_normal.y);

            const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

            ImVec2 dir_0 = { sign * (n0.x + bevel_normal.x) * 0.5f, sign * (n0.y + bevel_normal.y) * 0.5f };
            ImVec2 dir_1 = { sign * (n1.x + bevel_normal.x) * 0.5f, sign * (n1.y + bevel_normal.y) * 0.5f };
            IM_FIXNORMAL2F(dir_0.x, dir_0.y);
            IM_FIXNORMAL2F(dir_1.x, dir_1.y);


            //
            // Left turn                            Right turn
            //                                      
            //              5                                  2
            //               +                                +
            //              /.\                              / \
            //           4 +  .\                            /  .+ 3
            //            / \ . \                          /  ./ \
            //           /   \ . \                        / . /   \
            //          /     \ . \                      / . /     \
            //         /   x   \.  \                    /.  /   x   \
            //      3 /         \.  \                  /.  /         \ 4
            //      .+-----------+_  \                /. _+-----------+.
            // 2  .'            0  ''-+              +-''  1            '.  5
            //  +'                    1              0                    '+
            //
            // 6 vertices
            //

            ImVec2 right_inner, right_outer, left_inner, left_outer, miter_inner, miter_outer;

            const float  clip_projection = n0.y * bevel_normal.x - n0.x * bevel_normal.y;
            const bool   allow_clip      = join_type == MiterClip ? ImAbs(clip_projection) >= miter_clip_projection_tollerance : false;

            [[likely]] if (!allow_clip)
            {
                right_inner = { p1.x + sign * n0.x * half_thickness,                          p1.y + sign * n0.y * half_thickness                          };
                right_outer = { p1.x + sign * n0.x * half_thickness + dir_0.x * fringe_width, p1.y + sign * n0.y * half_thickness + dir_0.y * fringe_width };
                left_inner  = { p1.x + sign * n1.x * half_thickness,                          p1.y + sign * n1.y * half_thickness                          };
                left_outer  = { p1.x + sign * n1.x * half_thickness + dir_1.x * fringe_width, p1.y + sign * n1.y * half_thickness + dir_1.y * fringe_width };
                miter_inner = { p1.x - sign *        miter_offset.x,                          p1.y - sign *        miter_offset.y                          };
                miter_outer = { p1.x - sign * fringe_miter_offset.x,                          p1.y - sign * fringe_miter_offset.y                          };
            }
            else
            {
                const ImVec2 point_on_clip_line       = ImVec2(p1.x + sign * bevel_normal.x * miter_distance_limit, p1.y + sign * bevel_normal.y * miter_distance_limit);
                const ImVec2 point_on_bevel_edge      = ImVec2(p1.x + sign *           n0.x *       half_thickness, p1.y + sign *           n0.y *       half_thickness);
                const ImVec2 offset                   = ImVec2(point_on_clip_line.x - point_on_bevel_edge.x, point_on_clip_line.y - point_on_bevel_edge.y);
                const float  distance_to_intersection = (n0.x * offset.x + n0.y * offset.y) / clip_projection;
                const ImVec2 offset_to_intersection   = ImVec2(-distance_to_intersection * bevel_normal.y, distance_to_intersection * bevel_normal.x);

                right_inner = { point_on_clip_line.x                          - offset_to_intersection.x, point_on_clip_line.y                          - offset_to_intersection.y };
                right_outer = { point_on_clip_line.x + dir_0.x * fringe_width - offset_to_intersection.x, point_on_clip_line.y + dir_0.y * fringe_width - offset_to_intersection.y };
                left_inner  = { point_on_clip_line.x                          + offset_to_intersection.x, point_on_clip_line.y                          + offset_to_intersection.y };
                left_outer  = { point_on_clip_line.x + dir_1.x * fringe_width + offset_to_intersection.x, point_on_clip_line.y + dir_1.y * fringe_width + offset_to_intersection.y };
                miter_inner = { p1.x                 - sign    *                          miter_offset.x, p1.y                 - sign    *                          miter_offset.y };
                miter_outer = { p1.x                 - sign    *                   fringe_miter_offset.x, p1.y                 - sign    *                   fringe_miter_offset.y };
            }

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_VERTEX(0, right_inner.x, right_inner.y, color);
                IM_POLYLINE_VERTEX(1, right_outer.x, right_outer.y, color_border);
                IM_POLYLINE_VERTEX(2, miter_outer.x, miter_outer.y, color_border);
                IM_POLYLINE_VERTEX(3, miter_inner.x, miter_inner.y, color);
                IM_POLYLINE_VERTEX(4,  left_inner.x,  left_inner.y, color);
                IM_POLYLINE_VERTEX(5,  left_outer.x,  left_outer.y, color_border);
            }
            else
            {
                IM_POLYLINE_VERTEX(0, right_outer.x, right_outer.y, color_border);
                IM_POLYLINE_VERTEX(1, right_inner.x, right_inner.y, color);
                IM_POLYLINE_VERTEX(2,  left_outer.x,  left_outer.y, color_border);
                IM_POLYLINE_VERTEX(3,  left_inner.x,  left_inner.y, color);
                IM_POLYLINE_VERTEX(4, miter_inner.x, miter_inner.y, color);
                IM_POLYLINE_VERTEX(5, miter_outer.x, miter_outer.y, color_border);
            }

            new_vtx_count = 6;
        }
        else [[unlikely]] if (join_type == ThickButt)
        {
            //   ~   ~    /\   ~   ~
            //   |   |         |   |
            //   +---+----x----+---+
            //   9  10    p1  11  12
            //
            //   4, 5, 6, 7, 8 - extra vertices to plug discontinuity by joins
            //
            //   0   1    p0   2   3
            //   +---+----x----+---+
            //   |   |         |   |
            //   ~   ~    \/   ~   ~
            //
            // 13 vertices
            //

            IM_POLYLINE_VERTEX( 0, p1.x - n0.x * half_fringe_thickness, p1.y - n0.y * half_fringe_thickness, color_border);
            IM_POLYLINE_VERTEX( 1, p1.x - n0.x *        half_thickness, p1.y - n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX( 2, p1.x + n0.x *        half_thickness, p1.y + n0.y *        half_thickness, color);
            IM_POLYLINE_VERTEX( 3, p1.x + n0.x * half_fringe_thickness, p1.y + n0.y * half_fringe_thickness, color_border);
            IM_POLYLINE_VERTEX( 4, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX( 5, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX( 6, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX( 7, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX( 8, p1.x,                                p1.y,                                color);
            IM_POLYLINE_VERTEX( 9, p1.x - n1.x * half_fringe_thickness, p1.y - n1.y * half_fringe_thickness, color_border);
            IM_POLYLINE_VERTEX(10, p1.x - n1.x *        half_thickness, p1.y - n1.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(11, p1.x + n1.x *        half_thickness, p1.y + n1.y *        half_thickness, color);
            IM_POLYLINE_VERTEX(12, p1.x + n1.x * half_fringe_thickness, p1.y + n1.y * half_fringe_thickness, color_border);

            new_vtx_count = 13;

            // there are 4 vertices in the previous segment, to make indices less confusing
            // we shift base do 0 will be first vertex of ThickButt, later we undo that
            // and segments will be connected properly
            idx_base += 4;

            [[likely]] if (preferred_join_type == Miter)
            {
                //
                // Miter join between two discontinuous segments
                // 
                // Left turn                                     Right turn
                // 
                //             ~                      6      |      6                      ~               
                //           ~ ~ --+-------------------+     |     +-------------------+-- ~ ~             
                //             ~ 12|'''....          .'|     |     |'.'''''....        |9  ~               
                //             ~   |       ''''... .' '|     |     |  '.  5    ''''....|   ~               
                //           ~ ~ --+-------------+ 5 ' |     |     |    '+-------------+-- ~ ~             
                //             ~ 11|           .'|   ' |     |     |    '|           .'|10 ~               
                //       <     ~   |         .'  |  '  |     |     |   ' |         .'  |   ~     <         
                //     to next ~   |       .'    |  '  |     |     |  '  |       .'    |   ~ to next       
                //             ~   |     .'      | '   |     |     |  '  |     .'      |   ~               
                //             ~   |   .'        | '   |     |     | '   |   .'        |   ~               
                //             ~   | .'          |'    |     |     |'    | .'          |   ~               
                //   +-----+-------+-------------+-----+     |     +-----+-------------+-------+-----+     
                //  0|    1|   ~  4|            2|    3|     |     |0    |1            |4  ~   |2    |3    
                // ~ ~ ~~~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   |   ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~~~ ~ ~   
                //   |     |   ~   |             |     |     |     |     |             |   ~   |     |     
                //   ~     ~ ~ ~ --+             ~     ~     |     ~     ~             +-- ~ ~ ~     ~     
                //             ~ 10|        /\               |               \/        |11 ~               
                //   overlap   ~   |   from previous         |         from previous   |   ~   overlap     
                //           ~ ~ --+                         |                         +-- ~ ~             
                //             ~  9                          |                          12 ~               
                //
                // 3 of 5 extra vertices allocated
                // 6 extra triangles (12 total per join)
                //

                const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

                IM_POLYLINE_VERTEX(4, p1.x,                                p1.y,                                color);
                IM_POLYLINE_VERTEX(5, p1.x + sign *        miter_offset.x, p1.y + sign *        miter_offset.y, color);
                IM_POLYLINE_VERTEX(6, p1.x + sign * fringe_miter_offset.x, p1.y + sign * fringe_miter_offset.y, color_border);

                if (sin_theta < 0.0f)
                {
                    IM_POLYLINE_TRIANGLE(0,  4,  2,  5);
                    IM_POLYLINE_TRIANGLE(1,  4,  5, 11);
                    IM_POLYLINE_TRIANGLE(2,  2,  6,  5);
                    IM_POLYLINE_TRIANGLE(3,  2,  3,  6);
                    IM_POLYLINE_TRIANGLE(4,  5, 12, 11);
                    IM_POLYLINE_TRIANGLE(5,  5,  6, 12);
                }
                else
                {
                    IM_POLYLINE_TRIANGLE(0,  0,  5,  6);
                    IM_POLYLINE_TRIANGLE(1,  0,  1,  5);
                    IM_POLYLINE_TRIANGLE(2,  1, 10,  5);
                    IM_POLYLINE_TRIANGLE(3,  1,  4, 10);
                    IM_POLYLINE_TRIANGLE(4, 10,  6,  5);
                    IM_POLYLINE_TRIANGLE(5, 10,  9,  6);
                }

                draw_list->_IdxWritePtr += 18;
            }
            else [[unlikely]] if (preferred_join_type == Bevel || preferred_join_type == MiterClip)
            {
                //
                // Bevel join between two discontinuous segments
                // 
                // Left turn                                     Right turn
                // 
                //             ~            7                |                7            ~               
                //           ~ ~ --+---------+               |               +---------+-- ~ ~             
                //             ~ 12|''..     :'.             |             .':''...    |9  ~               
                //             ~   |    ''..: . '.           |           .' 8 :    ''..|   ~               
                //           ~ ~ --+--------+8 .  '.         |         .'  .. +--------+-- ~ ~             
                //             ~ 11|       ' '. .   '.       |       .'..'' .' '       |10 ~               
                //       <     ~   |      '    '.. ...'+ 6   |   6 +'...  .'    '      |   ~     <         
                //     to next ~   |    .'    ...+'  .'|     |     |    '+...    '.    |   ~ to next       
                //             ~   |   '  ..''  5|  .' |     |     |   .'|5  ''..  '   |   ~               
                //             ~   |  '.''       | .'  |     |     | .'  |       ''.'  |   ~               
                //             ~   |.''          |.'   |     |     |.'   |          ''.|   ~               
                //   +-----+-------+-------------+-----+     |     +-----+-------------+-------+-----+     
                //  0|    1|   ~  4|            2|    3|     |     |0    |1            |4  ~   |2    |3    
                // ~ ~ ~~~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~   |   ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~~~ ~ ~   
                //   |     |   ~   |             |     |     |     |     |             |   ~   |     |     
                //   ~     ~ ~ ~ --+             ~     ~     |     ~     ~             +-- ~ ~ ~     ~     
                //             ~ 10|        /\               |               \/        |11 ~               
                //   overlap   ~   |   from previous         |         from previous   |   ~   overlap     
                //           ~ ~ --+                         |                         +-- ~ ~             
                //             ~  9                          |                          12 ~               
                //
                // 5 of 5 extra vertices allocated
                // 9 extra triangles (15 total per join)
                //

                const float sign = sin_theta < 0.0f ? 1.0f : -1.0f;

                ImVec2 bevel_normal = n01;
                IM_NORMALIZE2F_OVER_ZERO(bevel_normal.x, bevel_normal.y);

                ImVec2 dir_0 = { sign * (n0.x + bevel_normal.x) * 0.5f, sign * (n0.y + bevel_normal.y) * 0.5f };
                ImVec2 dir_1 = { sign * (n1.x + bevel_normal.x) * 0.5f, sign * (n1.y + bevel_normal.y) * 0.5f };
                IM_FIXNORMAL2F(dir_0.x, dir_0.y);
                IM_FIXNORMAL2F(dir_1.x, dir_1.y);

                IM_POLYLINE_VERTEX(4, p1.x, p1.y, color);

                [[likely]] if (preferred_join_type == Bevel)
                {
                    // 5 and 8 vertices are already present
                    IM_POLYLINE_VERTEX(6, p1.x + sign * n0.x * half_thickness + dir_0.x * fringe_width, p1.y + sign * n0.y * half_thickness + dir_0.y * fringe_width, color_border);
                    IM_POLYLINE_VERTEX(7, p1.x + sign * n1.x * half_thickness + dir_1.x * fringe_width, p1.y + sign * n1.y * half_thickness + dir_1.y * fringe_width, color_border);

                    if (sin_theta < 0.0f)
                    {
                        // Left turn, for full bevel we skip collapsed triangles (same vertices: 5 == 2, 8 == 11)

                        IM_POLYLINE_TRIANGLE(0,  4, 2, 11);
                        IM_POLYLINE_TRIANGLE(1,  2, 3,  6);
                        IM_POLYLINE_TRIANGLE(2, 11, 7, 12);
                        IM_POLYLINE_TRIANGLE(3,  2, 6,  7);
                        IM_POLYLINE_TRIANGLE(4,  2, 7, 11);

                    }
                    else
                    {
                        // Right turn, for full bevel we skip collapsed triangles (same vertices: 5 == 1, 8 == 10)

                        IM_POLYLINE_TRIANGLE(0,  4, 10, 1);
                        IM_POLYLINE_TRIANGLE(1,  0,  1, 6);
                        IM_POLYLINE_TRIANGLE(2, 10,  9, 7);
                        IM_POLYLINE_TRIANGLE(3, 10,  7, 6);
                        IM_POLYLINE_TRIANGLE(4, 10,  6, 1);
                    }
                        
                    draw_list->_IdxWritePtr += 15;
                }
                else
                {
                    [[likely]] if (allow_miter)
                    {
                        // clip bevel to miter distance

                        const float  clip_projection          = n0.y * bevel_normal.x - n0.x * bevel_normal.y;
                        const ImVec2 point_on_clip_line       = ImVec2(p1.x + sign * bevel_normal.x * miter_distance_limit, p1.y + sign * bevel_normal.y * miter_distance_limit);
                        const ImVec2 point_on_bevel_edge      = ImVec2(p1.x + sign *           n0.x *       half_thickness, p1.y + sign *           n0.y *       half_thickness);
                        const ImVec2 offset                   = ImVec2(point_on_clip_line.x - point_on_bevel_edge.x, point_on_clip_line.y - point_on_bevel_edge.y);
                        const float  distance_to_intersection = (n0.x * offset.x + n0.y * offset.y) / clip_projection;
                        const ImVec2 offset_to_intersection   = ImVec2(-distance_to_intersection * bevel_normal.y, distance_to_intersection * bevel_normal.x);

                        IM_POLYLINE_VERTEX(5, point_on_clip_line.x                          - offset_to_intersection.x, point_on_clip_line.y                          - offset_to_intersection.y, color);
                        IM_POLYLINE_VERTEX(6, point_on_clip_line.x + dir_0.x * fringe_width - offset_to_intersection.x, point_on_clip_line.y + dir_0.y * fringe_width - offset_to_intersection.y, color_border);
                        IM_POLYLINE_VERTEX(7, point_on_clip_line.x + dir_1.x * fringe_width + offset_to_intersection.x, point_on_clip_line.y + dir_1.y * fringe_width + offset_to_intersection.y, color_border);
                        IM_POLYLINE_VERTEX(8, point_on_clip_line.x                          + offset_to_intersection.x, point_on_clip_line.y                          + offset_to_intersection.y, color);
                    }
                    else
                    {
                        // handle 180 degrees turn, bevel is square

                        const ImVec2 normal    = ImVec2(n1.x, n1.y);
                        const ImVec2 direction = ImVec2(normal.y, -normal.x);

                        const float fringe_distance = miter_distance_limit + fringe_width;

                        IM_POLYLINE_VERTEX(5, p1.x + direction.x * miter_distance_limit - sign * normal.x *        half_thickness, p1.y + direction.y * miter_distance_limit - sign * normal.y *        half_thickness, color);
                        IM_POLYLINE_VERTEX(6, p1.x + direction.x *      fringe_distance - sign * normal.x * half_fringe_thickness, p1.y + direction.y *      fringe_distance - sign * normal.y * half_fringe_thickness, color_border);
                        IM_POLYLINE_VERTEX(7, p1.x + direction.x *      fringe_distance + sign * normal.x * half_fringe_thickness, p1.y + direction.y *      fringe_distance + sign * normal.y * half_fringe_thickness, color_border);
                        IM_POLYLINE_VERTEX(8, p1.x + direction.x * miter_distance_limit + sign * normal.x *        half_thickness, p1.y + direction.y * miter_distance_limit + sign * normal.y *        half_thickness, color);
                    }

                    if (sin_theta < 0.0f)
                    {
                        IM_POLYLINE_TRIANGLE(0,  4, 2,  5);
                        IM_POLYLINE_TRIANGLE(1,  4, 5,  8);
                        IM_POLYLINE_TRIANGLE(2,  4, 8, 11);
                        IM_POLYLINE_TRIANGLE(3,  2, 6,  5);
                        IM_POLYLINE_TRIANGLE(4,  2, 3,  6);
                        IM_POLYLINE_TRIANGLE(5, 11, 8, 12);
                        IM_POLYLINE_TRIANGLE(6,  8, 7, 12);
                        IM_POLYLINE_TRIANGLE(7,  5, 7,  8);
                        IM_POLYLINE_TRIANGLE(8,  5, 6,  7);
                    }
                    else
                    {
                        IM_POLYLINE_TRIANGLE(0,  4,  5, 1);
                        IM_POLYLINE_TRIANGLE(1,  4,  8, 5);
                        IM_POLYLINE_TRIANGLE(2,  4, 10, 8);
                        IM_POLYLINE_TRIANGLE(3,  0,  5, 6);
                        IM_POLYLINE_TRIANGLE(4,  0,  1, 5);
                        IM_POLYLINE_TRIANGLE(5, 10,  9, 7);
                        IM_POLYLINE_TRIANGLE(6, 10,  7, 8);
                        IM_POLYLINE_TRIANGLE(7,  8,  6, 5);
                        IM_POLYLINE_TRIANGLE(8,  8,  7, 6);
                    }

                    draw_list->_IdxWritePtr += 27;
                }
            }

            // Restore index base (see commend next to 'idx_base += 4' above)
            idx_base -= 4;
        }

        [[unlikely]] if (join_type == Bevel || join_type == MiterClip)
        {
            //
            // Left turn                            Right turn
            // 
            //                9                 |               6
            //                 +                |              +
            //                /.\               |             / \
            //             8 +  .\              |            /  .+ 7
            //              / \ . \             |           /  ./ \
            //             /   \ . \            |          / . /   \
            //            /     \ . \           |         / . /     \
            //           /   x   \.  \          |        /.  /   x   \
            //        7 /       4 \.  \         |       /.  / 5       \ 8
            //        .+-----------+_  \        |      /. _+-----------+.
            //   6  .'.|        ..'| ''-+ 5     |   4 +-'' |        ..'| '.  9
            //    +' . |      ..   |   .|       |     |   .|      ..   |   '+
            //    | .  |   ..'     |  ' |       |     |  ' |   ..'     |  .'|
            //    |.   |..'        |.'  |       |     |.'  |..'        |.'  |
            //    + ~ ~+~ ~ ~ ~ ~ ~+~ ~ +       |     + ~ ~+~ ~ ~ ~ ~ ~+~ ~ +
            //    0    1           2    3       |     0    1           2    3
            //
            // 9 triangles
            //

            if (sin_theta < 0.0f)
            {
                IM_POLYLINE_TRIANGLE( 0, 0, 6, 7);
                IM_POLYLINE_TRIANGLE( 1, 0, 1, 7);
                IM_POLYLINE_TRIANGLE( 2, 1, 4, 7);
                IM_POLYLINE_TRIANGLE( 3, 1, 2, 4);
                IM_POLYLINE_TRIANGLE( 4, 2, 5, 4);
                IM_POLYLINE_TRIANGLE( 5, 2, 3, 5);
                IM_POLYLINE_TRIANGLE( 6, 4, 8, 7);
                IM_POLYLINE_TRIANGLE( 7, 4, 9, 8);
                IM_POLYLINE_TRIANGLE( 8, 4, 5, 9);
            }
            else
            {
                IM_POLYLINE_TRIANGLE( 0, 0, 5, 4);
                IM_POLYLINE_TRIANGLE( 1, 0, 1, 5);
                IM_POLYLINE_TRIANGLE( 2, 1, 8, 5);
                IM_POLYLINE_TRIANGLE( 3, 1, 2, 8);
                IM_POLYLINE_TRIANGLE( 4, 2, 9, 8);
                IM_POLYLINE_TRIANGLE( 5, 2, 3, 9);
                IM_POLYLINE_TRIANGLE( 6, 4, 7, 6);
                IM_POLYLINE_TRIANGLE( 7, 4, 5, 7);
                IM_POLYLINE_TRIANGLE( 8, 5, 8, 7);
            }

            draw_list->_IdxWritePtr += 27;
        }
        else
        {
            //
            //   4   5         6   7
            //   +---+----x----+---+
            //   |  .|        .|  .|
            //   |  .|       . |  .|
            //   |  .|      .  |  .|
            //   | . |     .   | . |
            //   | . |    .    | . |
            //   | . |   .     | . |
            //   |.  |  .      |.  |
            //   |.  | .       |.  |
            //   |.  |.        |.  |
            //   +---+---------+---+
            //   0   1         2   3
            //
            // 6 triangles
            //
            IM_POLYLINE_TRIANGLE(0, 0, 5, 4);
            IM_POLYLINE_TRIANGLE(1, 0, 1, 5);
            IM_POLYLINE_TRIANGLE(2, 1, 6, 5);
            IM_POLYLINE_TRIANGLE(3, 1, 2, 6);
            IM_POLYLINE_TRIANGLE(4, 2, 7, 6);
            IM_POLYLINE_TRIANGLE(5, 2, 3, 7);

            draw_list->_IdxWritePtr += 18;
        }

        draw_list->_VtxWritePtr += new_vtx_count;
        idx_base += new_vtx_count;

        p0 = p1;
        n0 = n1;

        last_join_type = join_type;
    }

    if (closed)
    {
        idx_base += 4;

        vtx_start[0].pos = draw_list->_VtxWritePtr[-4].pos;
        vtx_start[1].pos = draw_list->_VtxWritePtr[-3].pos;
        vtx_start[2].pos = draw_list->_VtxWritePtr[-2].pos;
        vtx_start[3].pos = draw_list->_VtxWritePtr[-1].pos;
    }
    else
    {
        draw_list->_VtxWritePtr -= 4;

        [[unlikely]] if (cap == ImDrawFlags_CapSquare)
        {
            const ImVec2 n0 = normals[0];
            const ImVec2 n1 = normals[count - 1];

            vtx_start[0].pos.x                += n0.y * half_fringe_thickness;
            vtx_start[0].pos.y                -= n0.x * half_fringe_thickness;
            vtx_start[1].pos.x                += n0.y * half_thickness;
            vtx_start[1].pos.y                -= n0.x * half_thickness;
            vtx_start[2].pos.x                += n0.y * half_thickness;
            vtx_start[2].pos.y                -= n0.x * half_thickness;
            vtx_start[3].pos.x                += n0.y * half_fringe_thickness;
            vtx_start[3].pos.y                -= n0.x * half_fringe_thickness;

            draw_list->_VtxWritePtr[-4].pos.x -= n1.y * half_fringe_thickness;
            draw_list->_VtxWritePtr[-4].pos.y += n1.x * half_fringe_thickness;
            draw_list->_VtxWritePtr[-3].pos.x -= n1.y * half_thickness;
            draw_list->_VtxWritePtr[-3].pos.y += n1.x * half_thickness;
            draw_list->_VtxWritePtr[-2].pos.x -= n1.y * half_thickness;
            draw_list->_VtxWritePtr[-2].pos.y += n1.x * half_thickness;
            draw_list->_VtxWritePtr[-1].pos.x -= n1.y * half_fringe_thickness;
            draw_list->_VtxWritePtr[-1].pos.y += n1.x * half_fringe_thickness;

            IM_POLYLINE_TRIANGLE(0, -4, -3, -1);
            IM_POLYLINE_TRIANGLE(1, -3, -2, -1);

            const unsigned int zero_index_offset = static_cast<unsigned int>(idx_base - draw_list->_VtxCurrentIdx);

            IM_POLYLINE_TRIANGLE(2, 0 - zero_index_offset, 3 - zero_index_offset, 1 - zero_index_offset);
            IM_POLYLINE_TRIANGLE(3, 3 - zero_index_offset, 2 - zero_index_offset, 1 - zero_index_offset);

            draw_list->_IdxWritePtr += 12;
        }
    }

    const int used_vtx_count = static_cast<int>(draw_list->_VtxWritePtr - vtx_start);
    const int used_idx_count = static_cast<int>(draw_list->_IdxWritePtr - idx_start);
    const int unused_vtx_count = vtx_count - used_vtx_count;
    const int unused_idx_count = idx_count - used_idx_count;

    IM_ASSERT(unused_idx_count >= 0);
    IM_ASSERT(unused_vtx_count >= 0);

    draw_list->PrimUnreserve(unused_idx_count, unused_vtx_count);

    draw_list->_VtxCurrentIdx = idx_base;

#if 0
    [[unlikely]] if (!closed && cap == ImDrawFlags_CapRound)
    {
        const ImVec2 p0 = data[0];
        const ImVec2 p1 = data[count - 1];
        const ImVec2 n0 = normals[0];
        const ImVec2 n1 = normals[count - 1];

        const auto begin_angle = ImAtan2(n0.y, n0.x);
        const auto   end_angle = ImAtan2(n1.y, n1.x);

        const auto half_thickness_to_half_fringe_thickness = half_fringe_thickness / half_thickness;

        draw_list->PathClear();
        draw_list->PathArcTo({ 0, 0 }, half_thickness, begin_angle - IM_PI, begin_angle);
        const int begin_point_count = draw_list->_Path.Size;
        //draw_list->PathArcTo({ 0, 0 }, half_thickness, end_angle, end_angle + IM_PI);
        //const int end_point_count = draw_list->_Path.Size - begin_point_count;

        const int vtx_count = (begin_point_count /*+ end_point_count*/) * 2;
        const int idx_count = (begin_point_count /*+ end_point_count */- 1) * 9;

        draw_list->PrimReserve(idx_count, vtx_count);

        const ImVec2* arc_data = draw_list->_Path.Data;
        for (int i = 0; i < begin_point_count; i++)
        {
            const ImVec2 point = *arc_data++;

            IM_POLYLINE_VERTEX(0, p0.x + point.x,                                           p0.y + point.y,                                           color);
            IM_POLYLINE_VERTEX(1, p0.x + point.x * half_thickness_to_half_fringe_thickness, p0.y + point.y * half_thickness_to_half_fringe_thickness, color_border);

            [[likely]] if (i < begin_point_count - 1)
            {
                IM_POLYLINE_TRIANGLE(0, 0, 2, 1);
                IM_POLYLINE_TRIANGLE(1, 1, 2, 3);
                draw_list->_IdxWritePtr[6] = idx_base;
                draw_list->_IdxWritePtr[7] = draw_list->_VtxCurrentIdx + begin_point_count * 2 - 2;
                draw_list->_IdxWritePtr[8] = idx_base + 2;

                draw_list->_IdxWritePtr += 9;

            }
            
            draw_list->_VtxWritePtr += 2;
            idx_base += 2;
        }

        draw_list->_VtxCurrentIdx = idx_base;
    }
#endif

#undef IM_POLYLINE_VERTEX
#undef IM_POLYLINE_TRIANGLE
}

void ImDrawList_Polyline(ImDrawList* draw_list, const ImVec2* data, const int count, const ImU32 color, const ImDrawFlags draw_flags, float thickness, float miter_limit)
{
    [[unlikely]] if (count < 2 || thickness <= 0.0f) 
        return;

    const bool antialias = !!(draw_list->Flags & ImDrawListFlags_AntiAliasedLines);

    if (antialias)
        ImDrawList_Polyline_AA(draw_list, data, count, color, draw_flags, thickness, miter_limit);
    else
        ImDrawList_Polyline_NoAA(draw_list, data, count, color, draw_flags, thickness, miter_limit);
}


} // namespace ImGuiEx