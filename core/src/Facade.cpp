#include "neptune/core/Facade.h"

#include <algorithm>
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
            f.widgets.push_back (std::move (w));
        }
    return sanitised (std::move (f));
}
} // namespace neptune::core::facade
