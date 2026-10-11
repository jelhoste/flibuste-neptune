#pragma once
// Petits utilitaires fichiers portables (std::filesystem), partagés par les réglages et l'autosave.
#include <filesystem>
#include <string>

namespace neptune::core::fileutil
{
// Écrit `text` dans `path` de façon atomique : fichier temporaire voisin puis renommage.
// Crée les dossiers manquants. Un crash en cours d'écriture ne laisse jamais un fichier tronqué à la place de l'ancien.
bool writeAtomic (const std::filesystem::path& path, const std::string& text, std::string* error = nullptr);

// Lit tout le fichier. false si absent ou illisible.
bool readAll (const std::filesystem::path& path, std::string& out);
} // namespace neptune::core::fileutil
