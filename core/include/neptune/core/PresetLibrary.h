#pragma once
// Bibliothèque de presets sur disque (modèle de données §4) : espace utilisateur en lecture/écriture, espace factory en lecture
// seule (fourni par le plugin, en mémoire), corbeille horodatée. Aucun accès hors du dossier racine donné. Thread de message
// uniquement (jamais le thread audio).
//
//   <racine>/presets/<id>.flipreset      presets utilisateur
//   <racine>/banks.json                  manifeste des banques
//   <racine>/config.json                 configuration globale
//   <racine>/grids/<gridId>.json         grilles concert
//   <racine>/trash/AAAA-MM-JJ_HHhMM_<label>/<id>.flipreset + meta.json (date de suppression, id, nom)
#include <filesystem>
#include <string>
#include <vector>

#include "neptune/core/Preset.h"

namespace neptune::core::preset
{
struct TrashEntry
{
    std::string folder;   // nom du sous-dossier
    std::string label;
    std::string presetId;
    std::string name;
    long long deletedAt = 0; // secondes epoch
};

class Library
{
public:
    // `factory` : presets livrés avec le plugin (marqués `factory`, jamais écrits sur disque ni modifiables).
    explicit Library (std::filesystem::path root, std::vector<Preset> factory = {});

    // Crée les dossiers, lit la configuration, les banques, les grilles et les presets. Les fichiers illisibles sont ignorés
    // et signalés dans `warnings`. Purge la corbeille selon la configuration.
    bool open (std::vector<std::string>* warnings = nullptr);

    const std::filesystem::path& root() const noexcept { return root_; }
    const std::vector<Preset>& userPresets() const noexcept { return user_; }
    const std::vector<Preset>& factoryPresets() const noexcept { return factory_; }
    std::vector<Preset> allPresets() const; // factory puis utilisateur
    const Preset* find (const std::string& id) const;
    std::vector<std::string> allIds() const;

    // ---- écriture (utilisateur) ----
    // Enregistre ; refuse (faux, `error`) un preset factory ou verrouillé (l'appelant propose « Enregistrer sous »).
    bool save (Preset p, std::string* error = nullptr);
    // Copie sous un nouvel id et un nouveau nom : `derivedFrom` = source si elle est factory, déverrouillée.
    bool saveAs (const Preset& source, const std::string& newName, Preset* out = nullptr, std::string* error = nullptr);
    // Importe un preset venu d'ailleurs : garde son id s'il est libre, sinon en prend un nouveau. Jamais factory.
    bool importPreset (Preset p, Preset* out = nullptr, std::string* error = nullptr);
    // Change seulement les métadonnées (nom, type, note, verrou, champs...) d'un preset utilisateur, y compris verrouillé.
    bool updateMetadata (const Preset& edited, std::string* error = nullptr);

    // ---- corbeille ----
    bool moveToTrash (const std::string& id, std::string* error = nullptr); // retire aussi l'id des banques
    std::vector<TrashEntry> trash() const;
    bool restore (const TrashEntry&, std::string* error = nullptr);          // le preset revient ; pas dans les banques
    // Retire les entrées plus vieilles que la rétention et au-delà du maximum (les plus anciennes). now : secondes epoch (-1 : maintenant).
    int purgeTrash (long long nowEpochSeconds = -1);
    bool emptyTrash();

    // ---- banques ----
    const BankManifest& banks() const noexcept { return banks_; }
    bool saveBanks (std::string* error = nullptr);
    bool setBank (const Bank&);                                             // remplace / crée la banque
    // Place un preset (utilisateur ou factory copié) à un emplacement ; `presetId` "" vide l'emplacement.
    bool assign (int bank, int program, const std::string& presetId, std::string* error = nullptr);
    BundleManifest exportManifest (int bank, std::vector<Preset>* presetsOut = nullptr) const;
    // Import d'un bundle dans une banque cible (choisie par l'appelant) : réassigne les positions à partir du premier
    // emplacement libre. Retourne le nombre de presets placés.
    int importBundle (const BundleManifest&, std::vector<Preset> presets, int targetBank, std::string* error = nullptr);

    // ---- configuration ----
    const LibraryConfig& config() const noexcept { return config_; }
    bool setConfig (const LibraryConfig&, std::string* error = nullptr);

    // ---- grilles ----
    const std::vector<Grid>& grids() const noexcept { return grids_; }
    Grid& newGrid (const std::string& name);
    bool saveGrid (const Grid&, std::string* error = nullptr);
    bool deleteGrid (const std::string& gridId);
    std::vector<std::string> gridProblems (const Grid&) const; // ids absents de la bibliothèque

private:
    std::filesystem::path presetPath (const std::string& id) const;
    bool writePreset (const Preset&, std::string* error);

    std::filesystem::path root_;
    std::vector<Preset> factory_, user_;
    BankManifest banks_;
    LibraryConfig config_;
    std::vector<Grid> grids_;
};
} // namespace neptune::core::preset
