#include "neptune/core/Facade.h"

#include <algorithm>
#include <cmath>
#include <map>
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
        case WidgetType::Zone:   w.w = 320; w.h = 200; break;
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
        w.layout = std::clamp (w.layout, 0, numZoneLayouts - 1);
        w.align = std::clamp (w.align, 0, 2);
        w.justify = std::clamp (w.justify, 0, 3);
        w.gap = std::clamp (w.gap, 0, maxZoneGap);
        w.columns = std::clamp (w.columns, 1, maxZoneColumns);
        if (w.id <= 0 || used.count (w.id) != 0)
        {
            int id = 1;
            while (used.count (id) != 0) ++id;
            w.id = id;
        }
        used.insert (w.id);
    }
    // rattachements valides : la zone existe, n'est pas le widget lui-même, et la chaîne de parents ne boucle pas
    for (auto& w : f.widgets)
    {
        const auto* z = w.parent != 0 && w.parent != w.id ? f.find (w.parent) : nullptr;
        if (z == nullptr || z->type != WidgetType::Zone) w.parent = 0;
    }
    for (auto& w : f.widgets)
    {
        int steps = 0;
        for (int p = w.parent; p != 0; p = f.find (p)->parent)
            if (p == w.id || ++steps > 8) { w.parent = 0; break; }
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

// ---- Zones ----

std::set<int> descendants (const Facade& f, int id)
{
    std::set<int> out;
    bool grew = true;
    while (grew)
    {
        grew = false;
        for (const auto& w : f.widgets)
            if (w.parent != 0 && (w.parent == id || out.count (w.parent) != 0) && out.insert (w.id).second) grew = true;
    }
    return out;
}

namespace
{
void reassign (Facade& f, Widget& w)
{
    const auto below = descendants (f, w.id);
    const Widget* best = nullptr;
    const int cx = w.x + w.w / 2, cy = w.y + w.h / 2;
    for (const auto& z : f.widgets)
    {
        if (z.type != WidgetType::Zone || z.id == w.id || below.count (z.id) != 0) continue;
        if (cx < z.x || cx >= z.x + z.w || cy < z.y || cy >= z.y + z.h) continue;
        if (z.w * z.h <= w.w * w.h && w.type == WidgetType::Zone) continue;   // une zone n'entre que dans une zone plus grande
        if (best == nullptr || z.w * z.h < best->w * best->h) best = &z;
    }
    w.parent = best != nullptr ? best->id : 0;
}
} // namespace

void assignParents (Facade& f, const std::set<int>& touched)
{
    std::set<int> zonesTouched;
    for (auto& w : f.widgets)
        if (touched.count (w.id) != 0)
        {
            reassign (f, w);
            if (w.type == WidgetType::Zone) zonesTouched.insert (w.id);
        }
    // une zone qui a bougé ou grandi adopte les widgets libres qu'elle couvre maintenant, et lâche ceux qui sont sortis
    for (const int zid : zonesTouched)
        for (auto& w : f.widgets)
            if (touched.count (w.id) == 0 && (w.parent == 0 || w.parent == zid)) reassign (f, w);
}

void moveWidgets (Facade& f, const std::set<int>& ids, int dx, int dy)
{
    std::set<int> all = ids;
    for (const int id : ids) all.merge (descendants (f, id));
    for (auto& w : f.widgets)
        if (all.count (w.id) != 0) { w.x += dx; w.y += dy; }
}

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Angle d'écran (y vers le bas : le sens croissant est le sens horaire) de la place de départ.
double startAngle (CircleStart s) noexcept
{
    switch (s)
    {
        case CircleStart::Top:    return -kPi / 2.0;
        case CircleStart::Right:  return 0.0;
        case CircleStart::Bottom: return kPi / 2.0;
        default:                  return kPi;
    }
}

// Écart horaire de `angle` depuis `from`, dans [0, 2π[ (les erreurs d'arrondi près de 2π comptent pour 0).
double clockwiseFrom (double angle, double from) noexcept
{
    double d = std::fmod (angle - from, 2.0 * kPi);
    if (d < 0.0) d += 2.0 * kPi;
    return d > 2.0 * kPi - 0.02 ? 0.0 : d;
}

void centreOn (Facade& f, int id, double px, double py)
{
    const auto& w = *f.find (id);
    const int nx = std::clamp (static_cast<int> (std::lround (px - w.w / 2.0)), 0, canvasWidth - w.w);
    const int ny = std::clamp (static_cast<int> (std::lround (py - w.h / 2.0)), 0, canvasHeight - w.h);
    moveWidgets (f, { id }, nx - w.x, ny - w.y);
}

// `order[0]` à l'angle `theta0`, les suivants à pas égaux dans le sens horaire.
void spreadOnCircle (Facade& f, const std::vector<int>& order, double cx, double cy, double r, double theta0)
{
    const double step = 2.0 * kPi / static_cast<double> (order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const double a = theta0 + step * static_cast<double> (i);
        centreOn (f, order[i], cx + r * std::cos (a), cy + r * std::sin (a));
    }
}
} // namespace

