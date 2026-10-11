#pragma once
// Internationalisation (palier 0) : anglais par défaut, français disponible. Sans JUCE, sans fichier à installer :
// les tables sont compilées dans le plugin (un .clap = un seul fichier).
//
// Règles :
//  - chaque texte visible par l'utilisateur est désigné par une clé (« recovery.restore »), jamais écrit en dur ;
//  - l'anglais est la langue de référence : une clé sans traduction dans la langue demandée retombe sur l'anglais,
//    une clé inconnue retombe sur la clé elle-même (visible, donc repérable) ;
//  - les textes à trous utilisent {0}, {1}... (l'ordre des mots change selon la langue) ;
//  - le nom du produit « Neptune » n'est pas traduit : il n'a pas de clé.
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace neptune::core::i18n
{
struct Language
{
    std::string_view code; // valeur de settings.json ("en", "fr")
    std::string_view name; // dans sa propre langue, pour le sélecteur
};

const std::vector<Language>& languages();
bool isSupported (std::string_view code);

class Translator
{
public:
    // Code inconnu -> anglais.
    explicit Translator (std::string_view languageCode = "en");

    std::string_view language() const noexcept { return code_; }
    std::string_view tr (std::string_view key) const;
    std::string format (std::string_view key, std::initializer_list<std::string_view> args) const;

private:
    std::string_view code_;
    int index_;
};

// Remplace {0}, {1}... ; un indice sans argument est laissé tel quel.
std::string substitute (std::string_view pattern, std::initializer_list<std::string_view> args);

// Pour les tests de cohérence des tables.
struct EntryView
{
    std::string_view key;
    std::vector<std::string_view> text; // une par langue, dans l'ordre de languages() ; vide = non traduit
};
std::vector<EntryView> allEntries();
} // namespace neptune::core::i18n
