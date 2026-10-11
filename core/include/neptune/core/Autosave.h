#pragma once
// Autosave périodique + marqueur de crash (palier 0, point 5).
//
// Principe : chaque instance du plugin a un identifiant (32 caractères hexadécimaux) inscrit dans son état de projet.
// Elle écrit son état courant dans <dossier>/<id>.neptuneautosave, et supprime ce fichier à la fermeture propre.
// Un fichier resté là, dont le processus propriétaire n'existe plus, est la trace d'un crash. Au rechargement du projet,
// l'instance retrouve son identifiant dans l'état du projet, donc son autosave : elle peut proposer de le restaurer.
//
// Le cœur ne décide ni du dossier ni de l'interface : l'adaptateur fournit le chemin et appelle write() périodiquement.
// Toutes les méthodes sont protégées par un verrou : hors thread audio (fichiers, allocations).
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace neptune::core
{
struct RecoveredState
{
    std::string state;          // document d'état (State.h)
    std::int64_t savedAt = 0;   // secondes Unix, instant de l'autosave
};

class AutosaveStore
{
public:
    explicit AutosaveStore (std::filesystem::path directory);
    ~AutosaveStore(); // libère l'identifiant ; ne supprime PAS le fichier (un crash doit le laisser)

    AutosaveStore (const AutosaveStore&) = delete;
    AutosaveStore& operator= (const AutosaveStore&) = delete;

    const std::string& id() const noexcept { return id_; }

    // Identifiant valide = exactement 32 caractères [0-9a-f]. Il sert de nom de fichier : tout le reste est refusé
    // (un état de projet est une entrée non fiable).
    static bool isValidId (std::string_view id) noexcept;

    // Prend l'identifiant lu dans l'état du projet, s'il est valide et pas déjà utilisé par une autre instance de ce processus
    // (piste dupliquée : les deux copies portent le même identifiant). Retourne false et garde l'identifiant courant sinon.
    bool adoptId (const std::string& candidate);

    // Texte d'état « déjà sauvegardé » : write() n'écrit rien tant que l'état courant lui est identique.
    void setBaseline (std::string_view stateText);

    // Écrit l'état courant (atomique). Ne fait rien si identique à la dernière écriture ou à la base.
    bool write (std::string_view stateText, std::string* error = nullptr);

    // Fermeture propre : supprime l'autosave de cette instance.
    void discard();

    // À appeler juste après adoptId() et avant toute écriture. Retourne l'état laissé par une session précédente de cette
    // instance, s'il y en a un. Le fichier est conservé (renommé .neptunerecovered) jusqu'à resolveRecovered().
    std::optional<RecoveredState> takeRecoverable();
    void resolveRecovered(); // l'utilisateur a restauré ou refusé : supprime le fichier conservé

    // Supprime les autosaves orphelins (processus mort) et les états conservés plus vieux que `keepDays` jours.
    // Retourne le nombre de fichiers supprimés.
    static int purgeStale (const std::filesystem::path& directory, int keepDays);

    // Utilitaires de processus (exposés pour les tests).
    static std::int64_t currentProcessId();
    static bool isProcessAlive (std::int64_t pid);

private:
    std::filesystem::path autosavePath() const;
    std::filesystem::path recoveredPath() const;

    std::filesystem::path dir_;
    std::string id_;
    std::string lastWritten_;
    bool haveLast_ = false;
    mutable std::mutex mutex_;
};
} // namespace neptune::core
