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
    { "import.ram", { "About {0} MB of audio will be loaded into memory", "Environ {0} Mo d'audio seront chargés en mémoire" } },
    { "import.ram_both", { "In memory: {0} MB if replacing, {1} MB if adding", "En mémoire : {0} Mo en remplaçant, {1} Mo en ajoutant" } },
    { "import.avail", { "available RAM {0} MB, limit {1} MB", "RAM disponible {0} Mo, limite {1} Mo" } },
    { "import.avail_unknown", { "available RAM unknown, fixed limit {0} MB", "RAM disponible inconnue, limite fixe {0} Mo" } },
    { "import.unreadable", { "{0} unreadable file(s) not counted", "{0} fichier(s) illisible(s) non compté(s)" } },
    { "import.ram_hint", { "too much for memory: tick \"Stream from disk\", or untick the alternates / release samples",
                           "trop pour la mémoire : cochez « Streaming disque », ou décochez les alternatives / relâchements" } },
    { "import.light_alternates", { "Load round-robin alternates", "Charger les alternatives round-robin" } },
    { "import.light_stream", { "Stream from disk (only the start stays in memory)", "Streaming disque (seul le début reste en mémoire)" } },
    { "import.light_release", { "Load release samples", "Charger les échantillons de relâchement" } },
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
    { "map.velocity", { "Velocity...", "Vélocité..." } },
    { "vel.fade_width", { "Crossfade width between velocity layers (0 = hard switch)", "Largeur du fondu entre couches de vélocité (0 = commutation franche)" } },
    { "vel.fade_linear", { "Linear crossfade (otherwise constant power)", "Fondu linéaire (sinon : puissance constante)" } },
    { "vel.shape", { "Response", "Réponse" } },
    { "vel.presets", { "Preset", "Préréglage" } },
    { "vel.preset_linear", { "Linear", "Linéaire" } },
    { "vel.preset_soft", { "Soft", "Douce" } },
    { "vel.preset_very_soft", { "Very soft", "Très douce" } },
    { "vel.preset_hard", { "Hard", "Dure" } },
    { "vel.preset_very_hard", { "Very hard", "Très dure" } },
    { "vel.hint", { "Double-click the graph: add a point - drag a point: move - right-click or double-click a point: remove",
                    "Double-clic dans le graphe : ajoute un point - glisser un point : le déplace - clic droit ou double-clic sur un point : le supprime" } },
    { "vel.axis_in", { "velocity received (horizontal) -> velocity used (vertical)", "vélocité reçue (horizontal) -> vélocité utilisée (vertical)" } },
    { "map.polyphony", { "Polyphony...", "Polyphonie..." } },
    { "poly.mode", { "Mode", "Mode" } },
    { "poly.mode_poly", { "Polyphonic", "Polyphonique" } },
    { "poly.mode_mono", { "Mono (retrigger)", "Mono (la note repart)" } },
    { "poly.mode_legato", { "Mono legato (same voice, pitch glides)", "Mono legato (même voix, la hauteur glisse)" } },
    { "poly.limit", { "Voice limit", "Limite de voix" } },
    { "poly.steal", { "When the limit is reached", "Quand la limite est atteinte" } },
    { "poly.steal_oldest", { "Steal the oldest", "Voler la plus ancienne" } },
    { "poly.steal_quietest", { "Steal the quietest", "Voler la plus faible" } },
    { "poly.steal_keep_low", { "Keep the low notes", "Garder les graves" } },
    { "poly.steal_keep_high", { "Keep the high notes", "Garder les aigus" } },
    { "poly.same_key", { "Same key played again", "Même touche rejouée" } },
    { "poly.same_stack", { "Stack (both sound)", "Empiler (les deux sonnent)" } },
    { "poly.same_cut", { "Cut the previous one", "Couper l'ancienne" } },
    { "poly.cut_fade", { "Cut fade", "Fondu de coupure" } },
    { "poly.glide", { "Legato glide", "Glissando legato" } },
    { "poly.hint", { "Mono: one note at a time; releasing the sounding key returns to the previous held key. The cut fade also applies to stolen voices.",
                     "Mono : une seule note à la fois ; relâcher la touche qui sonne revient à la précédente encore tenue. Le fondu de coupure s'applique aussi aux voix volées." } },
    { "voices.button", { "Active voices", "Voix actives" } },
    { "voices.col_sample", { "Sample", "Échantillon" } },
    { "voices.col_key", { "Key", "Touche" } },
    { "voices.col_velocity", { "Vel", "Vél" } },
    { "voices.col_zone", { "Zone", "Zone" } },
    { "voices.col_state", { "State", "État" } },
    { "voices.col_layer", { "Layer", "Couche" } },
    { "voices.col_level", { "Level", "Niveau" } },
    { "voices.col_position", { "Position", "Position" } },
    { "voices.col_source", { "Source", "Source" } },
    { "voices.state_held", { "held", "tenue" } },
    { "voices.state_release", { "releasing", "relâchement" } },
    { "voices.state_fading", { "cut (fade)", "coupée (fondu)" } },
    { "voices.state_preview", { "preview", "écoute" } },
    { "voices.state_finished", { "finished", "terminée" } },
    { "voices.source_ram", { "RAM", "RAM" } },
    { "voices.source_disk", { "disk", "disque" } },
    { "voices.summary", { "{0} voice(s): {1} held, {2} releasing, {3} fading - stream underruns: {4}", "{0} voix : {1} tenue(s), {2} en relâchement, {3} coupée(s) - coupures du streaming : {4}" } },
    { "voices.none", { "no voice sounding", "aucune voix ne sonne" } },
    { "choke.cuts_short", { "Cuts", "Coupe" } },
    { "choke.cut_by_short", { "Cut by", "Coupée par" } },
    { "trig.label", { "Trigger", "Déclenchement" } },
    { "trig.attack", { "Attack", "Attaque" } },
    { "trig.release", { "Release", "Relâchement" } },
    { "trig.first", { "First", "Premier" } },
    { "trig.legato", { "Legato (transition)", "Legato (transition)" } },
    { "choke.label", { "Choke", "Choke" } },
    { "choke.none", { "None", "Aucun" } },
    { "choke.advanced", { "Advanced", "Avancé" } },
    { "choke.cuts", { "Cuts group", "Coupe le groupe" } },
    { "choke.cut_by", { "Is cut by group", "Est coupée par le groupe" } },
    { "choke.hint", { "The simple setting (one number) sets both: the zones of the group cut each other. Here you can make a zone cut a group without being cut by it.",
                      "Le réglage simple (un seul numéro) règle les deux : les zones du groupe se coupent entre elles. Ici une zone peut couper un groupe sans être coupée par lui." } },
    { "poly.choke_same", { "A choke also cuts the voices of the same zone (round-robin alternates included)",
                           "Un choke coupe aussi les voix de la même zone (alternatives round-robin comprises)" } },
    { "map.tab", { "Mapping", "Mapping" } },
    { "map.list", { "Zone list", "Liste des zones" } },
    { "map.view_main", { "Attack zones", "Zones d'attaque" } },
    { "map.view_first", { "\"First\" zones", "Zones « Premier »" } },
    { "map.view_legato", { "\"Legato\" zones (transitions)", "Zones « Legato » (transitions)" } },
    { "map.view_release", { "Release zones", "Zones de relâchement" } },
    { "map.view_group", { "Group: {0}", "Groupe : {0}" } },
    { "map.fit", { "Fit", "Tout voir" } },
    { "map.duplicate", { "Duplicate", "Dupliquer" } },
    { "map.empty", { "No zone in this view. Import samples first.", "Aucune zone dans cette vue. Importez d'abord des samples." } },
    { "map.hint", { "Drag: move (Shift: root follows) - Ctrl+drag: duplicate - edge: resize - wheel: zoom - keyboard: bottom = loud, Shift+click = hold",
                    "Glisser : déplacer (Maj : racine suit) - Ctrl+glisser : dupliquer - bord : étirer - molette : zoom - clavier : bas = fort, Maj+clic = maintenir" } },
    { "load.stream", { "Stream from disk (only the start of each sample stays in memory)", "Streaming disque (seul le début de chaque sample reste en mémoire)" } },
    { "load.preload", { "Preload", "Préchargement" } },
    { "sampler.stream_status", { " - streaming: {0} dropout(s)", " - streaming : {0} coupure(s)" } },
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
