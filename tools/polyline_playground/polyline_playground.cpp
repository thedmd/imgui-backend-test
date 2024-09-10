#define IMGUI_DEFINE_MATH_OPERATORS
#include "polyline_playground.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "poly2d/Polyline2D.h"
#include "polyline_new.h"
#include "polyline_allegro.h"
#include "clipper2/clipper.h"

#include <span>
#include <chrono>

#if ENABLE_TRACY
#include "Tracy.hpp"
#define ImZoneScoped ZoneScoped
#else
#define ImZoneScoped (void)0
#endif

#pragma region Utilities
template <typename T>
inline constexpr bool false_v = std::false_type::value;

template<std::size_t I>
struct std::tuple_element<I, Clipper2Lib::Point<double>>
{
    using type = double;
};


namespace std {

template <std::size_t I>
double& get(Clipper2Lib::Point<double>& p) noexcept
{
    if constexpr (I == 0)
        return p.x;
    else if constexpr (I == 1)
        return p.y;
    else
        static_assert(false_v<I>, "Index out of range");
}

template <std::size_t I>
const double& get(const Clipper2Lib::Point<double>& p) noexcept
{
    if constexpr (I == 0)
        return p.x;
    else if constexpr (I == 1)
        return p.y;
    else
        static_assert(false_v<I>, "Index out of range");
}

template <std::size_t I>
double&& get(Clipper2Lib::Point<double>&& p) noexcept
{
    if constexpr (I == 0)
        return p.x;
    else if constexpr (I == 1)
        return p.y;
    else
        static_assert(false_v<I>, "Index out of range");
}

template <std::size_t I>
const double&& get(const Clipper2Lib::Point<double>&& p) noexcept
{
    if constexpr (I == 0)
        return p.x;
    else if constexpr (I == 1)
        return p.y;
    else
        static_assert(false_v<I>, "Index out of range");
}

} // namespace std

#include "mapbox/earcut.hpp"

template <typename T>
T ImSmoothDamp(T current, T target, T& currentVelocity, float smoothTime, float deltaTime, float maxSpeed = std::numeric_limits<float>::infinity());

template <typename T>
inline T ImSmoothDamp(T current, T target, T& currentVelocity, float smoothTime, float deltaTime, float maxSpeed)
{
    smoothTime = ImMax(0.0001f, smoothTime);
    const auto  num = deltaTime * 2.0f / smoothTime;
    const auto  d = 1.0f / (1.0f + num + 0.48f * num * num + 0.235f * num * num * num);
    auto vector = current - target;
    auto vector2 = target;
    auto maxLength = maxSpeed * smoothTime;
    auto vectorLength = ImSqrt(ImLengthSqr(vector));
    if (vectorLength > 0.0f)
        vector = vector * (1.0f / vectorLength);
    vector = vector * ImMin(vectorLength, maxLength);
    target = current - vector;
    auto vector3 = (currentVelocity + vector * num) * deltaTime;
    currentVelocity = (currentVelocity - vector3 * num) * d;
    auto vector4 = target + (vector + vector3) * d;
    if (ImDot(vector2 - current, vector4 - vector2) > 0.0f)
    {
        vector4 = vector2;
        currentVelocity = (vector4 - vector2) / deltaTime;
    }
    return vector4;
}

template <>
inline float ImSmoothDamp<float>(float current, float target, float& currentVelocity, float smoothTime, float deltaTime, float maxSpeed)
{
    smoothTime = ImMax(0.0001f, smoothTime);
    float num = 2.0f / smoothTime;
    float num2 = num * deltaTime;
    float num3 = 1.0f / (1.0f + num2 + 0.48f * num2 * num2 + 0.235f * num2 * num2 * num2);
    float num4 = current - target;
    float num5 = target;
    float num6 = maxSpeed * smoothTime;
    num4 = ImClamp(num4, -num6, num6);
    target = current - num4;
    float num7 = (currentVelocity + num * num4) * deltaTime;
    currentVelocity = (currentVelocity - num * num7) * num3;
    float num8 = target + (num4 + num7) * num3;
    if (num5 - current > 0.0f == num8 > num5)
    {
        num8 = num5;
        currentVelocity = (num8 - num5) / deltaTime;
    }
    return num8;
}

template <typename T>
    requires std::is_same_v<T, float> || std::is_same_v<T, ImVec2>
struct ImSmoothVariable
{
    ImSmoothVariable(ImGuiID id) : m_Id(id) { Fetch(); }
    ImSmoothVariable(ImGuiID id, T value) : m_Id(id), m_Value(value) { Fetch(); }
    auto Next(T target, float dt) -> T { m_Value = ImSmoothDamp(m_Value, target, m_Velocity, m_SmoothTime, dt); Commit(); return m_Value; }
    void Reset(T value = {}) { m_Value = value; m_Velocity = {}; Commit(); }
    void ResetVelocity() { m_Velocity = {}; Commit(); }
    auto Get() const -> T { return m_Value; }
    operator T() const { return m_Value; }
    auto operator->() -> T& { return &m_Value; }
    auto operator->() const -> const T& { return &m_Value; }
    auto operator*() -> T& { return m_Value; }
    auto operator*() const -> const T& { return m_Value; }
    ImSmoothVariable& operator=(const T& value) { m_Value = value; return *this; }

private:
    void Fetch()
    {
        auto storage = ImGui::GetStateStorage();
        ImGui::PushID(m_Id);
        if constexpr (std::is_same_v<T, float>)
        {
            m_Value    = storage->GetFloat(ImGui::GetID("Value"), m_Value);
            m_Velocity = storage->GetFloat(ImGui::GetID("Velocity"), m_Velocity);
        }
        else if constexpr (std::is_same_v<T, ImVec2>)
        {
            m_Value.x    = storage->GetFloat(ImGui::GetID("Value.x"), m_Value.x);
            m_Value.y    = storage->GetFloat(ImGui::GetID("Value.y"), m_Value.y);
            m_Velocity.x = storage->GetFloat(ImGui::GetID("Velocity.x"), m_Velocity.x);
            m_Velocity.y = storage->GetFloat(ImGui::GetID("Velocity.y"), m_Velocity.y);
        }
        else
        {
            static_assert(false_v<T>, "Unsupported type");
        }
        ImGui::PopID();
    }

    void Commit()
    {
        auto storage = ImGui::GetStateStorage();
        ImGui::PushID(m_Id);
        if constexpr (std::is_same_v<T, float>)
        {
            storage->SetFloat(ImGui::GetID("Value"), m_Value);
            storage->SetFloat(ImGui::GetID("Velocity"), m_Velocity);
        }
        else if constexpr (std::is_same_v<T, ImVec2>)
        {
            storage->SetFloat(ImGui::GetID("Value.x"), m_Value.x);
            storage->SetFloat(ImGui::GetID("Value.y"), m_Value.y);
            storage->SetFloat(ImGui::GetID("Velocity.x"), m_Velocity.x);
            storage->SetFloat(ImGui::GetID("Velocity.y"), m_Velocity.y);
        }
        else
        {
            static_assert(false_v<T>, "Unsupported type");
        }
        ImGui::PopID();
    }

    ImGuiID m_Id;
    float   m_SmoothTime = 0.075f;
    T       m_Value      = {};
    T       m_Velocity   = {};
};

template <typename V>
struct ComboBoxItem
{
    const char* const Name;
    const V           Value;
};

