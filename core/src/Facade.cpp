#include "neptune/core/Facade.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <nlohmann/json.hpp>

namespace neptune::core::facade
{
using nlohmann::json;

int Facade::freeId() const noexcept
{
    int id = 1;
    for (const auto& w : widgets) id = std::max (id, w.id + 1);
    return id;
}

const Widget* Facade::find (int id) const noexcept
{
    for (const auto& w : widgets)
        if (w.id == id) return &w;
    return nullptr;
}

Widget* Facade::find (int id) noexcept
{
    for (auto& w : widgets)
        if (w.id == id) return &w;
    return nullptr;
}

Widget defaultWidget (WidgetType type, int id, int x, int y)
{
    Widget w;
    w.id = id;
    w.type = type;
    switch (type)
    {
        case WidgetType::Knob:   w.w = 90;  w.h = 110; break;
        case WidgetType::Button: w.w = 120; w.h = 44;  break;
        case WidgetType::Label:  w.w = 160; w.h = 32;  break;
        case WidgetType::Menu:   w.w = 170; w.h = 56;  break;
        case WidgetType::Action: w.w = 130; w.h = 44;  break;
        case WidgetType::Curve:  w.w = 340; w.h = 260; break;
    }
    w.x = x;
    w.y = y;
    return w;
}

namespace
{
std::string clipped (std::string s, std::size_t maxLength)
{
    if (s.size() > maxLength) s.resize (maxLength);
    return s;
}
} // namespace

Facade sanitised (Facade f)
{
    f.image = clipped (std::move (f.image), maxImagePath);
    const int fit = static_cast<int> (f.fit);
    if (fit < 0 || fit >= numImageFits) f.fit = ImageFit::Fit;
    if (f.widgets.size() > static_cast<std::size_t> (maxWidgets)) f.widgets.resize (maxWidgets);
    std::set<int> used;
    for (auto& w : f.widgets)
    {
        const int type = static_cast<int> (w.type);
        if (type < 0 || type >= numWidgetTypes) w.type = WidgetType::Knob;
        w.w = std::clamp (w.w, minWidgetSize, canvasWidth);
        w.h = std::clamp (w.h, minWidgetSize, canvasHeight);
        w.x = std::clamp (w.x, 0, canvasWidth - w.w);
        w.y = std::clamp (w.y, 0, canvasHeight - w.h);
        w.text = clipped (std::move (w.text), maxTextLength);
        if (w.snapshot.size() > maxSnapshot) w.snapshot.resize (maxSnapshot);
        w.snapshot.erase (std::remove_if (w.snapshot.begin(), w.snapshot.end(), [] (const auto& e) { return e.first == 0 || ! std::isfinite (e.second); }), w.snapshot.end());
        if (w.id <= 0 || used.count (w.id) != 0)
        {
            int id = 1;
            while (used.count (id) != 0) ++id;
            w.id = id;
        }
        used.insert (w.id);
    }
    return f;
}

namespace
{
std::vector<Widget*> pick (Facade& f, const std::set<int>& ids)
{
    std::vector<Widget*> out;
    for (auto& w : f.widgets)
        if (ids.count (w.id) != 0) out.push_back (&w);
    return out;
}
void keepInside (Widget& w)
{
    w.x = std::clamp (w.x, 0, canvasWidth - w.w);
    w.y = std::clamp (w.y, 0, canvasHeight - w.h);
}
} // namespace

void alignWidgets (Facade& f, const std::set<int>& ids, Align a)
{
    auto ws = pick (f, ids);
    if (ws.size() < 2) return;
    int lo = canvasWidth, hi = 0, top = canvasHeight, bottom = 0;
    for (const auto* w : ws) { lo = std::min (lo, w->x); hi = std::max (hi, w->x + w->w); top = std::min (top, w->y); bottom = std::max (bottom, w->y + w->h); }
    for (auto* w : ws)
    {
        switch (a)
        {
            case Align::Left:    w->x = lo; break;
            case Align::Right:   w->x = hi - w->w; break;
            case Align::Top:     w->y = top; break;
            case Align::Bottom:  w->y = bottom - w->h; break;
            case Align::CentreH: w->x = (lo + hi - w->w) / 2; break;
            case Align::CentreV: w->y = (top + bottom - w->h) / 2; break;
        }
        keepInside (*w);
    }
}

void distributeWidgets (Facade& f, const std::set<int>& ids, bool horizontal)
{
    auto ws = pick (f, ids);
    if (ws.size() < 3) return;
    std::sort (ws.begin(), ws.end(), [horizontal] (const Widget* a, const Widget* b) { return horizontal ? a->x < b->x : a->y < b->y; });
    int total = 0;
    for (const auto* w : ws) total += horizontal ? w->w : w->h;
    const int first = horizontal ? ws.front()->x : ws.front()->y;
    const int last = horizontal ? ws.back()->x + ws.back()->w : ws.back()->y + ws.back()->h;
    const int gap = (last - first - total) / static_cast<int> (ws.size() - 1);
    int pos = first;
    for (auto* w : ws)
    {
        (horizontal ? w->x : w->y) = pos;
        pos += (horizontal ? w->w : w->h) + gap;
        keepInside (*w);
    }
}

void arrangeGrid (Facade& f, const std::set<int>& ids, int columns)
{
    auto ws = pick (f, ids);
    if (ws.size() < 2 || columns < 1) return;
    columns = std::min (columns, static_cast<int> (ws.size()));
    int x0 = canvasWidth, y0 = canvasHeight, cellW = 0, cellH = 0;
    for (const auto* w : ws) { x0 = std::min (x0, w->x); y0 = std::min (y0, w->y); cellW = std::max (cellW, w->w); cellH = std::max (cellH, w->h); }
    const int gapX = 12, gapY = 12;
    for (std::size_t i = 0; i < ws.size(); ++i)
    {
        const int c = static_cast<int> (i) % columns, r = static_cast<int> (i) / columns;
        ws[i]->x = x0 + c * (cellW + gapX);
        ws[i]->y = y0 + r * (cellH + gapY);
        keepInside (*ws[i]);
    }
}

std::string toJson (const Facade& f)
{
    json o;
    if (! f.image.empty()) o["image"] = f.image;
    o["fit"] = static_cast<int> (f.fit);
    json arr = json::array();
    for (const auto& w : f.widgets)
    {
        json e;
        e["id"] = w.id;
        e["type"] = static_cast<int> (w.type);
        e["x"] = w.x;
        e["y"] = w.y;
        e["w"] = w.w;
        e["h"] = w.h;
        if (w.param != 0) e["param"] = w.param;
        if (! w.text.empty()) e["text"] = w.text;
        if (! w.snapshot.empty())
        {
            json snap = json::array();
            for (const auto& [id, v] : w.snapshot) snap.push_back ({ id, v });
            e["snap"] = std::move (snap);
        }
        arr.push_back (std::move (e));
    }
    o["widgets"] = std::move (arr);
    return o.dump();
}

namespace
{
int intOf (const json& o, const char* key, int def)
{
    const auto it = o.find (key);
    return it != o.end() && it->is_number_integer() ? static_cast<int> (std::clamp<long long> (it->get<long long>(), -1000000, 1000000)) : def;
}
} // namespace

Facade fromJson (std::string_view text)
{
    Facade f;
    const auto o = json::parse (text.begin(), text.end(), nullptr, false);
    if (! o.is_object()) return f;
    if (const auto it = o.find ("image"); it != o.end() && it->is_string()) f.image = it->get<std::string>();
    f.fit = static_cast<ImageFit> (intOf (o, "fit", 0));
    if (const auto it = o.find ("widgets"); it != o.end() && it->is_array())
        for (const auto& e : *it)
        {
            if (! e.is_object() || f.widgets.size() >= static_cast<std::size_t> (maxWidgets)) continue;
            Widget w;
            w.id = intOf (e, "id", 0);
            w.type = static_cast<WidgetType> (intOf (e, "type", 0));
            w.x = intOf (e, "x", 0);
            w.y = intOf (e, "y", 0);
            w.w = intOf (e, "w", w.w);
            w.h = intOf (e, "h", w.h);
            if (const auto p = e.find ("param"); p != e.end() && p->is_number_integer())
            {
                const auto v = p->get<long long>();
                if (v > 0 && v < 1000000) w.param = static_cast<ParamId> (v);
            }
            if (const auto t = e.find ("text"); t != e.end() && t->is_string()) w.text = t->get<std::string>();
            if (const auto sn = e.find ("snap"); sn != e.end() && sn->is_array())
                for (const auto& pr : *sn)
                    if (pr.is_array() && pr.size() == 2 && pr[0].is_number_integer() && pr[1].is_number() && w.snapshot.size() < maxSnapshot)
                    {
                        const auto id = pr[0].get<long long>();
                        if (id > 0 && id < 1000000) w.snapshot.emplace_back (static_cast<ParamId> (id), pr[1].get<double>());
                    }
            f.widgets.push_back (std::move (w));
        }
    return sanitised (std::move (f));
}
} // namespace neptune::core::facade
