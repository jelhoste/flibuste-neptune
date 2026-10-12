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
    { "envelope.preset_sustained", { "Sustained (all segments)", "Tenu (tous les segments)" } },
    { "envelope.preset_perc_attack", { "Percussion: attack only", "Percussion : attaque seule" } },
    { "envelope.preset_perc_decay", { "Percussion: attack + hold + decay", "Percussion : attaque + hold + decay" } },
    { "envelope.preset", { "Envelope preset", "Réglage d'enveloppe" } },
    { "envelope.delay", { "Dly", "Dél" } },
    { "envelope.attack", { "Atk", "Att" } },
    { "envelope.hold", { "Hold", "Hold" } },
    { "envelope.decay", { "Dec", "Dec" } },
    { "envelope.release", { "Rel", "Rel" } },
    { "envelope.db", { "dB curve", "Courbe en dB" } },
    { "sampler.import", { "Import...", "Importer..." } },
    { "sampler.loaded_many", { "{0} zones from {1} files - {2} MB", "{0} zones issues de {1} fichiers - {2} Mo" } },
    { "sampler.loading_progress", { "Loading {0} / {1}...", "Chargement {0} / {1}..." } },
    { "sampler.remove", { "Remove zones", "Supprimer les zones" } },
    { "sampler.selection_all", { "Edits apply to all zones", "Les réglages s'appliquent à toutes les zones" } },
    { "sampler.selection_n", { "Edits apply to {0} selected zone(s)", "Les réglages s'appliquent à {0} zone(s) sélectionnée(s)" } },
    { "import.title", { "Import samples", "Importer des samples" } },
    { "import.example", { "Example name - drag over characters to give them a role", "Nom d'exemple - glisse sur des caractères pour leur donner un rôle" } },
    { "import.delimiter", { "Delimiter", "Délimiteur" } },
    { "import.octave", { "Middle C (MIDI 60) is", "Le do central (MIDI 60) est" } },
    { "import.keys", { "Key ranges", "Étendues de notes" } },
    { "import.velocities", { "Velocity ranges", "Étendues de vélocité" } },
    { "import.strategy_exact", { "None (exact)", "Aucune (exacte)" } },
    { "import.strategy_midpoint", { "Halfway between neighbours", "À mi-chemin des voisines" } },
    { "import.strategy_fixed", { "Fixed spread", "Étendue fixe" } },
    { "import.below", { "below", "sous" } },
    { "import.above", { "above", "au-dessus" } },
    { "import.layers", { "Layers (0 = detect)", "Couches (0 = détecter)" } },
    { "import.rr_mode", { "Round-robin", "Round-robin" } },
    { "import.rr_sequential", { "Sequential", "Séquentiel" } },
    { "import.rr_random", { "Random", "Aléatoire" } },
    { "import.rr_no_repeat", { "Random, no repeat", "Aléatoire sans répétition" } },
    { "import.replace", { "Replace instrument", "Remplacer l'instrument" } },
    { "import.add", { "Add to instrument", "Ajouter à l'instrument" } },
    { "import.cancel", { "Cancel", "Annuler" } },
    { "import.summary", { "{0} files - {1} zones - {2} errors - {3} warnings - {4} layers", "{0} fichiers - {1} zones - {2} erreurs - {3} avertissements - {4} couches" } },
    { "import.ram", { "About {0} MB of audio will be loaded into memory (disk streaming is not available yet)", "Environ {0} Mo d'audio seront chargés en mémoire (le streaming disque n'existe pas encore)" } },
    { "import.ram_blocked", { "Too much audio for memory ({0} MB, limit {1} MB): disk streaming is needed", "Trop d'audio pour la mémoire ({0} Mo, limite {1} Mo) : le streaming disque est nécessaire" } },
    { "import.col_file", { "File", "Fichier" } },
    { "import.col_note", { "Root", "Racine" } },
    { "import.col_keys", { "Keys", "Notes" } },
    { "import.col_velocity", { "Velocity", "Vélocité" } },
    { "import.col_rr", { "RR", "RR" } },
    { "import.col_group", { "Group", "Groupe" } },
    { "import.col_status", { "Status", "État" } },
    { "import.ok", { "OK", "OK" } },
    { "import.rules", { "Fields read from the name (segment, start, min/max length)", "Champs lus dans le nom (segment, début, longueur min/max)" } },
    { "import.segment", { "segment", "segment" } },
    { "import.start", { "start", "début" } },
    { "import.min", { "min", "min" } },
    { "import.max", { "max", "max" } },
    { "role.ignore", { "Ignore", "Ignorer" } },
    { "role.note_number", { "Root note (MIDI number)", "Note racine (numéro MIDI)" } },
    { "role.note_name", { "Root note (name, C3...)", "Note racine (nom, C3...)" } },
    { "role.key_low", { "Low key", "Note basse" } },
    { "role.key_high", { "High key", "Note haute" } },
    { "role.key_below", { "Semitones below root", "Demi-tons sous la racine" } },
    { "role.key_above", { "Semitones above root", "Demi-tons au-dessus de la racine" } },
    { "role.velocity_value", { "Velocity (value)", "Vélocité (valeur)" } },
    { "role.velocity_low", { "Low velocity", "Vélocité basse" } },
    { "role.velocity_high", { "High velocity", "Vélocité haute" } },
    { "role.velocity_layer", { "Velocity layer number", "Numéro de couche de vélocité" } },
    { "role.velocity_layer_count", { "Number of velocity layers", "Nombre de couches de vélocité" } },
    { "role.dynamics", { "Dynamics (pp, mf, ff...)", "Nuance (pp, mf, ff...)" } },
    { "role.round_robin", { "Round-robin (number or letter)", "Round-robin (numéro ou lettre)" } },
    { "role.group", { "Group (mic, articulation...)", "Groupe (micro, articulation...)" } },
    { "role.release", { "Release trigger marker", "Marqueur de déclenchement au relâchement" } },
    { "sampler.import_files", { "Files...", "Fichiers..." } },
    { "sampler.import_folder", { "Folder (with subfolders)...", "Dossier (avec sous-dossiers)..." } },
    { "import.keywords", { "Expressions: a word in the name decides (RR, SUS, REL...)", "Expressions : un mot dans le nom décide (RR, SUS, REL...)" } },
    { "import.add_keyword", { "+ Expression", "+ Expression" } },
    { "import.expr_from_selection", { "Expression from this text", "Expression à partir de ce texte" } },
    { "import.scope_any", { "Anywhere in the name", "N'importe où dans le nom" } },
    { "import.scope_pos", { "At character", "À partir du caractère" } },
    { "import.scope_seg", { "In segment", "Dans le segment" } },
    { "import.value_none", { "No value after the word", "Pas de valeur après le mot" } },
    { "import.trigger_unchanged", { "Trigger: unchanged", "Déclenchement : inchangé" } },
    { "import.trigger_attack", { "Trigger: on note on (sustain)", "Déclenchement : au Note On (sustain)" } },
    { "import.trigger_release", { "Trigger: on release", "Déclenchement : au relâchement" } },
    { "load.title", { "Loaded in memory (uncheck to free memory, recheck to reload)", "Chargé en mémoire (décocher libère la mémoire, recocher recharge)" } },
    { "load.round_robin", { "Round-robin alternates (about {0} MB)", "Alternatives round-robin (environ {0} Mo)" } },
    { "load.release", { "Release samples (about {0} MB)", "Échantillons de relâchement (environ {0} Mo)" } },
    { "load.group", { "Table '{0}' (about {1} MB)", "Table « {0} » (environ {1} Mo)" } },
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
