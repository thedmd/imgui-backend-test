#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_canvas.h>

#include "polyline_new.h"
#include "polyline_upstream.h"
#include "polyline_pr2964.h"

#include <utility>
#include <vector>
#include <memory>

struct ImMeshCaptureInfo
{
    unsigned int ElementCount = 0;
    int          VtxCount     = 0;
    int          IdxCount     = 0;
};

struct ImMeshCapture
{
    struct Line { ImVec2 P0, P1; };

    void Begin(ImDrawList* draw_list);
    void End();
    void Rewind();
    void Capture();
    void Draw(ImDrawList* draw_list, ImU32 color, float thickness = 1.0f);
    auto Info() const -> ImMeshCaptureInfo;

private:
    struct State
    {
        int          CmdCount         = 0;
        unsigned int ElementCount     = 0;
        ptrdiff_t    VtxWriteStart    = 0;
        ptrdiff_t    IdxWriteStart    = 0;
    };

    auto CaptureState() const -> State;

    ImDrawList*  m_DrawList         = nullptr;

    int          m_VtxBufferSize    = 0;
    int          m_IdxBufferSize    = 0;
    unsigned int m_VtxCurrentIdx    = 0;
    State        m_Begin;
    State        m_End;

    ImVector<Line> m_LineCache;
};

template <size_t N = 60, typename T = float>
struct ImRollingAverageValue
{
    T     Values[N] = {};
    int   Index = 0;
    int   Count = 0;

    T CachedValue = 0;

    void Add(T value)
    {
        Values[Index] = value;
        Index = (Index + 1) % N;
        if (Count < N)
            ++Count;

        if (Index == 0)
            CachedValue = Get();
    }

    T Get() const
    {
        if (Count == 0)
            return 0;

        T sum = 0;
        for (int i = 0; i < Count; ++i)
            sum += Values[i];
        return sum / Count;
    }
};

namespace ImPolyline {

using std::vector;
using std::unique_ptr;
using std::make_unique;

enum class Method
{
    Upstream,
    UpstreamNoTex,
    PR2964,
    New,
    NewOptimized,
    NewV3,
    Polyline2D,
    Allegro,
    Clipper2
};

using PolylineFlags = int;

enum PolylineFlags_
{
    PolylineFlags_None        = 0,
    PolylineFlags_Closed      = 1 << 0,
    PolylineFlags_AntiAliased = 1 << 1,
    PolylineFlags_SquareCaps  = 1 << 2,
};

using PolylinePointFlags = int;

enum PolylinePointFlags_
{
    PolylinePointFlags_None    = 0,
    PolylinePointFlags_First   = 1 << 0,
    PolylinePointFlags_Last    = 1 << 1,
    PolylinePointFlags_Current = 1 << 2,
};

struct Segment
{
    ImVec2 P0;
    ImVec2 P1;
};

struct Polyline
{
    char                Name[64] = {};
    vector<ImVec2>      Points;
    ImU32               Color = IM_COL32_WHITE;
    float               Thickness = 1.0f;
    PolylineFlags       Flags = PolylineFlags_None;

    ImRect              ViewRect;
    ImGuiEx::CanvasView View;  

    int                 CurrentPoint = -1;
    ImVec2              DragStart;
    ImGuiKey            DragButton = ImGuiKey_None;

    bool                FitToContent = false;

    int Index(const ImVec2* point) const noexcept
    {
        auto data    = Points.data();
        auto dataEnd = data + Points.size();
        if (point < data || point >= dataEnd)
            return -1;

        return static_cast<int>(point - data);
    }

    int Index(const ImVec2& point) const noexcept
    {
        return Index(&point);
    }

    //Segment GetSegment(int index) const noexcept
    //{
    //    const auto segment_count = static_cast<int>(Points.size()) - ((Flags & PolylineFlags_Closed) ? 0 : 1);
    //    if (segment_count < 1)
    //        return {};

    //    index %= segment_count;
    //    if (index < 0)
    //        index += segment_count;

    //    return { Points[index], Points[(index + 1) % Points.size()] };
    //}

    struct Stats
    {
        int Elements = 0;
        int Vertices = 0;
        int Indices  = 0;
        double Duration = 0.0;
        double DurationAvg = 0.0;
        int Iterations = 0;
    };

    Stats Draw(ImDrawList* draw_list, const ImVec2& origin, Method method, int stress = 0) const;    

    template <typename F>
        requires std::invocable<F, const ImVec2&, const ImVec2&> || std::invocable<F, const ImVec2&, const ImVec2&, int>
    void ForEachSegment(F&& f) const
    {
        if (Points.size() < 2)
            return;
        for (size_t i = 0; i < Points.size() - 1; ++i)
        {
            if constexpr (std::invocable<F, const ImVec2&, const ImVec2&, int>)
                f(Points[i], Points[i + 1], static_cast<int>(i));
            else
                f(Points[i], Points[i + 1]);
        }
        if ((Flags & PolylineFlags_Closed) && Points.size() > 2)
        {
            if constexpr (std::invocable<F, const ImVec2&, const ImVec2&, int>)
                f(Points.back(), Points.front(), static_cast<int>(Points.size() - 1));
            else
                f(Points.back(), Points.front());
        }
    }

