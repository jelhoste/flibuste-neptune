#pragma once
// Système de presets (modèle de données « modele-donnees-presets », décisions du 2026-10-15). Commun à tous les plugins de la
// suite (instruments et effets). Cœur portable : aucune dépendance à JUCE ni à l'hôte.
//
//  - Preset          : un fichier `.flipreset` (JSON). L'appartenance à une banque n'est PAS structurelle.
//  - BankManifest    : `banks.json` (banques 0..127 alignées sur MIDI Bank Select, 128 programmes chacune, par ids).
//  - LibraryConfig   : configuration globale (défauts de capture, pédale, corbeille).
//  - Grid            : grille concert (tuiles qui référencent des presets).
//  - BundleManifest  : manifeste du bundle `.flibank` (archive zip faite par l'adaptateur).
//  - Query / matches : recherche à facettes (OR entre valeurs d'une même clé, AND entre clés).
//  - Navigator       : stepper et MIDI Bank Select / Program Change (sans état audio).
// Lecture tolérante partout : champ absent ou invalide -> défaut ; clé inconnue ignorée ; version supérieure acceptée.
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::preset
{
constexpr int schemaVersion = 1;
constexpr int numBanks = 128;
constexpr int programsPerBank = 128;
constexpr int maxRating = 3;

struct CustomField
{
    std::string key, value;
    bool operator== (const CustomField& o) const noexcept { return key == o.key && value == o.value; }
};

enum class Capture { Captured, Free };
enum class PedalBehavior { Default, Isolate, Immediate };

const char* toString (Capture) noexcept;        // "captured" / "free"
const char* toString (PedalBehavior) noexcept;  // "default" / "isolate" / "immediate"

struct ParamValue
{
    bool isText = false;
    double number = 0.0;
    std::string text;
    static ParamValue ofNumber (double v) { ParamValue p; p.number = v; return p; }
    static ParamValue ofText (std::string s) { ParamValue p; p.isText = true; p.text = std::move (s); return p; }
    bool operator== (const ParamValue& o) const noexcept { return isText == o.isText && number == o.number && text == o.text; }
};

struct Preset
{
    int version = schemaVersion;
    std::string id;                                // identifiant stable (UUID v4), indépendant du nom
    std::string name = "Init";
    std::string type;                              // taxonomie libre : "Lead", "Pad", "Dynamique"...
    std::vector<std::string> characteristics;
    int rating = 0;                                // 0 = non noté, 1..3 étoiles
    bool locked = false;                           // verrou utilisateur : « Enregistrer » est redirigé vers « Enregistrer sous »
    std::vector<CustomField> customFields;
    std::string derivedFrom;                       // id du preset factory d'origine ; "" = aucun (purement informatif)

    std::map<std::string, ParamValue> parameters;       // seuls les paramètres capturés ; absent = valeur courante inchangée
    std::map<std::string, Capture> captureOverrides;    // exceptions au défaut global, pour CE preset

    // Extension du modèle (état structuré d'un module : formes d'onde, enveloppes, filtres...), propre au plugin.
    std::string module;                            // identifiant du module qui sait lire `state` ("analogsynth")
    std::string state;                             // objet JSON sérialisé ; "" = aucun
    // Façade de la piste d'origine (Neptune) : objet JSON {"track":n,"facade":{...}}, images désignées par « asset:<nom> » (fichiers
    // copiés sous <bibliothèque>/facade-assets) ; "" = aucune. Les autres plugins la conservent sans la lire.
    std::string facade;

    // Effets uniquement : conservés tels quels.
    std::string reverbEngine;                      // "light" | "convolution" | ""
    std::string reverbIrRef;
    int declaredLatencySamples = 0;

    PedalBehavior pedalHeldOverride = PedalBehavior::Default;

    bool factory = false;                          // origine (jamais écrit dans le fichier)

    bool operator== (const Preset& o) const noexcept;
};

std::string newId();                               // UUID v4
Preset sanitised (Preset);                         // bornes de longueur, note 0..3, valeurs finies, id non vide
std::string toJson (const Preset&);                // UTF-8, indenté
// `ok` (facultatif) : faux si le texte n'est pas un objet JSON exploitable (le preset rendu est alors un « Init »).
Preset presetFromJson (std::string_view text, bool* ok = nullptr);

// ---- configuration globale ----
struct ReverbPool
{
    int maxConvolutionInstances = 2;
    int maxLightInstances = 3;
    int forcedFadeOutMsLight = 30;
    int forcedFadeOutMsConvolution = 150;
    bool operator== (const ReverbPool& o) const noexcept
    {
        return maxConvolutionInstances == o.maxConvolutionInstances && maxLightInstances == o.maxLightInstances
            && forcedFadeOutMsLight == o.forcedFadeOutMsLight && forcedFadeOutMsConvolution == o.forcedFadeOutMsConvolution;
    }
};

struct LibraryConfig
{
    std::map<std::string, Capture> parameterCaptureDefaults;   // clé de paramètre -> capturé / libre (absent : capturé)
    PedalBehavior pedalHeldBehaviorDefault = PedalBehavior::Isolate; // Default n'a pas de sens ici (ramené à Isolate)
    ReverbPool reverbPool;
    int trashRetentionDays = 14;
    int trashMaxEntries = 50;

    bool operator== (const LibraryConfig& o) const noexcept
    {
        return parameterCaptureDefaults == o.parameterCaptureDefaults && pedalHeldBehaviorDefault == o.pedalHeldBehaviorDefault
            && reverbPool == o.reverbPool && trashRetentionDays == o.trashRetentionDays && trashMaxEntries == o.trashMaxEntries;
    }
};

std::string toJson (const LibraryConfig&);
LibraryConfig configFromJson (std::string_view text);

// Statut effectif d'un paramètre : exception du preset, sinon défaut global, sinon capturé.
Capture captureOf (const std::string& key, const Preset&, const LibraryConfig&) noexcept;
PedalBehavior pedalBehaviorOf (const Preset&, const LibraryConfig&) noexcept;

// ---- banques ----
struct Bank
{
    int index = 0;                                 // 0..127 (MIDI Bank Select)
    std::string name;                              // "" : lettre calculée
    std::vector<CustomField> customFields;
    bool wrapToNextBank = false;                   // stepper : en fin de banque, passer à la banque suivante (sinon reboucler)
    std::vector<std::string> presets;              // ids par numéro de programme (taille 128, "" = emplacement vide)

    Bank() : presets (programsPerBank) {}
    bool operator== (const Bank& o) const noexcept
    {
        return index == o.index && name == o.name && customFields == o.customFields && wrapToNextBank == o.wrapToNextBank
            && presets == o.presets;
    }
};

std::string defaultBankName (int index);           // 0 -> "A" ... 25 -> "Z", 26 -> "A1" ... 51 -> "Z1", ...
std::string displayName (const Bank&);

struct BankManifest
{
    int version = schemaVersion;
    std::vector<Bank> banks;                       // triées par index, index uniques

    Bank* find (int index) noexcept;
    const Bank* find (int index) const noexcept;
    Bank& ensure (int index);                      // crée la banque si elle n'existe pas
    // Retire l'id partout (preset supprimé) ; retourne le nombre d'emplacements vidés.
    int forget (const std::string& id);
    bool operator== (const BankManifest& o) const noexcept { return banks == o.banks; }
};

std::string toJson (const BankManifest&);
BankManifest banksFromJson (std::string_view text);

// ---- grille concert ----
struct GridTile
{
    int position = 0;
    std::string presetRef;                         // "" : tuile vide, réservée mais non assignée
    bool operator== (const GridTile& o) const noexcept { return position == o.position && presetRef == o.presetRef; }
};

struct Grid
{
    int version = schemaVersion;
    std::string gridId;
    std::string name;
    bool locked = true;                            // verrou de la disposition (distinct du verrou de contenu d'un preset)
    std::string linkedConcertField;                // suggestion de pré-remplissage, jamais recalculée
    std::vector<GridTile> tiles;                   // tuiles ordonnées par position, positions uniques
    static constexpr int maxTiles = 256;

    // Assigne (écrase) ; retourne l'id précédemment présent ("" si la tuile était vide ou nouvelle).
    std::string assign (int position, const std::string& presetId);
    // Échange deux tuiles (glisser-déposer). Faux si une position est hors limites.
    bool swapTiles (int a, int b);
    // Ids référencés qui n'existent pas dans `known` (vérification d'intégrité).
    std::vector<std::string> missing (const std::vector<std::string>& known) const;
    bool operator== (const Grid& o) const noexcept
    {
        return gridId == o.gridId && name == o.name && locked == o.locked && linkedConcertField == o.linkedConcertField && tiles == o.tiles;
    }
};

std::string toJson (const Grid&);
Grid gridFromJson (std::string_view text);

// ---- bundle de banque exportée ----
struct BundleManifest
{
    int version = schemaVersion;
    std::string bankName;
    std::vector<CustomField> bankCustomFields;
    std::vector<std::string> presetOrder;          // ids (pas de positions : l'import réassigne les positions)
    bool operator== (const BundleManifest& o) const noexcept
    {
        return bankName == o.bankName && bankCustomFields == o.bankCustomFields && presetOrder == o.presetOrder;
    }
};

std::string toJson (const BundleManifest&);
BundleManifest bundleFromJson (std::string_view text);
constexpr const char* presetExtension = ".flipreset";
constexpr const char* bundleExtension = ".flibank";

// ---- recherche à facettes ----
struct Query
{
    std::string text;                              // sous-chaîne du nom (insensible à la casse)
    std::string type;                              // "" : tous
    std::vector<std::string> characteristics;      // OR entre elles
    int minRating = 0;
    std::vector<CustomField> facets;               // OR entre valeurs d'une même clé, AND entre clés
    bool lockedOnly = false;
};

bool matches (const Preset&, const Query&);
std::vector<const Preset*> filter (const std::vector<Preset>&, const Query&); // tri : nom, insensible à la casse

// ---- navigation : stepper et MIDI ----
struct Slot
{
    int bank = 0;
    int program = 0;
    bool operator== (const Slot& o) const noexcept { return bank == o.bank && program == o.program; }
};

class Navigator
{
public:
    // Id du preset à l'emplacement ; "" si vide ou hors limites.
    static std::string presetAt (const BankManifest&, Slot);
    // Emplacement suivant (dir > 0) ou précédent (dir < 0) non vide, selon wrapToNextBank. Absent s'il n'y en a aucun.
    static std::optional<Slot> step (const BankManifest&, Slot current, int dir);
    // Emplacement du preset `id` (premier trouvé).
    static std::optional<Slot> locate (const BankManifest&, const std::string& id);

    // MIDI : CC 0 (Bank Select MSB) choisit la banque (0..127, valeur = index) ; Program Change choisit le programme.
    void bankSelect (int value) noexcept { bank_ = value < 0 ? 0 : value > 127 ? 127 : value; }
    int bank() const noexcept { return bank_; }
    // Id du preset visé ; "" si l'emplacement est vide : l'appelant garde le preset actuel (aucune interruption).
    std::string programChange (const BankManifest&, int program) const;

private:
    int bank_ = 0;
};
} // namespace neptune::core::preset