template <typename T>
bool ComboBox(const char* label, T& current_value, std::initializer_list<const ComboBoxItem<T>> values)
{
    const int count = static_cast<int>(values.size());

    float max_value_width = 0.0f;
    for (const auto& value : values)
        max_value_width = ImMax(max_value_width, ImGui::CalcTextSize(value.Name).x);

    ImGui::PushItemWidth(max_value_width + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x * 2 + ImGui::GetTextLineHeight());

    auto current_value_it = std::find_if(values.begin(), values.end(), [&](const ComboBoxItem<T>& item) { return item.Value == current_value; });
    const char* current_value_name = current_value_it != values.end() ? current_value_it->Name : "Unknown";

    bool value_changed = false;
    int current_value_index = current_value_it != values.end() ? static_cast<int>(current_value_it - values.begin()) : -1;
    if (ImGui::BeginCombo(label, current_value_name, ImGuiComboFlags_None))
    {
        int n = 0;
        for (const auto& value : values)
        {
            ImGui::PushID(n);
            const bool is_selected = current_value_index == n;
            if (ImGui::Selectable(value.Name, is_selected))
            {
                current_value_index = n;
                current_value       = value.Value;
                value_changed       = true;
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
            ++n;
        }
        ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered() && ImGui::Shortcut(ImGuiKey_MouseWheelY, ImGuiInputFlags_RouteGlobal))
    {
        const auto value        = -ImGui::GetIO().MouseWheel;
        const auto positive     = value > 0.0f;
        const auto wheel_step   = static_cast<int>(value);
        const auto step         = positive ? ImMax(wheel_step, 1) : ImMin(wheel_step, -1);

        const auto new_item_index = ImClamp(current_value_index + step, 0, count - 1);

        if (new_item_index != current_value_index)
        {
            current_value_index = new_item_index;
            current_value       = std::next(values.begin(), current_value_index)->Value;
            value_changed       = true;
        }
    }

    ImGui::PopItemWidth();
    
    return value_changed;
}

#pragma endregion

namespace ImPolyline {

static State state;

#define IM_NORMALIZE2F_OVER_ZERO(VX,VY)     { float d2 = VX*VX + VY*VY; if (d2 > 0.0f) { float inv_len = ImRsqrt(d2); VX *= inv_len; VY *= inv_len; } } (void)0
#define IM_FIXNORMAL2F_MAX_INVLEN2          100.0f // 500.0f (see #4053, #3366)
#define IM_FIXNORMAL2F(VX,VY)               { float d2 = VX*VX + VY*VY; if (d2 > 0.000001f) { float inv_len2 = 1.0f / d2; if (inv_len2 > IM_FIXNORMAL2F_MAX_INVLEN2) inv_len2 = IM_FIXNORMAL2F_MAX_INVLEN2; VX *= inv_len2; VY *= inv_len2; } } (void)0

void ImDrawList_Polyline(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, PolylineFlags flags, float thickness)
{
    if (count < 2 || thickness <= 0.0f)
        return;

    const bool is_closed = ((flags & PolylineFlags_Closed) != 0) && (count > 2);
    const bool is_capped = !is_closed && ((flags & PolylineFlags_SquareCaps) != 0);

    // Compute normals
    ImVector<ImVec2>& temp_buffer = draw_list->_Data->TempBuffer;
    temp_buffer.reserve_discard(count);
    ImVec2* temp_normals = temp_buffer.Data;

#define IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(i0, i1, n)                            \
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
        n.x =  dy;                                              \
        n.y = -dx;                                              \
    } while (false)

    for (int i = 0; i < count - 1; ++i)
    {
        IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(i, i + 1, temp_normals[i]);
    }

    if (is_closed)
    {
        IM_POLYLINE_COMPUTE_SEGMENT_DETAILS(count - 1, 0, temp_normals[count - 1]);
    }
    else
        temp_normals[count - 1] = temp_normals[count - 2];

#undef IM_POLYLINE_COMPUTE_SEGMENT_DETAILS

    const float half_thickness = thickness * 0.5f;

    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

    const int idx_count = is_closed ? count * 6 : (count - 1) * 6;
    const int vtx_count = count * 2;

    draw_list->PrimReserve(idx_count, vtx_count);

    ImDrawVert*&  vtx_write      = draw_list->_VtxWritePtr;
    ImDrawVert*   vtx_start      = vtx_write;
    ImDrawIdx*&   idx_write      = draw_list->_IdxWritePtr;
    ImDrawIdx*    idx_start      = idx_write;
    unsigned int& base_vtx_index = draw_list->_VtxCurrentIdx;
    unsigned int  start_vtx_idx  = base_vtx_index;

    if (is_closed)
    {
        const ImVec2& p0 = data[count - 1];
        const ImVec2& p1 = data[0];
        const ImVec2& n0 = temp_normals[count - 1];
        const ImVec2& n1 = temp_normals[0];

        ImVec2 n;
        n.x = (n0.x + n1.x) * 0.5f;
        n.y = (n0.y + n1.y) * 0.5f;
        float d2 = n.x * n.x + n.y * n.y;
        if (d2 > 0.000001f)
        {
            float inv_len2 = 1.0f / d2;
            if (inv_len2 > 100.0f)
                inv_len2 = 100.0f;
            n.x *= inv_len2;
            n.y *= inv_len2;
        }

        vtx_write[0].pos = p1 - n * half_thickness;
        vtx_write[0].uv  = uv;
        vtx_write[0].col = color;

        vtx_write[1].pos = p1 + n * half_thickness;
        vtx_write[1].uv  = uv;
        vtx_write[1].col = color;

        vtx_write += 2;
    }
    else
    {
        vtx_write[0].pos = data[0] - temp_normals[0] * half_thickness;
        vtx_write[0].uv  = uv;
        vtx_write[0].col = color;

        vtx_write[1].pos = data[0] + temp_normals[0] * half_thickness;
        vtx_write[1].uv  = uv;
        vtx_write[1].col = color;

        if (is_capped)
        {
            vtx_write[0].pos.x +=  temp_normals[0].y * half_thickness;
            vtx_write[0].pos.y += -temp_normals[0].x * half_thickness;
            vtx_write[1].pos.x +=  temp_normals[0].y * half_thickness;
            vtx_write[1].pos.y += -temp_normals[0].x * half_thickness;
        }

        vtx_write += 2;
    }

    for (int i = 1; i < count; ++i)
    {
        const ImVec2& p0 = data[i - 1];
        const ImVec2& p1 = data[i];
        const ImVec2& n0 = temp_normals[i - 1];
        const ImVec2& n1 = temp_normals[i];

        ImVec2 n;
        n.x = (n0.x + n1.x) * 0.5f;
        n.y = (n0.y + n1.y) * 0.5f;
        //IM_FIXNORMAL2F(n.x, n.y);

        float d2 = n.x * n.x + n.y * n.y;
        if (d2 > 0.000001f)
        {
            float inv_len2 = 1.0f / d2;
            //if (inv_len2 > 100.0f)
            //    inv_len2 = 100.0f;
            n.x *= inv_len2;
            n.y *= inv_len2;
        }

        vtx_write[0].pos = p1 - n * half_thickness;
        vtx_write[0].uv  = uv;
        vtx_write[0].col = color;

        vtx_write[1].pos = p1 + n * half_thickness;
        vtx_write[1].uv  = uv;
        vtx_write[1].col = color;

        vtx_write += 2;

        idx_write[0] = base_vtx_index;
        idx_write[1] = base_vtx_index + 1;
        idx_write[2] = base_vtx_index + 2;
        idx_write[3] = base_vtx_index + 1;
        idx_write[4] = base_vtx_index + 2;
        idx_write[5] = base_vtx_index + 3;

        idx_write += 6;
        base_vtx_index += 2;
    }

    base_vtx_index += 2;

    if (is_closed)
    {
        idx_write[0] = base_vtx_index - 2;
        idx_write[1] = base_vtx_index - 1;
        idx_write[2] = start_vtx_idx;
        idx_write[3] = base_vtx_index - 1;
        idx_write[4] = start_vtx_idx;
        idx_write[5] = start_vtx_idx + 1;
    }
    else if (is_capped)
    {
        vtx_write[-1].pos.x += -temp_normals[count - 2].y * half_thickness;
        vtx_write[-1].pos.y +=  temp_normals[count - 2].x * half_thickness;
        vtx_write[-2].pos.x += -temp_normals[count - 2].y * half_thickness;
        vtx_write[-2].pos.y +=  temp_normals[count - 2].x * half_thickness;
    }
}

void ImDrawList_Polyline2D(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, PolylineFlags flags, float thickness)
{
    const auto is_closed = (flags & PolylineFlags_Closed) && count > 2;
    const auto is_capped = !is_closed && (flags & PolylineFlags_SquareCaps);

    std::span<const ImVec2> points(data, count);

    auto jointStyle = crushedpixel::Polyline2D::JointStyle::MITER;
    switch (state.LineCap)
    {
        using enum ImGuiEx::ImDrawFlagsExtra_;

        case ImDrawFlags_JoinMiter:     jointStyle = crushedpixel::Polyline2D::JointStyle::MITER; break;
        case ImDrawFlags_JoinMiterClip: jointStyle = crushedpixel::Polyline2D::JointStyle::MITER; break;
        case ImDrawFlags_JoinBevel:     jointStyle = crushedpixel::Polyline2D::JointStyle::BEVEL; break;
        case ImDrawFlags_JoinRound:     jointStyle = crushedpixel::Polyline2D::JointStyle::ROUND; break;
    }

    auto endCapStyle = crushedpixel::Polyline2D::EndCapStyle::BUTT;
    switch (state.LineCap)
    {
        using enum ImGuiEx::ImDrawFlagsExtra_;

        case ImDrawFlags_CapButt:   endCapStyle = crushedpixel::Polyline2D::EndCapStyle::BUTT;   break;
        case ImDrawFlags_CapSquare: endCapStyle = crushedpixel::Polyline2D::EndCapStyle::SQUARE; break;
        case ImDrawFlags_CapRound:  endCapStyle = crushedpixel::Polyline2D::EndCapStyle::ROUND;  break;
    }

    if (is_closed)
        endCapStyle = crushedpixel::Polyline2D::EndCapStyle::JOINT;
    else if (is_capped)
        endCapStyle = crushedpixel::Polyline2D::EndCapStyle::SQUARE;

    auto allowOverlap = false;

    auto vertices = crushedpixel::Polyline2D::create<ImVec2>(points, thickness, jointStyle, endCapStyle, allowOverlap);

    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();

    draw_list->PrimReserve(static_cast<int>(vertices.size()), static_cast<int>(vertices.size()));

    auto& base_index = draw_list->_VtxCurrentIdx;

    for (const auto& vertex : vertices)
        draw_list->PrimVtx(vertex, uv, color);
}

void ImDrawList_Polyline_Allegro(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, PolylineFlags flags, float thickness)
{
    using enum ImGuiEx::ImDrawFlagsExtra_;

    const auto is_closed = (flags & PolylineFlags_Closed) && count > 2;
    const auto is_capped = !is_closed && (flags & PolylineFlags_SquareCaps);

    int line_cap = ALLEGRO_LINE_CAP_NONE;
    switch (state.LineCap)
    {
        case ImDrawFlags_CapButt:   line_cap = ALLEGRO_LINE_CAP_NONE;   break;
        case ImDrawFlags_CapSquare: line_cap = ALLEGRO_LINE_CAP_SQUARE; break;
        case ImDrawFlags_CapRound:  line_cap = ALLEGRO_LINE_CAP_ROUND;  break;
    }
    if (is_closed)
        line_cap = ALLEGRO_LINE_CAP_CLOSED;
    else if (is_capped)
        line_cap = ALLEGRO_LINE_CAP_SQUARE;

    int line_join = ALLEGRO_LINE_JOIN_MITER;
    switch (state.LineJoin)
    {
        case ImDrawFlags_JoinMiter:     line_join = ALLEGRO_LINE_JOIN_MITER; break;
        case ImDrawFlags_JoinMiterClip: line_join = ALLEGRO_LINE_JOIN_MITER; break;
        case ImDrawFlags_JoinBevel:     line_join = ALLEGRO_LINE_JOIN_BEVEL; break;
        case ImDrawFlags_JoinRound:     line_join = ALLEGRO_LINE_JOIN_ROUND; break;
    }

    imgui_al_draw_polyline(draw_list, &data->x, sizeof(ImVec2), count, line_join, line_cap, color, thickness, state.MiterLimit);
}

void ImDrawList_Polyline_Clipper2(ImDrawList* draw_list, const ImVec2* data, int count, ImU32 color, PolylineFlags flags, float thickness)
{
    if (count <= 0)
        return;

    const auto is_closed = (flags & PolylineFlags_Closed) && count > 2;
    const auto is_capped = !is_closed && (flags & PolylineFlags_SquareCaps);

    //const auto [min_x_it, max_x_it] = std::minmax_element(data, data + count, [](const auto& a, const auto& b) { return a.x < b.x; });
    //const auto [min_y_it, max_y_it] = std::minmax_element(data, data + count, [](const auto& a, const auto& b) { return a.y < b.y; });
    //const auto min_x = min_x_it->x;
    //const auto max_x = max_x_it->x;
    //const auto min_y = min_y_it->y;
    //const auto max_y = max_y_it->y;

    //const auto size_x = std::ceil(max_x - min_x);
    //const auto size_y = std::ceil(max_y - min_y);

    //const auto range_x = powf(10, Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION);
    //const auto range_y = powf(10, Clipper2Lib::CLIPPER2_MAX_DEC_PRECISION);

    Clipper2Lib::PathD points;
    points.reserve(count);
    for (int i = 0; i < count; ++i)
        points.emplace_back(data[i].x, data[i].y);

    auto joinType = Clipper2Lib::JoinType::Square;
    switch (state.LineJoin)
    {
        using enum ImGuiEx::ImDrawFlagsExtra_;

        case ImDrawFlags_JoinMiter:     joinType = Clipper2Lib::JoinType::Miter; break;
        case ImDrawFlags_JoinMiterClip: joinType = Clipper2Lib::JoinType::Miter; break;
        case ImDrawFlags_JoinBevel:     joinType = Clipper2Lib::JoinType::Bevel; break;
        case ImDrawFlags_JoinRound:     joinType = Clipper2Lib::JoinType::Round; break;
    }

    auto endType = Clipper2Lib::EndType::Butt;
    switch (state.LineCap)
    {
        using enum ImGuiEx::ImDrawFlagsExtra_;

        case ImDrawFlags_CapButt:   endType = Clipper2Lib::EndType::Butt;   break;
        case ImDrawFlags_CapSquare: endType = Clipper2Lib::EndType::Square; break;
        case ImDrawFlags_CapRound:  endType = Clipper2Lib::EndType::Round;  break;
    }
    if (is_closed)
        endType = Clipper2Lib::EndType::Joined;
    else if (is_capped)
        endType = Clipper2Lib::EndType::Square;

    Clipper2Lib::PathsD paths;
    paths.emplace_back(std::move(points));

    auto result = Clipper2Lib::InflatePaths(paths, static_cast<double>(thickness) * 0.5, joinType, endType, state.MiterLimit, 6);

    auto draw_flags = draw_list->Flags;
    draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedFill);

    auto indices = mapbox::earcut<uint32_t>(result);

    int vtx_count = 0;
    for (const auto& path : result)
        vtx_count += static_cast<int>(path.size());

    int idx_count = static_cast<int>(indices.size());

    draw_list->PrimReserve(idx_count, vtx_count);

    auto& vtx_wirte = draw_list->_VtxWritePtr;
    auto& idx_write = draw_list->_IdxWritePtr;

    const auto uv = ImGui::GetFontTexUvWhitePixel();
    for (const auto& path : result)
    {
        for (const auto& point : path)
        {
            vtx_wirte->pos = { static_cast<float>(point.x), static_cast<float>(point.y) };
            vtx_wirte->uv  = uv;
            vtx_wirte->col = color;
            ++vtx_wirte;
        }
    }

    const auto base_vtx_index = draw_list->_VtxCurrentIdx;
    for (const auto index : indices)
    {
        *idx_write = static_cast<ImDrawIdx>(base_vtx_index + index);
        ++idx_write;
    }

    draw_list->_VtxCurrentIdx += vtx_count;

    draw_list->Flags = draw_flags;
}

