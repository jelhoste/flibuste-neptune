#pragma once
// État de l'interface mémorisé d'une session à l'autre (palier 0) : distinct des réglages de l'utilisateur
// (Settings.h), que l'on édite à la main. Fichier `ui_state.json` du dossier de configuration.
// Pour l'instant : taille de la modale des potards, une par type de terminal (souris / tactile), spec-modules.md l.22.
// Lecture tolérante : un fichier absent ou abîmé donne les valeurs par défaut, jamais d'erreur bloquante.
#include <filesystem>
#include <string>
#include <string_view>

namespace neptune::core::uistate
{
constexpr int currentSchemaVersion = 1;

struct Size
{
    int width = 0, height = 0;
    bool operator== (const Size& o) const noexcept { return width == o.width && height == o.height; }
};

enum class Terminal { Pointer, Touch };

constexpr int minModalSide = 200;
constexpr int maxModalSide = 2000;

struct UiState
{
    // Défauts : plus grand au tactile (cibles de 44 px et doigt).
    Size modalPointer { 380, 230 };
    Size modalTouch { 500, 320 };

    // Dernier gabarit de nom utilisé à l'import (JSON de neptune::core::import::Template) ; vide = aucun.
    std::string lastImportTemplate;

    const Size& modalSize (Terminal t) const noexcept { return t == Terminal::Touch ? modalTouch : modalPointer; }
    // Borné à [minModalSide, maxModalSide].
    void setModalSize (Terminal t, Size s) noexcept;
    bool operator== (const UiState& o) const noexcept
    {
        return modalPointer == o.modalPointer && modalTouch == o.modalTouch && lastImportTemplate == o.lastImportTemplate;
    }
};

std::string toJson (const UiState& s);
UiState fromJson (std::string_view text);
UiState loadFile (const std::filesystem::path& path);
bool saveFile (const std::filesystem::path& path, const UiState& s, std::string* error = nullptr);
} // namespace neptune::core::uistate