    template <typename F>
        requires std::invocable<F, const ImVec2&> || std::invocable<F, const ImVec2&, PolylinePointFlags>
    void ForEachPoint(F&& f) const
    {
        for (const auto& point : Points)
        {
            if constexpr (std::invocable<F, const ImVec2&, PolylinePointFlags>)
            {
                int flags = PolylinePointFlags_None;
                if (&point == &Points.front())
                    flags |= PolylinePointFlags_First;
                if (&point == &Points.back())
                    flags |= PolylinePointFlags_Last;
                if (CurrentPoint >= 0 && Index(&point) == CurrentPoint)
                    flags |= PolylinePointFlags_Current;
                f(point, flags);
            }
            else
            {
                f(point);
            }
        }
    }

    ImRect Bounds() const
    {
        auto min = ImVec2( FLT_MAX,  FLT_MAX);
        auto max = ImVec2(-FLT_MAX, -FLT_MAX);

        for (const auto& point : Points)
        {
            min = ImMin(min, point);
            max = ImMax(max, point);
        }
        auto size = max - min;

        if (size.x <= 0.0f || size.y <= 0.0f)
            return {};

        auto bounds = ImRect(min, max);
        bounds.Expand(Thickness * 0.5f);
        return bounds;
    }
};

enum class PolylineTemplate : int
{
    Empty,
    RectStroke,
    RectStrokeThick,
    RectRoundedStroke,
    RectRoundedStrokeThick,
    CircleStroke,
    CircleStrokeThick,
    TriangleStroke,
    TriangleStrokeThick,
    LongStroke,
    LongStrokeThick,
    LongJaggedStroke,
    LongJaggedStrokeThick,
    LineStroke,
    LineStrokeThick,
    Issue2183,
    Issue3366,
    Issue288_A,
    Issue288_B,
    Issue3258_A,
    Issue3258_B,
};

enum class RectangleImplementation : int
{
    Upstream,
    UpstreamLegacy,
    NewV1,
    NewV2
};

struct RectangleTestState
{
    bool                        AntiAliased    = true;
    bool                        AntiAliasedTex = true;
    RectangleImplementation     Implementation = RectangleImplementation::NewV1;
    float                       Thickness      = 1.0f;
    ImVec2                      Size           = ImVec2(400.0f, 400.0f);
    float                       Rounding       = 100.0f;
    ImDrawFlags                 Corners        = ImDrawFlags_RoundCornersAll;
    int                         Stress         = 1;
    bool                        ShowMesh       = false;
    ImMeshCapture               MeshCapture;
    ImRollingAverageValue<120, double> DrawDuration;
};

struct State
{
    using enum Method;
    using enum ImGuiEx::ImDrawFlagsExtra_;

    Polyline*                       Current = nullptr;
    vector<unique_ptr<Polyline>>    Polylines;
    bool                            EnableEdit = true;
    bool                            ShowPoints = true;
    bool                            ShowLines = true;
    bool                            ShowMesh = false;

    PolylineTemplate                Template = PolylineTemplate::Empty;

    Method                          Method = New;
    ImDrawFlags                     LineCap = ImDrawFlags_CapDefault_;
    ImDrawFlags                     LineJoin = ImDrawFlags_JoinDefault_;
    float                           MiterLimit = 4.0f;
    bool                            UseFixedDpi = true;
    float                           FixedDpi = 1.0f;

    int                             Stress = 1;
    ImRollingAverageValue<120, double> DrawDuration;

    RectangleTestState              RectangleTest;

    ImGuiEx::Canvas                 Canvas;

    ImGuiSettingsHandler            SettingsHandler;

    ImMeshCapture                   MeshCapture;

    State();

    void Clear()
    {
        Polylines.clear();
        Current = nullptr;
        DrawDuration = {};
    }

    void SetCurrent(int index)
    {
        auto previous = Current;
        if (index >= 0 && index < static_cast<int>(Polylines.size()))
            Current = Polylines[index].get();
        else
            Current = nullptr;

        if (Current != previous)
            DrawDuration = {};
    }

    void SetCurrent(const Polyline* polyline)
    {
        SetCurrent(Index(polyline));
    }

    int Index(const Polyline* polyline) const noexcept
    {
        for (size_t i = 0; i < Polylines.size(); ++i)
        {
            if (Polylines[i].get() == polyline)
                return static_cast<int>(i);
        }
        return -1;
    }
};

} // namespace ImPolyline

ImGuiSettingsHandler* GetPolylinePlaygroundSettingsHandler();

void PolylinePlayground();
