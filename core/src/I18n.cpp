#include "neptune/core/I18n.h"
#include <array>
#include <unordered_map>

namespace neptune::core::i18n
{
namespace
{
constexpr int kNumLanguages = 2; // ordre : en, fr

struct Entry
{
    std::string_view key;
    std::array<std::string_view, kNumLanguages> text; // en, fr ; vide = retombe sur l'anglais
};

// Pour ajouter une langue : une colonne ici, une ligne dans languages(), kNumLanguages + 1.
const Entry kEntries[] = {
    { "recovery.found", { "Unsaved state from a previous session was found (autosave of {0}).",
                          "Un état non sauvegardé d'une session précédente a été trouvé (autosave du {0})." } },
    { "recovery.restore", { "Restore", "Restaurer" } },
    { "recovery.discard", { "Discard", "Ignorer" } },
    { "latency.reported", { "Reported latency: {0} samples", "Latence déclarée : {0} échantillons" } },
    { "latency.set", { "Latency {0}", "Latence {0}" } },
    { "knob.reset", { "Reset to default", "Remettre par défaut" } },
    { "modal.close", { "Close", "Fermer" } },
    { "undo.undo", { "Undo", "Annuler" } },
    { "undo.redo", { "Redo", "Rétablir" } },
    { "sampler.load", { "Load sample...", "Charger un sample..." } },
    { "sampler.choose", { "Choose a sample (WAV, AIFF, FLAC)", "Choisir un sample (WAV, AIFF, FLAC)" } },
    { "sampler.none", { "No sample loaded", "Aucun sample chargé" } },
    { "sampler.loading", { "Loading...", "Chargement..." } },
    { "sampler.loaded", { "{0} - {1}, {2} Hz, {3} s, {4}-bit, {5} MB", "{0} - {1}, {2} Hz, {3} s, {4} bits, {5} Mo" } },
    { "sampler.error", { "Cannot load: {0}", "Chargement impossible : {0}" } },
    { "sampler.mono", { "mono", "mono" } },
    { "sampler.stereo", { "stereo", "stéréo" } },
    { "sampler.mode", { "Mode: {0}", "Mode : {0}" } },
    { "sampler.mode_one_shot", { "One-shot", "One-shot" } },
    { "sampler.mode_loop", { "Loop", "Boucle" } },
    { "sampler.root", { "Root note (MIDI number)", "Note racine (numéro MIDI)" } },
    { "sampler.release", { "Release (ms, loop mode)", "Relâchement (ms, mode boucle)" } },
    { "profile.live", { "Live", "Live" } },
    { "profile.studio", { "Studio", "Studio" } },
    { "profile.status", { "Global profile: {0} - this instance: {1}{2}", "Profil global : {0} - cette instance : {1}{2}" } },
    { "profile.locked_suffix", { " (locked)", " (verrouillée)" } },
    { "profile.switch", { "Switch global profile", "Basculer le profil global" } },
    { "profile.lock", { "Lock this instance", "Verrouiller cette instance" } },
    { "profile.unlock", { "Unlock this instance", "Déverrouiller cette instance" } },
};

const std::vector<Language> kLanguages = { { "en", "English" }, { "fr", "Français" } };

const std::unordered_map<std::string_view, const Entry*>& index()
{
    static const auto m = [] {
        std::unordered_map<std::string_view, const Entry*> r;
        for (const auto& e : kEntries) r.emplace (e.key, &e);
        return r;
    }();
    return m;
}
} // namespace

const std::vector<Language>& languages() { return kLanguages; }

bool isSupported (std::string_view code)
{
    for (const auto& l : kLanguages)
        if (l.code == code) return true;
    return false;
}

Translator::Translator (std::string_view languageCode) : code_ ("en"), index_ (0)
{
    for (size_t i = 0; i < kLanguages.size(); ++i)
        if (kLanguages[i].code == languageCode) { code_ = kLanguages[i].code; index_ = static_cast<int> (i); }
}

std::string_view Translator::tr (std::string_view key) const
{
    const auto it = index().find (key);
    if (it == index().end()) return key;
    const auto& t = it->second->text;
    const auto s = t[static_cast<size_t> (index_)];
    return s.empty() ? t[0] : s;
}

std::string Translator::format (std::string_view key, std::initializer_list<std::string_view> args) const
{
    return substitute (tr (key), args);
}

std::string substitute (std::string_view pattern, std::initializer_list<std::string_view> args)
{
    std::string out;
    out.reserve (pattern.size());
    for (size_t i = 0; i < pattern.size();)
    {
        if (pattern[i] == '{')
        {
            const auto close = pattern.find ('}', i);
            if (close != std::string_view::npos && close > i + 1)
            {
                size_t n = 0;
                bool digits = true;
                for (size_t j = i + 1; j < close; ++j)
                {
                    if (pattern[j] < '0' || pattern[j] > '9') { digits = false; break; }
                    n = n * 10 + static_cast<size_t> (pattern[j] - '0');
                }
                if (digits && n < args.size())
                {
                    out += *(args.begin() + static_cast<std::ptrdiff_t> (n));
                    i = close + 1;
                    continue;
                }
            }
        }
        out += pattern[i++];
    }
    return out;
}

std::vector<EntryView> allEntries()
{
    std::vector<EntryView> r;
    for (const auto& e : kEntries)
    {
        EntryView v { e.key, {} };
        for (const auto& s : e.text) v.text.push_back (s);
        r.push_back (std::move (v));
    }
    return r;
}
} // namespace neptune::core::i18n