void arrangeCircle (Facade& f, const std::set<int>& ids, int refId, CircleStart start)
{
    auto ws = pick (f, ids);
    if (ws.size() < 3) return;
    int lo = canvasWidth, hi = 0, top = canvasHeight, bottom = 0, maxW = 0, maxH = 0;
    for (const auto* w : ws) { lo = std::min (lo, w->x); hi = std::max (hi, w->x + w->w); top = std::min (top, w->y); bottom = std::max (bottom, w->y + w->h); maxW = std::max (maxW, w->w); maxH = std::max (maxH, w->h); }
    const double cx = (lo + hi) / 2.0, cy = (top + bottom) / 2.0;
    const double n = static_cast<double> (ws.size());
    double r = std::min (hi - lo - maxW, bottom - top - maxH) / 2.0;
    r = std::max ({ r, 40.0, n * std::max (maxW, maxH) / (2.0 * kPi) });   // jamais trop serré pour que les widgets se chevauchent
    const Widget* ref = ids.count (refId) != 0 ? f.find (refId) : ws.front();
    const double refAngle = std::atan2 (ref->y + ref->h / 2.0 - cy, ref->x + ref->w / 2.0 - cx);
    std::vector<int> others;
    for (const auto* w : ws)
        if (w->id != ref->id) others.push_back (w->id);
    std::stable_sort (others.begin(), others.end(), [&] (int a, int b)
    {
        const auto &wa = *f.find (a), &wb = *f.find (b);
        return clockwiseFrom (std::atan2 (wa.y + wa.h / 2.0 - cy, wa.x + wa.w / 2.0 - cx), refAngle)
             < clockwiseFrom (std::atan2 (wb.y + wb.h / 2.0 - cy, wb.x + wb.w / 2.0 - cx), refAngle);
    });
    std::vector<int> order { ref->id };
    order.insert (order.end(), others.begin(), others.end());
    spreadOnCircle (f, order, cx, cy, r, startAngle (start));
}