auto Polyline::Draw(ImDrawList* draw_list, const ImVec2& origin, Method method, int stress) const -> Stats
{
    ImZoneScoped;

    ImDrawFlags flags = ImDrawFlags_None;
    if (this->Flags & PolylineFlags_Closed)
        flags |= ImDrawFlags_Closed;

    const auto draw_flags = draw_list->Flags;

    if (!(this->Flags & PolylineFlags_AntiAliased))
        draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex);

    const auto repeat_count = ImMax(1, stress);

    Stats stats;
    stats.Elements = draw_list->CmdBuffer.back().ElemCount;
    stats.Vertices = draw_list->VtxBuffer.Size;
    stats.Indices  = draw_list->IdxBuffer.Size;

    const auto vtx_current_idx = draw_list->_VtxCurrentIdx;

    const auto first_vertex = draw_list->VtxBuffer.Size;

    const auto start_timestamp = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < repeat_count; ++i)
    {
        if (i > 0)
        {
            draw_list->CmdBuffer.back().ElemCount = stats.Elements;
            draw_list->VtxBuffer.resize(stats.Vertices);
            draw_list->IdxBuffer.resize(stats.Indices);
            draw_list->_VtxWritePtr = draw_list->VtxBuffer.Data + stats.Vertices;
            draw_list->_IdxWritePtr = draw_list->IdxBuffer.Data + stats.Indices;
            draw_list->_VtxCurrentIdx = vtx_current_idx;
        }

        switch (method)
        {
            case Method::Upstream:
                ImGuiEx::ImDrawList_Polyline_Upstream(draw_list, Points.data(), static_cast<int>(Points.size()), Color, flags, Thickness);
                break;

            case Method::PR2964:
                ImGuiEx::ImDrawList_Polyline_PR2964(draw_list, Points.data(), static_cast<int>(Points.size()), Color, flags, Thickness);
                break;

            case Method::New:
                ImGuiEx::ImDrawList_Polyline(draw_list, Points.data(), static_cast<int>(Points.size()), Color, flags | state.LineJoin | state.LineCap, Thickness, state.MiterLimit);
                break;

            case Method::NewOptimized:
                ImGuiEx::ImDrawList_Polyline_Optimized(draw_list, Points.data(), static_cast<int>(Points.size()), Color, flags | state.LineJoin | state.LineCap, Thickness, state.MiterLimit);
                break;

            case Method::NewV3:
                ImGuiEx::ImDrawList_Polyline_V3(draw_list, Points.data(), static_cast<int>(Points.size()), Color, flags | state.LineJoin | state.LineCap, Thickness, state.MiterLimit);
                break;

            case Method::Polyline2D:
                ImDrawList_Polyline2D(draw_list, Points.data(), static_cast<int>(Points.size()), Color, this->Flags, Thickness);
                break;

            case Method::Allegro:
                ImDrawList_Polyline_Allegro(draw_list, Points.data(), static_cast<int>(Points.size()), Color, this->Flags, Thickness);
                break;

            case Method::Clipper2:
                ImDrawList_Polyline_Clipper2(draw_list, Points.data(), static_cast<int>(Points.size()), Color, this->Flags, Thickness);
                break;
        }
    }

    const auto end_timestamp = std::chrono::high_resolution_clock::now();

    stats.Elements = draw_list->CmdBuffer.back().ElemCount - stats.Elements;
    stats.Vertices = draw_list->VtxBuffer.Size             - stats.Vertices;
    stats.Indices  = draw_list->IdxBuffer.Size             - stats.Indices;
    stats.Duration = std::chrono::duration<double>(end_timestamp - start_timestamp).count();
    stats.DurationAvg = stats.Duration / repeat_count;
    stats.Iterations = repeat_count;

    //stats.Elements /= 3;

    const auto last_vertex = draw_list->VtxBuffer.Size;

    for (int i = first_vertex; i < last_vertex; ++i)
    {
        auto& vertex = draw_list->VtxBuffer[i];
        vertex.pos.x += origin.x;
        vertex.pos.y += origin.y;
    }

    draw_list->Flags = draw_flags;

    return stats;
}

