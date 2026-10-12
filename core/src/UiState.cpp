#include "neptune/core/UiState.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "neptune/core/FileUtil.h"

namespace neptune::core::uistate
{
namespace
{
constexpr const char* kFormatTag = "neptune-ui-state";

int clampSide (int v) { return std::clamp (v, minModalSide, maxModalSide); }

// Lit {"width":w,"height":h} ; invalide -> défaut.
Size readSize (const nlohmann::json& o, const char* key, Size def)
{
    const auto it = o.find (key);
    if (it == o.end() || ! it->is_object()) return def;
    const auto w = it->find ("width"), h = it->find ("height");
    if (w == it->end() || h == it->end() || ! w->is_number_integer() || ! h->is_number_integer()) return def;
    const long long wv = w->get<long long>(), hv = h->get<long long>();
    if (wv < minModalSide || wv > maxModalSide || hv < minModalSide || hv > maxModalSide) return def;
    return { static_cast<int> (wv), static_cast<int> (hv) };
}
} // namespace

void UiState::setModalSize (Terminal t, Size s) noexcept
{
    Size& dst = t == Terminal::Touch ? modalTouch : modalPointer;
    dst = { clampSide (s.width), clampSide (s.height) };
}

std::string toJson (const UiState& s)
{
    nlohmann::json o;
    o["format"] = kFormatTag;
    o["schema_version"] = currentSchemaVersion;
    o["modal_pointer"] = { { "width", s.modalPointer.width }, { "height", s.modalPointer.height } };
    o["modal_touch"] = { { "width", s.modalTouch.width }, { "height", s.modalTouch.height } };
    if (! s.lastImportTemplate.empty()) o["import_template"] = s.lastImportTemplate;
    return o.dump (2) + "\n";
}

UiState fromJson (std::string_view text)
{
    UiState s;
    const auto doc = nlohmann::json::parse (text.begin(), text.end(), nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return s;
    s.modalPointer = readSize (doc, "modal_pointer", s.modalPointer);
    s.modalTouch = readSize (doc, "modal_touch", s.modalTouch);
    if (const auto it = doc.find ("import_template"); it != doc.end() && it->is_string() && it->get<std::string>().size() < 8192)
        s.lastImportTemplate = it->get<std::string>();
    return s;
}

UiState loadFile (const std::filesystem::path& path)
{
    std::string text;
    if (! fileutil::readAll (path, text)) return {};
    return fromJson (text);
}

bool saveFile (const std::filesystem::path& path, const UiState& s, std::string* error)
{
    return fileutil::writeAtomic (path, toJson (s), error);
}
} // namespace neptune::core::uistate
