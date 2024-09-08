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

namespace ImPolyline {

using std::vector;
using std::unique_ptr;
using std::make_unique;

enum class Method
{
    Upstream,
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
};

template <size_t N>
struct Average
{
    double Values[N] = {};
    size_t Index = 0;
    size_t Count = 0;

    double CachedValue = 0.0;

    void Add(double value)
    {
        Values[Index] = value;
        Index = (Index + 1) % N;
        if (Count < N)
            ++Count;

        if (Index == 0)
            CachedValue = Get();
    }

    double Get() const
    {
        if (Count == 0)
            return 0.0;

        double sum = 0.0;
        for (size_t i = 0; i < Count; ++i)
            sum += Values[i];
        return sum / Count;
    }
};

enum class NewPolylineContent : int
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

    NewPolylineContent              NewPolyline = NewPolylineContent::Empty;

    Method                          Method = New;
    ImDrawFlags                     LineCap = ImDrawFlags_CapDefault_;
    ImDrawFlags                     LineJoin = ImDrawFlags_JoinDefault_;
    float                           MiterLimit = 2.0f;

    int                             Stress = 1;
    Average<60>                     DrawDuration;
    Average<60>                     DrawDurationAvg;

    ImGuiEx::Canvas                 Canvas;

    ImGuiSettingsHandler            SettingsHandler;

    State();

    void Clear()
    {
        Polylines.clear();
        Current = nullptr;
        DrawDuration = {};
        DrawDurationAvg = {};
    }

    void SetCurrent(int index)
    {
        auto previous = Current;
        if (index >= 0 && index < static_cast<int>(Polylines.size()))
            Current = Polylines[index].get();
        else
            Current = nullptr;

        if (Current != previous)
        {
            DrawDuration = {};
            DrawDurationAvg = {};
        }
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