State::State()
{
    SettingsHandler.TypeName = "Polyline";
    SettingsHandler.TypeHash = ImHashStr(SettingsHandler.TypeName);
    SettingsHandler.ClearAllFn =
        [](ImGuiContext*, ImGuiSettingsHandler* handler) -> void
        {
            auto state = static_cast<State*>(handler->UserData);
            state->Clear();
        };
    ;
    SettingsHandler.ReadOpenFn =
        [](ImGuiContext*, ImGuiSettingsHandler* handler, const char* name) -> void*
        {
            return handler->UserData;
        }
    ;
    SettingsHandler.ReadLineFn =
        [](ImGuiContext*, ImGuiSettingsHandler* handler, void* entry, const char* line) -> void
        {
            auto state = static_cast<State*>(handler->UserData);
            int polyline_count = 0;
            int selected_polyline = -1;
            int point_count = 0;
            float x = 0.0f;
            float y = 0.0f;
            float thickness = 0.0f;
            ImU32 color = 0;
            decltype(Polyline::Name) name = {};
            int flag = 0;
            ImRect rect;

            if (sscanf_s(line, "PolylineCount=%i", &polyline_count) == 1)
            {
                state->Polylines.reserve(static_cast<size_t>(polyline_count));
            }
            else if (sscanf_s(line, "EnableEdit=%d", &flag) == 1)
            {
                state->EnableEdit = flag ? 1 : 0;
            }
            else if (sscanf_s(line, "ShowPoints=%d", &flag) == 1)
            {
                state->ShowPoints = flag ? 1 : 0;
            }
            else if (sscanf_s(line, "ShowLines=%d", &flag) == 1)
            {
                state->ShowLines = flag ? 1 : 0;
            }
            else if (sscanf_s(line, "ShowMesh=%d", &flag) == 1)
            {
                state->ShowMesh = flag ? 1 : 0;
            }
            else if (sscanf_s(line, "Method=%d", &flag) == 1)
            {
                state->Method = static_cast<decltype(state->Method)>(flag);
            }
            else if (sscanf_s(line, "Cap=%d", &flag) == 1)
            {
                state->LineCap = flag;
            }
            else if (sscanf_s(line, "Join=%d", &flag) == 1)
            {
                state->LineJoin = flag;
            }
            else if (sscanf_s(line, "MiterLimit=%g", &state->MiterLimit) == 1)
            {
            }
            else if (sscanf_s(line, "Stress=%d", &state->Stress) == 1)
            {
            }
            else if (sscanf_s(line, "PolylineTemplate=%d", &flag) == 1)
            {
                state->Template = static_cast<decltype(state->Template)>(flag);
            }
            else if (sscanf_s(line, "Name=%63[^\n]", name, static_cast<unsigned int>(sizeof(name))) == 1)
            {
                state->Polylines.push_back(make_unique<Polyline>());
                auto& polyline = state->Polylines.back();
                strcpy_s(polyline->Name, name);
            }
            else if (sscanf_s(line, "ViewRect=%g,%g,%g,%g", &rect.Min.x, &rect.Min.y, &rect.Max.x, &rect.Max.y) == 4)
            {
                auto& polyline = state->Polylines.back();
                polyline->ViewRect = rect;
            }
            else if (sscanf_s(line, "PointCount=%i", &point_count) == 1)
            {
                auto& polyline = state->Polylines.back();                
                polyline->Points.reserve(static_cast<size_t>(point_count));
            }
            else if (sscanf_s(line, "Point=%g,%g", &x, &y) == 2) 
            {
                auto& polyline = state->Polylines.back();                
                polyline->Points.push_back({ x, y });
            }
            else if (sscanf_s(line, "Thickness=%g", &thickness) == 1)
            {
                auto& polyline = state->Polylines.back();
                polyline->Thickness = thickness;
            }
            else if (sscanf_s(line, "Color=%x", &color) == 1)
            {
                auto& polyline = state->Polylines.back();
                polyline->Color = color;
            }
            else if (sscanf_s(line, "Closed=%d", &flag) == 1)
            {
                auto& polyline = state->Polylines.back();
                if (flag)
                    polyline->Flags |= PolylineFlags_Closed;
                else
                    polyline->Flags &= ~PolylineFlags_Closed;
            }
            else if (sscanf_s(line, "AntiAliased=%d", &flag) == 1)
            {
                auto& polyline = state->Polylines.back();
                if (flag)
                    polyline->Flags |= PolylineFlags_AntiAliased;
                else
                    polyline->Flags &= ~PolylineFlags_AntiAliased;
            }
            else if (sscanf_s(line, "EndCaps=%d", &flag) == 1)
            {
                auto& polyline = state->Polylines.back();
                if (flag)
                    polyline->Flags |= PolylineFlags_SquareCaps;
                else
                    polyline->Flags &= ~PolylineFlags_SquareCaps;
            }
            else if (sscanf_s(line, "SelectedPolyline=%i", &selected_polyline) == 1)
            {
                state->SetCurrent(selected_polyline);
            }
        }
    ;
    SettingsHandler.WriteAllFn =
        [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf) -> void
        {
            auto state = static_cast<State*>(handler->UserData);
            out_buf->appendf("[Polyline][Settings]\n");
            out_buf->appendf("PolylineCount=%i\n", state->Polylines.size());
            out_buf->appendf("EnableEdit=%d\n", state->EnableEdit ? 1 : 0);
            out_buf->appendf("ShowPoints=%d\n", state->ShowPoints ? 1 : 0);
            out_buf->appendf("ShowLines=%d\n", state->ShowLines ? 1 : 0);
            out_buf->appendf("ShowMesh=%d\n", state->ShowMesh ? 1 : 0);
            out_buf->appendf("Method=%d\n",  std::to_underlying(state->Method));
            out_buf->appendf("Cap=%d\n", state->LineCap);
            out_buf->appendf("Join=%d\n", state->LineJoin);
            out_buf->appendf("MiterLimit=%g\n", state->MiterLimit);
            out_buf->appendf("Stress=%d\n", state->Stress);
            out_buf->appendf("PolylineTemplate=%d\n", std::to_underlying(state->Template));
            for (const auto& polyline : state->Polylines)
            {
                out_buf->appendf("Name=%s\n", polyline->Name);
                out_buf->appendf("ViewRect=%g,%g,%g,%g\n", polyline->ViewRect.Min.x, polyline->ViewRect.Min.y, polyline->ViewRect.Max.x, polyline->ViewRect.Max.y);
                out_buf->appendf("PointCount=%i\n", polyline->Points.size());
                for (const auto& point : polyline->Points)
                    out_buf->appendf("Point=%g,%g\n", point.x, point.y);
                out_buf->appendf("Thickness=%g\n", polyline->Thickness);
                out_buf->appendf("Color=%x\n", polyline->Color);
                out_buf->appendf("Closed=%d\n", (polyline->Flags & PolylineFlags_Closed) ? 1 : 0);
                out_buf->appendf("AntiAliased=%d\n", (polyline->Flags & PolylineFlags_AntiAliased) ? 1 : 0);
                out_buf->appendf("EndCaps=%d\n", (polyline->Flags & PolylineFlags_SquareCaps) ? 1 : 0);
            }
            out_buf->appendf("SelectedPolyline=%i\n", state->Index(state->Current));
            out_buf->append("\n");
        }
    ;
    SettingsHandler.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler) -> void {};
    SettingsHandler.UserData = this;
}