void applyLayouts (Facade& f)
{
    // zones les moins profondes d'abord : une zone rangée par sa parente emporte son contenu avant d'être rangée à son tour
    auto depth = [&f] (const Widget& w)
    {
        int d = 0;
        for (int p = w.parent; p != 0 && d < 16; p = f.find (p)->parent) ++d;
        return d;
    };
    std::vector<int> zones;
    for (const auto& w : f.widgets)
        if (w.type == WidgetType::Zone && w.layout != static_cast<int> (ZoneLayout::Free)) zones.push_back (w.id);
    std::stable_sort (zones.begin(), zones.end(), [&] (int a, int b) { return depth (*f.find (a)) < depth (*f.find (b)); });

    for (const int zid : zones)
    {
        const Widget z = *f.find (zid);
        std::vector<int> kids;
        for (const auto& w : f.widgets)
            if (w.parent == zid) kids.push_back (w.id);
        if (kids.empty()) continue;
        const int cx = z.x + zonePad, cy = z.y + zonePad + (z.text.empty() ? 0 : zoneTitleHeight);
        const int cw = std::max (1, z.w - 2 * zonePad), ch = std::max (1, z.h - 2 * zonePad - (z.text.empty() ? 0 : zoneTitleHeight));
        const auto layout = static_cast<ZoneLayout> (z.layout);
        // ordre de rangement : par position (ligne : de gauche à droite ; colonne : de haut en bas ; matrice : ligne par ligne)
        const double midX = cx + cw / 2.0, midY = cy + ch / 2.0;
        const double theta0 = startAngle (static_cast<CircleStart> (z.justify));
        std::stable_sort (kids.begin(), kids.end(), [&] (int a, int b)
        {
            const auto &wa = *f.find (a), &wb = *f.find (b);
            if (layout == ZoneLayout::Circle)
                return clockwiseFrom (std::atan2 (wa.y + wa.h / 2.0 - midY, wa.x + wa.w / 2.0 - midX), theta0)
                     < clockwiseFrom (std::atan2 (wb.y + wb.h / 2.0 - midY, wb.x + wb.w / 2.0 - midX), theta0);
            if (layout == ZoneLayout::Row) return wa.x < wb.x;
            if (layout == ZoneLayout::Column) return wa.y < wb.y;
            const int ra = (wa.y + wa.h / 2) / 40, rb = (wb.y + wb.h / 2) / 40;   // même rangée à 40 px près
            return ra != rb ? ra < rb : wa.x < wb.x;
        });
        auto place = [&] (int id, int x, int y)
        {
            auto& w = *f.find (id);
            const int nx = std::clamp (x, 0, canvasWidth - w.w), ny = std::clamp (y, 0, canvasHeight - w.h);
            moveWidgets (f, { id }, nx - w.x, ny - w.y);
        };
        auto cross = [&] (int start, int size, int itemSize)
        {
            switch (static_cast<CrossAlign> (z.align))
            {
                case CrossAlign::Centre: return start + (size - itemSize) / 2;
                case CrossAlign::End:    return start + size - itemSize;
                default:                 return start;
            }
        };
        auto mainStart = [&] (int start, int size, int total)
        {
            switch (static_cast<Justify> (z.justify))
            {
                case Justify::Centre: return start + (size - total) / 2;
                case Justify::End:    return start + size - total;
                default:              return start;
            }
        };
        const int n = static_cast<int> (kids.size());
        if (layout == ZoneLayout::Circle)
        {
            int maxW = 0, maxH = 0;
            for (const int id : kids) { maxW = std::max (maxW, f.find (id)->w); maxH = std::max (maxH, f.find (id)->h); }
            const double r = std::max (0.0, std::min (cw - maxW, ch - maxH) / 2.0);
            spreadOnCircle (f, kids, midX, midY, r, theta0);
        }
        else if (layout == ZoneLayout::Row || layout == ZoneLayout::Column)
        {
            const bool row = layout == ZoneLayout::Row;
            int sum = 0;
            for (const int id : kids) sum += row ? f.find (id)->w : f.find (id)->h;
            const int mainSize = row ? cw : ch;
            int gap = z.gap;
            if (z.justify == static_cast<int> (Justify::Spread) && n > 1) gap = std::max (0, (mainSize - sum) / (n - 1));
            const int total = sum + gap * (n - 1);
            int pos = z.justify == static_cast<int> (Justify::Spread) ? (row ? cx : cy) : mainStart (row ? cx : cy, mainSize, total);
            for (const int id : kids)
            {
                const auto& w = *f.find (id);
                if (row) { place (id, pos, cross (cy, ch, w.h)); pos += w.w + gap; }
                else     { place (id, cross (cx, cw, w.w), pos); pos += w.h + gap; }
            }
        }
        else
        {
            const int cols = std::min (std::max (1, z.columns), n), rows = (n + cols - 1) / cols;
            int cellW = 0, cellH = 0;
            for (const int id : kids) { cellW = std::max (cellW, f.find (id)->w); cellH = std::max (cellH, f.find (id)->h); }
            const int totalW = cols * cellW + (cols - 1) * z.gap, totalH = rows * cellH + (rows - 1) * z.gap;
            const int x0 = mainStart (cx, cw, totalW), y0 = mainStart (cy, ch, totalH);
            for (int i = 0; i < n; ++i)
            {
                const auto& w = *f.find (kids[static_cast<std::size_t> (i)]);
                const int c = i % cols, r = i / cols;
                place (w.id, cross (x0 + c * (cellW + z.gap), cellW, w.w), cross (y0 + r * (cellH + z.gap), cellH, w.h));
            }
        }
    }
}

void arrange (Facade& f, const std::set<int>& touched)
{
    assignParents (f, touched);
    applyLayouts (f);
}

std::set<int> duplicateWidgets (Facade& f, const std::set<int>& ids, int offset)
{
    std::set<int> all = ids;
    for (const int id : ids) all.merge (descendants (f, id));
    std::map<int, int> remap;
    const auto original = f.widgets;
    for (const auto& src : original)
    {
        if (all.count (src.id) == 0 || static_cast<int> (f.widgets.size()) >= maxWidgets) continue;
        auto copy = src;
        copy.id = f.freeId();
        copy.x += offset;
        copy.y += offset;
        remap[src.id] = copy.id;
        f.widgets.push_back (std::move (copy));
    }
    std::set<int> fresh;
    for (const auto& [from, to] : remap)
    {
        auto* w = f.find (to);
        if (const auto it = remap.find (w->parent); it != remap.end()) w->parent = it->second;   // dans une zone copiée : suit la copie
        if (ids.count (from) != 0) fresh.insert (to);
    }
    f = sanitised (std::move (f));
    return fresh;
}

void matchSize (Facade& f, int id)
{
    const auto* ref = f.find (id);
    if (ref == nullptr) return;
    const int w = ref->w, h = ref->h;
    const auto type = ref->type;
    for (auto& o : f.widgets)
        if (o.type == type) { o.w = w; o.h = h; }
    f = sanitised (std::move (f));
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
        if (w.parent != 0) e["parent"] = w.parent;
        if (w.type == WidgetType::Zone)
        {
            e["layout"] = w.layout;
            e["align"] = w.align;
            e["justify"] = w.justify;
            e["gap"] = w.gap;
            e["cols"] = w.columns;
        }
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
            w.parent = intOf (e, "parent", 0);
            w.layout = intOf (e, "layout", 0);
            w.align = intOf (e, "align", 0);
            w.justify = intOf (e, "justify", 0);
            w.gap = intOf (e, "gap", 8);
            w.columns = intOf (e, "cols", 2);
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
