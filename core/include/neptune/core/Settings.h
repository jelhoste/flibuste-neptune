#pragma once
// Réglages globaux de l'utilisateur (palier 0, point 5) : propres à la machine, pas au projet Reaper.
// Le cœur ne sait pas où est le dossier de configuration : l'adaptateur lui donne un chemin.
// Lecture tolérante : un fichier absent, abîmé ou plus récent ne doit jamais empêcher le plugin de démarrer.
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "neptune/core/Profile.h"

namespace neptune::core::settings
{
constexpr int currentSchemaVersion = 1;

using ::neptune::core::PerformanceProfile; // défini dans Profile.h (partagé avec l'état du projet)

struct GlobalSettings
{
    std::string language = "en";  // code de langue : "en" par défaut, "fr" disponible
    std::string theme = "dark";   // "dark", "light", "hc" (haut contraste), "hammond" (bois) : identifiants de la maquette
    int contrast = 0;             // 0..4, confort visuel (sans effet sur le thème "hc")
    PerformanceProfile profile = PerformanceProfile::Live;

    // Dossiers par défaut ; vide = emplacement par défaut choisi par l'adaptateur.
    std::string samplesDir, presetsDir, irDir, exportDir;

    int autosaveIntervalSeconds = 60; // 0 = autosave désactivé ; sinon 5..3600
    int autosaveKeepDays = 30;        // les autosaves orphelins plus vieux sont supprimés ; 1..365

};

std::string toJson (const GlobalSettings& s);

// Valeur invalide ou inconnue -> valeur par défaut du champ ; chaque correction ajoute un message à `warnings`.
GlobalSettings fromJson (std::string_view text, std::vector<std::string>* warnings = nullptr);

// Fichier absent -> réglages par défaut (pas d'avertissement).
GlobalSettings loadFile (const std::filesystem::path& path, std::vector<std::string>* warnings = nullptr);
bool saveFile (const std::filesystem::path& path, const GlobalSettings& s, std::string* error = nullptr);
} // namespace neptune::core::settings