static void SetPolyline(Polyline& polyline, PolylineTemplate content)
{
    const auto loop_count = 200;
    const auto size       = ImVec2(120.0f, 120.0f);
    const auto center     = ImVec2(60.0f, 60.0f);
    const auto radius     = 48.0f;
    const auto rounding   = 8.0f;

    polyline.Points.clear();
    polyline.Flags     = PolylineFlags_None;
    polyline.Color     = IM_COL32(255, 255, 255, 255);
    polyline.Thickness = 1.0f;

    auto draw_list = ImGui::GetForegroundDrawList();

    switch (content)
    {
        case PolylineTemplate::Empty:
            break;

        case PolylineTemplate::RectStroke:
            draw_list->PathRect(ImVec2(0.50f, 0.50f), size - ImVec2(0.50f, 0.50f), 0.0f, ImDrawFlags_None);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Rect (Stroke)");
            break;

        case PolylineTemplate::RectStrokeThick:
            draw_list->PathRect(ImVec2(0.50f, 0.50f), size - ImVec2(0.50f, 0.50f), 0.0f, ImDrawFlags_None);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Rect (Stroke Thick)");
            break;

        case PolylineTemplate::RectRoundedStroke:
            draw_list->PathRect(ImVec2(0.50f, 0.50f), size - ImVec2(0.50f, 0.50f), rounding, ImDrawFlags_None);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Rect Rounded (Stroke)");
            break;

        case PolylineTemplate::RectRoundedStrokeThick:
            draw_list->PathRect(ImVec2(0.50f, 0.50f), size - ImVec2(0.50f, 0.50f), rounding, ImDrawFlags_None);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Rect Rounded (Stroke Thick)");
            break;

        case PolylineTemplate::CircleStroke:
            draw_list->_PathArcToFastEx(center, radius - 0.5f, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
            draw_list->_Path.Size--;
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Circle (Stroke)");
            break;

        case PolylineTemplate::CircleStrokeThick:
            draw_list->_PathArcToFastEx(center, radius - 0.5f, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
            draw_list->_Path.Size--;
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Circle (Stroke Thick)");
            break;

        case PolylineTemplate::TriangleStroke:
            draw_list->PathArcTo(center, radius - 0.5f, 0.0f, 4.188790f, 2);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Triangle (Stroke)");
            break;

        case PolylineTemplate::TriangleStrokeThick:
            draw_list->PathArcTo(center, radius - 0.5f, 0.0f, 4.188790f, 2);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Triangle (Stroke Thick)");
            break;

        case PolylineTemplate::LongStroke:
            draw_list->PathArcTo(center, radius - 0.5f, 0.0f, IM_PI * 2 * (10 * loop_count - 1) / (10 * loop_count), 10 * loop_count);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Long (Stroke)");
            break;

        case PolylineTemplate::LongStrokeThick:
            draw_list->PathArcTo(center, radius - 0.5f, 0.0f, IM_PI * 2 * (10 * loop_count - 1) / (10 * loop_count), 10 * loop_count);
            polyline.Flags |= PolylineFlags_Closed | PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Long (Stroke Thick)");
            break;

        case PolylineTemplate::LongJaggedStroke:
            for (float n = 0; n < 10 * loop_count; n += 2.51327412287f)
                draw_list->PathLineTo(center + ImVec2(radius * sinf(n), radius * cosf(n)));
            polyline.Flags |= PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Long Jagged (Stroke)");
            break;

        case PolylineTemplate::LongJaggedStrokeThick:
            for (float n = 0; n < 10 * loop_count; n += 2.51327412287f)
                draw_list->PathLineTo(center + ImVec2(radius * sinf(n), radius * cosf(n)));
            polyline.Flags |= PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Long Jagged (Stroke Thick)");
            break;

        case PolylineTemplate::LineStroke:
            draw_list->PathLineTo(ImVec2(0.50f, 0.50f));
            draw_list->PathLineTo(size + ImVec2(0.50f, 0.50f));
            polyline.Flags |= PolylineFlags_AntiAliased;
            strcpy_s(polyline.Name, "Line (Stroke)");
            break;

        case PolylineTemplate::LineStrokeThick:
            draw_list->PathLineTo(ImVec2(0.50f, 0.50f));
            draw_list->PathLineTo(size + ImVec2(0.50f, 0.50f));
            polyline.Flags |= PolylineFlags_AntiAliased;
            polyline.Thickness = 4.0f;
            strcpy_s(polyline.Name, "Line (Stroke Thick)");
            break;
    }

    auto min = ImVec2(FLT_MAX, FLT_MAX);
    auto max = ImVec2(-FLT_MAX, -FLT_MAX);
    for (const auto& point : draw_list->_Path)
    {
        min = ImMin(min, point);
        max = ImMax(max, point);
    }
    auto shape_size = max - min;

    const float view_rect_margin = 0.1f;
    polyline.ViewRect = ImRect(min - shape_size * view_rect_margin * 0.5f, max + shape_size * view_rect_margin * 0.5f);

    polyline.Points.reserve(draw_list->_Path.Size);
    for (const auto& point : draw_list->_Path)
        polyline.Points.push_back(point);

    draw_list->_Path.clear();
}

static void ToolbarAndTabs()
{
    bool select_tab = ImGui::IsWindowAppearing();


    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Method:");
        ImGui::SameLine();
        const auto value_changed = ComboBox("##Method",
            state.Method,
            {
                { "Upstream",           Method::Upstream     },
                { "PR2964",             Method::PR2964       },
                { "New",                Method::New          },
                { "New (optimized)",    Method::NewOptimized },
                { "New V3",             Method::NewV3        },
                { "Polyline2D",         Method::Polyline2D   },
                { "Allegro",            Method::Allegro      },
                { "Clipper2",           Method::Clipper2     }
            }
        );
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    {
    using enum ImGuiEx::ImDrawFlagsExtra_;

        ImGui::TextUnformatted("Cap:");
        ImGui::SameLine();
        const auto value_changed = ComboBox("##Cap",
            state.LineCap,
            {
                { "None",       ImDrawFlags_CapNone   },
                { "Butt",       ImDrawFlags_CapButt   },
                { "Square",     ImDrawFlags_CapSquare },
                { "Round",      ImDrawFlags_CapRound  }
            }
        );
        if (value_changed)
            ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    {
        using enum ImGuiEx::ImDrawFlagsExtra_;

        ImGui::TextUnformatted("Join:");
        ImGui::SameLine();
        const auto value_changed = ComboBox("##Join",
            state.LineJoin,
            {
                { "Miter",      ImDrawFlags_JoinMiter      },
                { "Miter Clip", ImDrawFlags_JoinMiterClip  },
                { "Bevel",      ImDrawFlags_JoinBevel      },
                { "Round",      ImDrawFlags_JoinRound      }
            }
        );
        if (value_changed)
            ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    {
        ImGui::TextUnformatted("Miter Limit:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::DragFloat("##MiterLimit", &state.MiterLimit, 0.05f, 0.0f, 200.0f))
            ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    {
        ImGui::TextUnformatted("Stress:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::DragInt("##Stress", &state.Stress, 1, 1, 1000))
            ImGui::MarkIniSettingsDirty();
    }

    ImGui::Separator();

    if (ImGui::Button("Add"))
    {
        state.Polylines.push_back(make_unique<Polyline>());
        state.SetCurrent(static_cast<int>(state.Polylines.size()) - 1);
        select_tab = true;
        ImFormatString(state.Current->Name, IM_ARRAYSIZE(state.Current->Name), "Polyline %d", state.Polylines.size());
        SetPolyline(*state.Current, state.Template);
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.Current == nullptr);
    if (ImGui::Button("Clone"))
    {
        auto current_index = state.Index(state.Current);
        auto& polyline = *state.Current;
        state.Polylines.insert(state.Polylines.begin() + current_index + 1, make_unique<Polyline>(polyline));
        state.SetCurrent(current_index + 1);
        select_tab = true;
        ImFormatString(state.Current->Name, IM_ARRAYSIZE(state.Current->Name), "%s (Clone)", polyline.Name);
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
    {
        auto selectedIndex = state.Index(state.Current);
        state.Polylines.erase(state.Polylines.begin() + state.Index(state.Current));
        state.SetCurrent(selectedIndex);
        select_tab = true;
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        state.Polylines.clear();
        state.SetCurrent(nullptr);
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (ImGui::Checkbox("Edit", &state.EnableEdit))
        ImGui::MarkIniSettingsDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Points", &state.ShowPoints))
        ImGui::MarkIniSettingsDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Lines", &state.ShowLines))
        ImGui::MarkIniSettingsDirty();
    ImGui::SameLine();
    if (ImGui::Checkbox("Show Mesh", &state.ShowMesh))
        ImGui::MarkIniSettingsDirty();

    ImGui::EndDisabled();

    if (ImGui::BeginTabBar("PolylineTabs"))
    {
        ImGuiTextBuffer tab_label;
        for (auto& polyline : state.Polylines)
        {
            tab_label.clear();
            tab_label.appendf("%s###%p", polyline->Name, polyline.get());

            int flags = ImGuiTabItemFlags_None;
            if (select_tab && state.Current == polyline.get())
                flags |= ImGuiTabItemFlags_SetSelected;

            if (ImGui::BeginTabItem(tab_label.c_str(), nullptr, flags))
            {
                if (!select_tab || (flags & ImGuiTabItemFlags_SetSelected))
                    state.SetCurrent(polyline.get());
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
}

static void EditCanvas()
{
    constexpr float point_radius            = 5.0f;
    constexpr auto  point_color             = IM_COL32(  0, 255,   0, 255);
    constexpr auto  point_color_current     = IM_COL32( 86, 192, 255, 255);
    constexpr auto  point_color_last        = IM_COL32(255, 255,   0, 255);
    constexpr auto  point_thickness         = 1.0f;
    constexpr auto  point_thickness_current = 2.0f;

    auto draw_list = ImGui::GetWindowDrawList();

    if (!state.Canvas.Begin("Canvas", ImGui::GetContentRegionAvail()))
        return;

    if (!state.Current)
    {
        state.Canvas.End();
        draw_list->AddRect(state.Canvas.Rect().Min, state.Canvas.Rect().Max, ImGui::GetColorU32(ImGuiCol_Border));
        return;
    }

    auto storage = ImGui::GetStateStorage();

    const auto last_canvas_size_x_id = ImGui::GetID("LastCanvasSizeX");
    const auto last_canvas_size_y_id = ImGui::GetID("LastCanvasSizeY");
    const auto last_canvas_size = ImVec2(storage->GetFloat(last_canvas_size_x_id, -1), storage->GetFloat(last_canvas_size_y_id, -1));
    const auto canvas_size = state.Canvas.Rect().GetSize();
    const auto canvas_size_changed = last_canvas_size != canvas_size;
    storage->SetFloat(last_canvas_size_x_id, canvas_size.x);
    storage->SetFloat(last_canvas_size_y_id, canvas_size.y);

    auto& last_polyline_index = *storage->GetIntRef(ImGui::GetID("LastPolylineIndex"), -1);
    const auto polyline_index = state.Index(state.Current);
    const auto polyline_changed = last_polyline_index != polyline_index;
    last_polyline_index = polyline_index;

    auto& polyline = *state.Current;

    auto viewRectMin = ImSmoothVariable<ImVec2>(ImGui::GetID("ViewRectMin"));
    auto viewRectMax = ImSmoothVariable<ImVec2>(ImGui::GetID("ViewRectMax"));

    if (polyline_changed)
    {
        if (auto viewRectSize = polyline.ViewRect.GetSize(); viewRectSize.x > 0.0f && viewRectSize.y > 0.0f)
        {
            auto centeredViewRect = polyline.ViewRect;
            centeredViewRect.Min.x = centeredViewRect.Max.x = centeredViewRect.GetCenter().x;
            state.Canvas.SetView({}, 0.0f);
            state.Canvas.CenterView(centeredViewRect);
            polyline.View = state.Canvas.View();
        }
        else
        {
            state.Canvas.SetView(polyline.View);
        }
        auto viewRect = state.Canvas.ViewRect();
        polyline.ViewRect = viewRect;
        viewRectMin.Reset(viewRect.Min);
        viewRectMax.Reset(viewRect.Max);
    }
    else if (canvas_size_changed && !ImGui::IsWindowAppearing() && last_canvas_size.y > 0.0f)
    {
        auto verticalScale = canvas_size.y / last_canvas_size.y;
        auto viewRect      = state.Canvas.CalcViewRect(polyline.View, last_canvas_size);

        state.Canvas.SetView(state.Canvas.ViewOrigin(), state.Canvas.ViewScale() * verticalScale);
        state.Canvas.CenterView(viewRect.GetCenter());

        polyline.View = state.Canvas.View();
        viewRect = state.Canvas.CalcViewRect(polyline.View);
        polyline.ViewRect = viewRect;
        viewRectMin.Reset(viewRect.Min);
        viewRectMax.Reset(viewRect.Max);
    }

    struct Action
    {
        ImGuiKeyChord       KeyChord;
        const char* const   Name;
        bool                Active = false;
    };
    vector<Action> actions;

    bool any_action_fired = false;
    auto action = [&actions, &any_action_fired](ImGuiKeyChord keyChord, const char* action, bool active) -> bool
    {
        actions.push_back({ keyChord, action, active });
        if (!ImGui::IsItemHovered() || !active || any_action_fired)
            return false;
        auto flags = (keyChord & ~ImGuiMod_Mask_) ? ImGuiInputFlags_RouteGlobal : ImGuiInputFlags_None;
        auto result = ImGui::Shortcut(keyChord, flags);
        any_action_fired |= result;
        return result;
    };

    const auto drag_threshold = 1.0f / state.Canvas.ViewScale();
    const auto drag_button = ImGui::IsMouseDragging(1, drag_threshold) ? ImGuiKey_MouseRight : (ImGui::IsMouseDragging(0, drag_threshold) ? ImGuiKey_MouseLeft : ImGuiKey_None);
    const auto drag_delta  = drag_button == ImGuiKey_MouseRight ? ImGui::GetMouseDragDelta(1, drag_threshold) : (drag_button == ImGuiKey_MouseLeft ? ImGui::GetMouseDragDelta(0, drag_threshold) : ImVec2{});
    const auto last_drag_button = polyline.DragButton;

    bool drag_begin  = false;
    bool drag_update = false;
    bool drag_end    = false;
    if (ImGui::IsItemHovered() && (polyline.DragButton == ImGuiKey_None && drag_button != ImGuiKey_None))
    {
        polyline.DragButton = drag_button;
        polyline.DragStart  = ImGui::GetMousePos()  - drag_delta;
        drag_begin = true;
    }
    else if (polyline.DragButton != ImGuiKey_None && drag_button == ImGuiKey_None)
    {
        polyline.DragButton = ImGuiKey_None;
        drag_end = true;
    }
    else if (polyline.DragButton != ImGuiKey_None && drag_button == polyline.DragButton)
    {
        drag_update = true;
    }

    const auto is_dragging = drag_begin || drag_update;

    const auto is_panning  = is_dragging && (drag_button == ImGuiKey_MouseRight);
    const auto was_panning = drag_end && (last_drag_button == ImGuiKey_MouseRight);

    if (action(ImGuiKey_MouseRight, "Pan", !is_dragging || is_panning) || is_panning)
    {
        state.Canvas.SetView(polyline.View.Origin + drag_delta * state.Canvas.ViewScale(), state.Canvas.ViewScale());
    }
    else if (was_panning)
    {
        polyline.View = state.Canvas.View();
        auto viewRect = state.Canvas.CalcViewRect(polyline.View);
        polyline.ViewRect = viewRect;
        viewRectMin.Reset(viewRect.Min);
        viewRectMax.Reset(viewRect.Max);
    }
    if (action(ImGuiKey_MouseWheelY, "Zoom", !is_panning))
    {
        auto& io = ImGui::GetIO();
        auto mousePos     = io.MousePos;

        // apply new view scale
        auto oldView      = polyline.View;
        auto newViewScale = oldView.Scale * powf(1.2f, io.MouseWheel);
        state.Canvas.SetView(oldView.Origin, newViewScale);
        auto newView      = state.Canvas.View();

        // calculate origin offset to keep mouse position fixed
        auto screenPosition = state.Canvas.FromLocal(mousePos, oldView);
        auto canvasPosition = state.Canvas.ToLocal(screenPosition, newView);
        auto originOffset   = (canvasPosition - mousePos) * newViewScale;

        // apply new view
        state.Canvas.SetView(oldView.Origin + originOffset, newViewScale);

        polyline.View = state.Canvas.View();
        auto viewRect = state.Canvas.CalcViewRect(polyline.View);
        polyline.ViewRect = viewRect;
    }

    if (!is_panning)
    {
        auto viewRect = state.Canvas.CalcViewRect(polyline.View);
        polyline.ViewRect = viewRect;
        auto currentViewRect = ImRect(
            viewRectMin.Next(viewRect.Min, ImGui::GetIO().DeltaTime),
            viewRectMax.Next(viewRect.Max, ImGui::GetIO().DeltaTime)
        );

        state.Canvas.CenterView(currentViewRect);
    }


    if (state.EnableEdit)
    {
        constexpr auto  closest_point_on_segment_max_distance = 5.0f;

        ImGui::SetCursorScreenPos(state.Canvas.ViewRect().Min);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##canvas", state.Canvas.ViewRect().GetSize());

        auto is_dragging_point                  = is_dragging && (drag_button == ImGuiKey_MouseLeft);
        auto current_point                      = polyline.CurrentPoint;
        auto hovered_point                      = is_dragging_point ? current_point : -1;
        auto closest_segment_index              = -1;
        auto closest_segment                    = Segment{};
        auto closest_point_on_segment           = ImVec2{};
        auto closest_point_on_segment_distance  = FLT_MAX;

        if (hovered_point < 0)
        {
            auto screen_point_radius     = point_radius / state.Canvas.ViewScale();
            auto screen_point_radius_sqr = screen_point_radius * screen_point_radius;
            polyline.ForEachPoint([&, mouse_pos = ImGui::GetMousePos()](const ImVec2& p)
                {
                    auto distance = ImSqrt(ImLengthSqr(mouse_pos - p));
                    if (distance < screen_point_radius)
                        hovered_point = polyline.Index(p);
                }
            );
        }

        if (closest_segment_index < 0)
        {
            polyline.ForEachSegment([&, mouse_pos = ImGui::GetMousePos()](const ImVec2& p0, const ImVec2& p1, int index)
                {
                    auto point_on_segment = ImLineClosestPoint(p0, p1, mouse_pos);

                    auto distance = ImLengthSqr(mouse_pos - point_on_segment);
                    if (distance < closest_point_on_segment_distance)
                    {
                        closest_segment_index             = index;
                        closest_segment                   = { p0, p1 };
                        closest_point_on_segment          = point_on_segment;
                        closest_point_on_segment_distance = distance;
                    }
                }
            );

            if (closest_segment_index >= 0)
                closest_point_on_segment_distance = ImSqrt(closest_point_on_segment_distance);

            if (closest_point_on_segment_distance > closest_point_on_segment_max_distance / state.Canvas.ViewScale())
                closest_segment_index = -1;
        }

        if (action(ImGuiKey_MouseLeft, "Drag Hovered Point", (hovered_point >= 0) || is_dragging_point))
        {
            polyline.CurrentPoint = hovered_point;
            polyline.DragStart    = polyline.Points[hovered_point];
        }
        else if (is_dragging_point && current_point >= 0 && ImGui::IsItemHovered())
        {
            auto viewRect = state.Canvas.ViewRect();
            polyline.Points[current_point] = ImClamp(polyline.DragStart + drag_delta, viewRect.Min, viewRect.Max);
            //polyline.Points[current_point] = ImFloor(polyline.Points[current_point]);

            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

            //ImGui::SetWindowFontScale(0.75f);
            //ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
            //ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
            //ImGui::SetNextWindowBgAlpha(0.75f);
            //ImGui::SetTooltip("(%.0f, %.0f)", point.x, point.y);
            //ImGui::PopStyleVar(2);
            //ImGui::SetWindowFontScale(1.0f);
        }
        else if (drag_end && drag_button == ImGuiKey_MouseLeft)
        {
            ImGui::MarkIniSettingsDirty();
        }
        if (action(ImGuiKey_MouseLeft, "Select Point", (hovered_point >= 0) && !is_dragging_point))
        {
            polyline.CurrentPoint = hovered_point;
        }
        if (action(ImGuiKey_MouseLeft, "Deselect Point", (hovered_point < 0) && (current_point >= 0) && !is_dragging_point))
        {
            polyline.CurrentPoint = -1;
        }
        if (action(ImGuiKey_MouseLeft | ImGuiMod_Shift, "Add Point", !is_dragging_point && (closest_segment_index < 0)))
        {
            auto mouse_pos = ImGui::GetMousePos();
            polyline.Points.push_back({ mouse_pos });
            polyline.CurrentPoint = static_cast<int>(polyline.Points.size()) - 1;
            ImGui::MarkIniSettingsDirty();
        }
        if (action(ImGuiKey_MouseLeft | ImGuiMod_Shift, "Add Point On Segment", !is_dragging_point && (closest_segment_index >= 0)))
        {
            polyline.Points.insert(polyline.Points.begin() + closest_segment_index + 1, closest_point_on_segment);
            polyline.CurrentPoint = closest_segment_index + 1;
            ImGui::MarkIniSettingsDirty();
        }
        if (action(ImGuiKey_MouseLeft | ImGuiMod_Ctrl, "Remove Point", hovered_point >= 0))
        {
            polyline.Points.erase(polyline.Points.begin() + hovered_point);
            polyline.CurrentPoint = -1;
            ImGui::MarkIniSettingsDirty();
        }


        state.Canvas.Suspend();

        draw_list->PushClipRect(state.Canvas.Rect().Min, state.Canvas.Rect().Max);

        if (closest_segment_index >= 0)
        {
            const auto screen_closest_point_on_segment_distance = closest_point_on_segment_distance * state.Canvas.ViewScale();

            const auto alpha = static_cast<int>(196.0f * ImClamp(1.0f - screen_closest_point_on_segment_distance / closest_point_on_segment_max_distance, 0.0f, 1.0f));

            //draw_list->AddCircleFilled(canvas_min + closest_point_on_segment, point_radius, IM_COL32(255, 255, 0, 255));
            draw_list->AddCircle(state.Canvas.FromLocal(closest_point_on_segment), point_radius, IM_COL32(255, 255, 0, alpha), 0, point_thickness);
        }

        draw_list->PopClipRect();

        state.Canvas.Resume();
    }

    //if ((isDragging || ImGui::IsItemHovered()) && ImGui::IsMouseDragging(1, 0.0f))
    //{
    //    if (!isDragging)
    //    {
    //        isDragging = true;
    //        drawStartPoint = viewOrigin;
    //    }

    //    canvas.SetView(drawStartPoint + ImGui::GetMouseDragDelta(1, 0.0f) * viewScale, viewScale);
    //}
    //else if (isDragging)
    //{
    //    isDragging = false;
    //}
    //else if (!isDragging && ImGui::IsItemHovered() && io.MouseWheel)
    //{
    //    auto mousePos     = io.MousePos;

    //    // apply new view scale
    //    auto oldView      = canvas.View();
    //    auto newViewScale = viewScale * powf(1.1f, io.MouseWheel);
    //    canvas.SetView(viewOrigin, newViewScale);
    //    auto newView      = canvas.View();

    //    // calculate origin offset to keep mouse position fixed
    //    auto screenPosition = canvas.FromLocal(mousePos, oldView);
    //    auto canvasPosition = canvas.ToLocal(screenPosition, newView);
    //    auto originOffset   = (canvasPosition - mousePos) * newViewScale;

    //    // apply new view
    //    canvas.SetView(viewOrigin + originOffset, newViewScale);
    //}

    auto last_fringe_scale = draw_list->_FringeScale;

    draw_list->_FringeScale = 1.0f;// / ImGui::GetIO().DisplayFramebufferScale.x;

    auto stats = polyline.Draw(draw_list, {}, state.Method, state.Stress);

    draw_list->_FringeScale = last_fringe_scale;

    if (state.ShowMesh)
    {
        auto draw_cmd               = &draw_list->CmdBuffer.back();
        auto draw_cmd_elements      = draw_cmd->ElemCount;
        auto draw_cmd_index_offset  = draw_cmd->IdxOffset;
        auto draw_cmd_vertex_offset = draw_cmd->VtxOffset;

        state.Canvas.Suspend();

        draw_list->PushClipRect(state.Canvas.Rect().Min, state.Canvas.Rect().Max);

        const auto color = IM_COL32(255, 255, 0, 255);
        const auto thickness = 1.0f / ImGui::GetIO().DisplayFramebufferScale.x;

        for (unsigned int i = 0; i < draw_cmd_elements; i += 6)
        {
            auto idx0 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 0];
            auto idx1 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 1];
            auto idx2 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 2];
            auto idx3 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 3];
            auto idx4 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 4];
            auto idx5 = draw_list->IdxBuffer[draw_cmd_index_offset + i + 5];

            auto& vertex0 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx0];
            auto& vertex1 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx1];
            auto& vertex2 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx2];
            auto& vertex3 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx3];
            auto& vertex4 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx4];
            auto& vertex5 = draw_list->VtxBuffer[draw_cmd_vertex_offset + idx5];

            auto p0 = ImVec2(vertex0.pos.x, vertex0.pos.y);
            auto p1 = ImVec2(vertex1.pos.x, vertex1.pos.y);
            auto p2 = ImVec2(vertex2.pos.x, vertex2.pos.y);
            auto p3 = ImVec2(vertex3.pos.x, vertex3.pos.y);
            auto p4 = ImVec2(vertex4.pos.x, vertex4.pos.y);
            auto p5 = ImVec2(vertex5.pos.x, vertex5.pos.y);

            draw_list->PathLineTo(p0); draw_list->PathLineTo(p1); draw_list->PathStroke(color, false, thickness);
            draw_list->PathLineTo(p1); draw_list->PathLineTo(p2); draw_list->PathStroke(color, false, thickness);
            draw_list->PathLineTo(p2); draw_list->PathLineTo(p0); draw_list->PathStroke(color, false, thickness);
            
            draw_list->PathLineTo(p3); draw_list->PathLineTo(p4); draw_list->PathStroke(color, false, thickness);
            draw_list->PathLineTo(p4); draw_list->PathLineTo(p5); draw_list->PathStroke(color, false, thickness);
            draw_list->PathLineTo(p5); draw_list->PathLineTo(p3); draw_list->PathStroke(color, false, thickness);
        }

        draw_list->PopClipRect();

        state.Canvas.Resume();
    }

    state.Canvas.Suspend();

    draw_list->PushClipRect(state.Canvas.Rect().Min, state.Canvas.Rect().Max);

    if (state.ShowLines)
    {
        polyline.ForEachSegment(
            [&](const ImVec2& p0, const ImVec2& p1)
            {
                draw_list->AddLine(state.Canvas.FromLocal(p0), state.Canvas.FromLocal(p1), IM_COL32(255, 0, 0, 255), 1.0f);
            }
        );
    }

    if (state.ShowPoints)
    {
        polyline.ForEachPoint(
            [&](const ImVec2& point_, PolylinePointFlags flags)
            {
                auto point      = state.Canvas.FromLocal(point_);

                auto color      = (flags & PolylinePointFlags_Last   ) ? point_color_last : point_color;
                auto fill_color = ((flags & PolylinePointFlags_First)) ? point_color      : IM_COL32_BLACK_TRANS;
                auto thickness  = point_thickness;

                draw_list->AddCircleFilled(point, point_radius, fill_color);
                draw_list->AddCircle(point, point_radius, color, 0, point_thickness);
            }
        );
    }

    if (state.EnableEdit)
    {
        if (polyline.CurrentPoint >= 0)
        {
            auto point = state.Canvas.FromLocal(polyline.Points[polyline.CurrentPoint]);

            //draw_list->AddCircleFilled(point, point_radius, IM_COL32(255, 255, 0, 255));
            draw_list->AddCircle(point, point_radius + 2.0f, point_color_current, 0, point_thickness);
        }
    }

    draw_list->PopClipRect();

    state.Canvas.Resume();

# if 0
    auto flags = draw_list->Flags;
    //draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill);

    float rounding = 20.0f;
    float half_rounding = rounding * 0.5f;
    float inner_rounding = half_rounding;

    ImVec2 p0 = ImVec2(50, 50);
    ImVec2 p1 = ImVec2(400, 200);
    ImVec2 half_pixel = ImVec2(0.50f, 0.50f);
    ImVec2 half_rounding_vec = ImVec2(half_rounding, half_rounding);

    draw_list->AddRectFilled(p0, p1, IM_COL32(102, 182, 102, 200), rounding);

    draw_list->AddRect(
        p0 - half_pixel + half_rounding_vec,
        p1 + half_pixel - half_rounding_vec,
        IM_COL32(255, 255, 0, 200),
        inner_rounding, 0, rounding);

    draw_list->Flags = flags;
# endif

    state.Canvas.End();

    auto canvas_min = ImGui::GetItemRectMin();
    auto canvas_max = ImGui::GetItemRectMax();

    ImGui::SetWindowFontScale(0.75f);

    {
        auto action_list_height = actions.size() * ImGui::GetTextLineHeightWithSpacing();
        auto action_list_pos = ImVec2(canvas_max.x - ImGui::GetStyle().ItemSpacing.x, canvas_max.y - action_list_height - ImGui::GetStyle().ItemSpacing.y);
        ImGui::SetCursorScreenPos(action_list_pos);
        ImGui::BeginGroup();
        auto action_list_x = ImGui::GetCursorPosX();
        for (const auto& action : actions)
        {
            ImGuiTextBuffer text;
            text.appendf("%s: %s", action.Name, ImGui::GetKeyChordName(action.KeyChord));

            const auto text_size = ImGui::CalcTextSize(text.c_str());

            ImGui::BeginDisabled(!action.Active);
            ImGui::SetCursorPosX(action_list_x - text_size.x);
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndDisabled();
        }
        ImGui::EndGroup();
    }

    {
        auto info_pos = canvas_min + ImGui::GetStyle().ItemSpacing;
        ImGui::SetCursorScreenPos(info_pos);
        ImGui::BeginGroup();
        auto canvas_mouse_pos = ImGui::GetMousePos() - canvas_min;
        ImGui::Text("Canvas: (%.0f, %.0f)", canvas_size.x, canvas_size.y);
        ImGui::Text("Mouse: (%.0f, %.0f)", canvas_mouse_pos.x, canvas_mouse_pos.y);

        //ImGui::Text("Drag Begin: %s", drag_begin ? "true" : "false");
        //ImGui::Text("Drag Update: %s", drag_update ? "true" : "false");
        //ImGui::Text("Drag End: %s", drag_end ? "true" : "false");

        //ImGui::Text("Panning: %s", is_panning ? "true" : "false");
        //ImGui::Text("Was Panning: %s", was_panning ? "true" : "false");
        //ImGui::Text("Dragging: %s", is_dragging ? "true" : "false");

        //ImGui::Text("View Origin: (%.0f, %.0f)", polyline.View.Origin.x, polyline.View.Origin.y);
        //ImGui::Text("View Scale: %.2f", polyline.View.Scale);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Zoom:");
        ImGui::SameLine();
        ImGui::PushItemWidth(100.0f);
        bool center = false;
        if (ImGui::DragFloat("##Zoom", &polyline.View.Scale, 0.05f, 0.0f, 100.0f))
        {
            polyline.View.InvScale = 1.0f / polyline.View.Scale;
            auto viewRect = state.Canvas.CalcViewRect(polyline.View);
            polyline.ViewRect = viewRect;
            ImGui::MarkIniSettingsDirty();
            center = true;
        }
        ImGui::PopItemWidth();

        ImGui::PushItemWidth(100.0f);
        center |= ImGui::Button("Center");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        bool fit_to_content = ImGui::Button("Fit to Content");
        ImGui::SameLine();
        bool fit_to_grid = ImGui::Button("1:1");

        if (fit_to_content || center || fit_to_grid)
        {
            auto bounds = polyline.Bounds();
            if (bounds.GetWidth() > 0.0f && bounds.GetHeight() > 0.0f)
            {
                auto last_view = state.Canvas.View();
                state.Canvas.SetView(polyline.View);
                if (fit_to_grid)
                    state.Canvas.SetView({}, 1.0f);
                if (fit_to_content)
                {
                    const float view_rect_margin = 0.1f;
                    bounds.Expand(ImVec2(bounds.GetWidth() * view_rect_margin, bounds.GetHeight() * view_rect_margin));
                    state.Canvas.CenterView(bounds);
                }
                else
                    state.Canvas.CenterView(bounds.GetCenter());
                polyline.View = state.Canvas.View();
                polyline.ViewRect = state.Canvas.ViewRect();
                ImGui::MarkIniSettingsDirty();
            }
        }

        ImGui::Spacing();

        ImGui::Text("Performance:");
        ImGui::Indent();

        auto find_best_time_unit = [](double durationInSeconds) -> std::pair<double, const char*>
        {
            if (durationInSeconds < 1.0e-6)
                return { 1.0e-9, "ns (e-9)" };
            if (durationInSeconds < 1.0e-3)
                return { 1.0e-6, "us (e-6)" };
            if (durationInSeconds < 1.0)
                return { 1.0e-3, "ms (e-3)" };
            return { durationInSeconds, "s" };
        };

        state.DrawDuration.Add(stats.Duration);
        state.DrawDurationAvg.Add(stats.DurationAvg);

        //auto value_getter = [](void* data, int idx) -> float
        //{
        //    auto& values = *static_cast<decltype(state.Duration)*>(data);
        //    return values.Count ? values.Values[(idx + values.Index) % values.Count] : 0.0;
        //};

        //auto [duration_unit_base, duration_unit_name] = find_best_time_unit(state.DrawDuration.CachedValue);
        //ImGui::Text("Duration: %.2f%s", state.DrawDuration.CachedValue / duration_unit_base, duration_unit_name);
        //ImGui::PlotLines("##Duration", value_getter, &state.Duration, state.Duration.Count, 0.0f, nullptr, 0.0f, 100.0f / duration_scale, ImVec2(200, 40.0f));
        auto [draw_duration_avg_unit_base, draw_duration_avg_unit_name] = find_best_time_unit(state.DrawDurationAvg.CachedValue);
        ImGui::Text("Duration (average): %.2f%s", state.DrawDurationAvg.CachedValue / draw_duration_avg_unit_base, draw_duration_avg_unit_name);
        ImGui::Text("Iterations: %d", stats.Iterations);
        ImGui::Unindent();

        ImGui::Spacing();

        ImGui::Text("Geometry:");
        ImGui::Indent();
        ImGui::Text("Elements: %d", stats.Elements);
        ImGui::Text("Vertices: %d", stats.Vertices);
        ImGui::Text("Indices: %d", stats.Indices);
        ImGui::Unindent();

        ImGui::Spacing();

        if (state.Current && state.EnableEdit)
        {
            auto& polyline = *state.Current;

            ImGui::Text("Points: %d", static_cast<int>(polyline.Points.size()));

            ImGui::Text("Current Point: %d", polyline.CurrentPoint);
            if (polyline.CurrentPoint >= 0)
            {
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::Text(" (%.0f, %.0f)", polyline.Points[polyline.CurrentPoint].x, polyline.Points[polyline.CurrentPoint].y);
            }
        }
        ImGui::EndGroup();
    }

    ImGui::SetWindowFontScale(0.5f);

    {
        const auto axis_size = 80.0f;
        const auto arrow_width = 2.5f;
        const auto arrow_length = 7.5f;
        const auto axis_label_color = ImGui::GetColorU32(ImGuiCol_Text);

        auto axis_pos = ImVec2(canvas_min.x + ImGui::GetStyle().ItemSpacing.x, canvas_max.y - ImGui::GetStyle().ItemSpacing.x);

        draw_list->AddLine(axis_pos, axis_pos + ImVec2(axis_size, 0), IM_COL32(255, 0, 0, 255), 1.0f);
        draw_list->AddLine(axis_pos, axis_pos - ImVec2(0, axis_size), IM_COL32(0, 255, 0, 255), 1.0f);

        draw_list->AddText(axis_pos + ImVec2(axis_size + arrow_length,-ImGui::GetTextLineHeight()), axis_label_color, "X");
        draw_list->AddText(axis_pos - ImVec2(0, axis_size + arrow_length + ImGui::GetTextLineHeight()), axis_label_color, "Y");

        axis_pos += ImVec2(0.5f, 0.5f);

        draw_list->AddTriangleFilled(axis_pos + ImVec2(axis_size + arrow_length, 0), axis_pos + ImVec2(  axis_size, arrow_width), axis_pos + ImVec2(   axis_size, -arrow_width), IM_COL32(255, 0, 0, 255));
        draw_list->AddTriangleFilled(axis_pos - ImVec2(0, axis_size + arrow_length), axis_pos + ImVec2(arrow_width,  -axis_size), axis_pos + ImVec2(-arrow_width,   -axis_size), IM_COL32(0, 255, 0, 255));


        const auto min_unit_density = 10.0f;
        const auto max_unit_step = 2.5f;
        auto unit = 1.0f * state.Canvas.ViewScale();
        while ((unit - min_unit_density) < max_unit_step)
            unit *= max_unit_step;

        for (float x = unit; x < axis_size; x += unit)
        {
            auto p0 = axis_pos + ImVec2(x,  0);
            auto p1 = axis_pos + ImVec2(x, -5);
            draw_list->AddLine(p0, p1, IM_COL32(255, 0, 0, 255), 1.0f);
            ImGuiTextBuffer label;
            label.appendf("%.0f", x / state.Canvas.ViewScale());
            draw_list->AddText(p0 + ImVec2(0, -5 - ImGui::GetTextLineHeight()), axis_label_color, label.c_str());
        }

        for (float y = 0.0f; y < axis_size; y += unit)
        {
            auto p0 = axis_pos + ImVec2(0, -y);
            auto p1 = axis_pos + ImVec2(5, -y);
            draw_list->AddLine(p0, p1, IM_COL32(0, 255, 0, 255), 1.0f);
        }
    }

    ImGui::SetWindowFontScale(1.0f);

    draw_list->AddRect(state.Canvas.Rect().Min, state.Canvas.Rect().Max, ImGui::GetColorU32(ImGuiCol_Border));
}


static void PlaygroundWindow()
{
    if (!ImGui::Begin("Polyline Playground", nullptr, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::End();
        return;
    }

    ToolbarAndTabs();

    EditCanvas();

    ImGui::End();
}

static void PolylineWindow()
{
    if (!state.Current)
        return;

    auto& polyline = *state.Current;

    ImGuiTextBuffer window_label;
    window_label.appendf("%s###polygon_window", polyline.Name);

    if (!ImGui::Begin(window_label.c_str()))
    {
        ImGui::End();
        return;
    }

    auto storage = ImGui::GetStateStorage();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Template:");
    ImGui::SameLine();
    {
        const auto value_changed = ComboBox("##PolylineContentSelector",
            state.Template,
            {
                { "Empty",                       PolylineTemplate::Empty                  },
                { "Rect (Stroke)",               PolylineTemplate::RectStroke             },
                { "Rect (Stroke Thick)",         PolylineTemplate::RectStrokeThick        },
                { "Rect Rounded (Stroke)",       PolylineTemplate::RectRoundedStroke      },
                { "Rect Rounded (Stroke Thick)", PolylineTemplate::RectRoundedStrokeThick },
                { "Circle (Stroke)",             PolylineTemplate::CircleStroke           },
                { "Circle (Stroke Thick)",       PolylineTemplate::CircleStrokeThick      },
                { "Triangle (Stroke)",           PolylineTemplate::TriangleStroke         },
                { "Triangle (Stroke Thick)",     PolylineTemplate::TriangleStrokeThick    },
                { "Long (Stroke)",               PolylineTemplate::LongStroke             },
                { "Long (Stroke Thick)",         PolylineTemplate::LongStrokeThick        },
                { "Long Jagged (Stroke)",        PolylineTemplate::LongJaggedStroke       },
                { "Long Jagged (Stroke Thick)",  PolylineTemplate::LongJaggedStrokeThick  },
                { "Line (Stroke)",               PolylineTemplate::LineStroke             },
                { "Line (Stroke Thick)",         PolylineTemplate::LineStrokeThick        }
            }
        );

        if (value_changed)
            ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Set"))
    {
        SetPolyline(polyline, state.Template);
        ImGui::MarkIniSettingsDirty();

        auto centeredViewRect = polyline.Bounds();
        if (centeredViewRect.GetWidth() > 0.0f && centeredViewRect.GetHeight() > 0.0f)
        {
            const float view_rect_margin = 0.1f;
            centeredViewRect.Expand(ImVec2(centeredViewRect.GetWidth() * view_rect_margin, centeredViewRect.GetHeight() * view_rect_margin));
            auto last_view = state.Canvas.View();
            centeredViewRect.Min.x = centeredViewRect.Max.x = centeredViewRect.GetCenter().x;
            state.Canvas.SetView({}, 0.0f);
            state.Canvas.CenterView(centeredViewRect);
            polyline.View = state.Canvas.View();
            state.Canvas.SetView(last_view);
        }
    }

    ImGui::Separator();

    ImGui::BeginTable("PolylineProperties", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 0.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableNextColumn();
    ImGui::Text("Name");
    ImGui::TableNextColumn();
    ImGui::PushItemWidth(-FLT_MIN);
    if (ImGui::InputText("##Name", polyline.Name, sizeof(polyline.Name)))
        ImGui::MarkIniSettingsDirty();
    ImGui::PopItemWidth();
    ImGui::TableNextColumn();
    ImGui::Text("Color");
    ImGui::TableNextColumn();
    ImGui::PushItemWidth(-FLT_MIN);
    ImColor color = polyline.Color;
    if (ImGui::ColorEdit4("##Color", &color.Value.x))
    {
        polyline.Color = color;
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::PopItemWidth();
    ImGui::TableNextColumn();
    ImGui::Text("Thickness");
    ImGui::TableNextColumn();
    ImGui::PushItemWidth(-FLT_MIN);
    if (ImGui::DragFloat("##Thickness", &polyline.Thickness, 0.05f, 0.0f, 200.0f))
        ImGui::MarkIniSettingsDirty();
    ImGui::PopItemWidth();
    ImGui::TableNextColumn();
    ImGui::Text("Flags");
    ImGui::TableNextColumn();
    if (ImGui::CheckboxFlags("Closed", &polyline.Flags, PolylineFlags_Closed))
        ImGui::MarkIniSettingsDirty();
    ImGui::SameLine();
    if (ImGui::CheckboxFlags("Anti-aliased", &polyline.Flags, PolylineFlags_AntiAliased))
        ImGui::MarkIniSettingsDirty();
    ImGui::SameLine();
    if (ImGui::CheckboxFlags("Square Caps", &polyline.Flags, PolylineFlags_SquareCaps))
        ImGui::MarkIniSettingsDirty();
    ImGui::EndTable();

    auto last_current_point = storage->GetIntRef(ImGui::GetID("LastCurrentPoint"), -1);
    auto current_point_changed = *last_current_point != polyline.CurrentPoint;
    *last_current_point = polyline.CurrentPoint;

    ImGui::BeginTable("PolylinePoints", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(ImGui::GetContentRegionAvail().x, 0.0f));
    ImGui::TableSetupColumn("Point", ImGuiTableColumnFlags_WidthFixed, 0.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableHeadersRow();
    int point_index = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(1.0f, 0.5f));
    for (auto& point : polyline.Points)
    {
        ImGui::PushID(point_index++);
        ImGui::TableNextColumn();
        ImGuiTextBuffer label;
        label.appendf("%d", point_index);
        bool is_selected = polyline.CurrentPoint == point_index - 1;
        if (is_selected && current_point_changed)
            ImGui::SetScrollHereY();
        if (ImGui::Selectable(label.c_str(), is_selected, 0, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            polyline.CurrentPoint = is_selected ? -1 : point_index - 1;
            *last_current_point = polyline.CurrentPoint;
        }
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-FLT_MIN);
        if (ImGui::DragFloat2("##Point", &point.x, 0.5f))
            ImGui::MarkIniSettingsDirty();
        ImGui::PopItemWidth();
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndTable();

    ImGui::End();
}

void Playground()
{
    PlaygroundWindow();
    PolylineWindow();
}

} // namespace ImPolyline

ImGuiSettingsHandler* GetPolylinePlaygroundSettingsHandler()
{
    return &ImPolyline::state.SettingsHandler;
}

void PolylinePlayground()
{
    auto& style = ImGui::GetStyle();

    auto last_anti_aliased_lines = style.AntiAliasedLines;

    style.AntiAliasedLinesUseTex = false;

    ImPolyline::Playground();

    style.AntiAliasedLines = last_anti_aliased_lines;
}
