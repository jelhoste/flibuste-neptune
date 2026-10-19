#pragma once
// Socle commun des éditeurs de la série NOVA (compresseur, égaliseur, ...) : habillage et échelle (NovaKit), en-tête (marque, preset,
// bypass), menu latéral de pages, barre du bas, A/B, modale des potards, minuterie d'actualisation à 30 Hz, annuler / rétablir.
// Les éditeurs dérivés ne font que créer leurs composants (add) et les placer (layout) en coordonnées de la maquette (1160 x 780).
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <map>
#include <memory>
#include <vector>
#include "KnobModal.h"
#include "NovaKit.h"
#include "neptune/core/I18n.h"
#include "neptune/core/Settings.h"

namespace nova
{
class EditorBase : public juce::Component, private juce::Timer
{
public:
    struct Options
    {
        bool brand = false;    // plugin séparé : marque et bouton de bypass dans l'en-tête (dans un insert, la fenêtre du plugin les fournit)
        std::function<double()> sampleRate;
        juce::String moduleName;   // « compressor », « equalizer » : dossier des presets utilisateur
        // plugin séparé seulement : réglages globaux de la suite (thème, contraste, langue) ; `applySettings` les enregistre et reconstruit l'éditeur
        std::function<neptune::core::settings::GlobalSettings()> getSettings;
        std::function<void (const neptune::core::settings::GlobalSettings&)> applySettings;
    };

    static constexpr int designWidth = 1160, designHeight = 780;
    // Plage d'échelle de la fenêtre. 1,0 au maximum pour l'instant (taille fixe d'abord) ; l'interface sait déjà s'agrandir sans transformation.
    static constexpr float minScale = 0.6f, maxScale = 1.0f;

    EditorBase (ParamHost&, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&,
                Options, juce::String word);
    ~EditorBase() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // hors minuterie (rendu hors-ligne, tests)
    bool keyPressed (const juce::KeyPress&) override;

protected:
    // ---- à fournir par l'éditeur dérivé
    virtual void layout() = 0;                                     // place tous les composants (coordonnées de la maquette via R())
    virtual void tick() {}                                         // 30 Hz, après le rafraîchissement des composants liés
    virtual void paintBackground (juce::Graphics&) {}              // sous les composants (page courante : currentPage())
    virtual void pageChanged() {}
    virtual std::vector<juce::String> presetNames() const { return {}; }
    virtual std::vector<std::pair<neptune::core::ParamId, double>> presetValues (int /*index*/) const { return {}; }
    virtual std::vector<neptune::core::ParamId> presetParamIds() const { return {}; }

    // ---- services
    ParamHost& host() { return host_; }
    Skin& skin() { return skin_; }
    const Skin& skin() const { return skin_; }
    juce::String tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }
    const neptune::core::i18n::Translator& translator() const { return tr_; }
    double value (neptune::core::ParamId id) const { return host_.parameters().get (id); }
    int choiceValue (neptune::core::ParamId id) const { return static_cast<int> (std::lround (value (id))); }
    void openModal (neptune::core::ParamId, neptune::core::uistate::Terminal);
    NeptuneKnob::OpenModal modalOpener() { return [this] (neptune::core::ParamId id, neptune::core::uistate::Terminal t) { openModal (id, t); }; }

    // Rectangle de la maquette (x, y, largeur, hauteur) converti dans la fenêtre actuelle (échelle et centrage compris)
    juce::Rectangle<int> R (float x, float y, float w, float h) const
    {
        return { frame_.getX() + skin_.ipx (x), frame_.getY() + skin_.ipx (y), skin_.ipx (w), skin_.ipx (h) };
    }
    juce::Rectangle<int> frame() const { return frame_; }

    // Crée un composant, l'ajoute à la fenêtre et à une page (-1 : toutes les pages, -2 : toutes sauf la page SETTINGS), l'enregistre pour le rafraîchissement s'il est Refreshable.
    template <class T, class... Args>
    T* add (int page, Args&&... args)
    {
        auto owned = std::make_unique<T> (std::forward<Args> (args)...);
        T* raw = owned.get();
        addChild (raw, page);
        owned_.push_back (std::move (owned));
        return raw;
    }
    // Bouton « ? » rattaché à un composant (à sa droite, ou dans son coin supérieur droit) ; les textes sont les clés `hlp.<key>.t` et `hlp.<key>`.
    // pos : 0 à droite du composant, 1 dans son coin supérieur droit, 2 en dessous (centré)
    InfoButton* addInfo (int page, juce::Component* target, const char* key, int pos = 0);
    int addPage (juce::String label, juce::Path icon);
    // Page SETTINGS commune : thème, contraste, langue (plugin séparé), dossier des presets utilisateur, à propos. À appeler après addPage.
    void buildSettingsPage (int page);
    // Place les entrées du menu latéral, centrées dans la zone [y, y + h] de la maquette
    void layoutNav (float zoneY, float zoneH);   // entrée du menu latéral ; retourne l'indice de la page
    void setPage (int);
    int currentPage() const noexcept { return page_; }
    int numPages() const noexcept { return static_cast<int> (navs_.size()); }
    NavItem* nav (int i) { return navs_[static_cast<std::size_t> (i)]; }

    Pill* abPill() { return ab_; }
    Pill* presetPill() { return preset_; }
    Pill* bypassPill() { return bypass_; }
    const Options& options() const { return options_; }

    // Zones communes de la maquette
    static constexpr float headerH = 58.0f, bottomH = 56.0f;

private:
    struct PageItem { juce::Component* c; int page; };
    struct InfoPlace { InfoButton* button; juce::Component* target; int pos; };
    void placeInfos();
    std::vector<InfoPlace> infos_;
    void addChild (juce::Component*, int page);
    void timerCallback() override;
    void closeModal();
    void layoutSettings();
    void refreshSettingsTexts();
    juce::File userPresetsDir() const;
    void saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File&);
    void askName (const juce::String& title, std::function<void (juce::String)> done);
    void showPresetMenu();
    void toggleAB();
    std::map<neptune::core::ParamId, double> snapshot() const;
    void restore (const std::map<neptune::core::ParamId, double>&);

    ParamHost& host_;
    Skin skin_;
    LookAndFeel look_;
    const neptune::core::i18n::Translator& tr_;
    Options options_;
    juce::String word_;
    juce::Rectangle<int> frame_;
    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<PageItem> items_;
    std::vector<Refreshable*> refreshables_;
    std::vector<NavItem*> navs_;
    Pill *preset_ = nullptr, *ab_ = nullptr, *bypass_ = nullptr;
    std::unique_ptr<KnobModal> modal_;
    std::unique_ptr<juce::Component> nameEntry_;
    int settingsPage_ = -1;
    Panel* setPanel_ = nullptr;
    Pill *themePill_ = nullptr, *contrastPill_ = nullptr, *langPill_ = nullptr, *openPill_ = nullptr;
    Text *setGeneral_ = nullptr, *setNote_ = nullptr, *setPresets_ = nullptr, *setFolder_ = nullptr, *setAbout_ = nullptr, *setAboutText_ = nullptr;
    int page_ = 0, presetIndex_ = 0;
    bool slotB_ = false;
    std::map<neptune::core::ParamId, double> otherSlot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorBase)
};
} // namespace nova
