#pragma once
// Interface d'Orgamon (orgue à roues phoniques, moteur de setBfree) au style NOVA (maquette design/nova/ui/nova_hammond_console_ui.html), sur le socle commun
// nova::EditorBase : pages DRAWBARS (trois claviers de tirettes, percussion, vibrato / chorus, claviers MIDI et splits, clavier à l'écran, potards), LESLIE (vitesse,
// mélange, rotors), LIVE (tirettes agrandies, percussion, Leslie), MATRICE (grille de presets de la banque courante pour le concert), SETTINGS (accord, clic des touches,
// câblage du générateur, contrôleurs MIDI). La bibliothèque de presets (banques, programmes MIDI, gestionnaire, corbeille) est conservée : la pastille de preset de
// l'en-tête ouvre un menu NOVA et le gestionnaire existant se superpose à la fenêtre.
#include "NovaEditor.h"
#include "OrganPlugin.h"
#include "PresetManager.h"
#include "SynthKeyboard.h"

class NovaOrganEditor : public nova::EditorBase
{
public:
    NovaOrganEditor (OrganProcessor&, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&, Options);
    ~NovaOrganEditor() override;
    bool keyPressed (const juce::KeyPress&) override;
    void showPresetMenu (juce::Component* anchor);   // pastille de preset de l'en-tête
    void setPageForTest (int p) { setPage (p); }

private:
    class Drawbar;
    class MidiBar;
    class DescSlider;
    class NumPill;
    class TaperGrid;
    class TileGrid;
    struct MenuPill;
    struct DrawbarSet;
    struct ToggleSet;
    struct LeslieSet;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics&) override;
    void buildLeftColumn();
    void buildDrawbarsPage();
    void buildLeslieAndLivePages();
    void buildMatrixPage();
    void buildSettingsExtras();
    DrawbarSet* makeDrawbars (int page);
    ToggleSet* makeToggles (int page, bool large);
    LeslieSet* makeLeslie (int page);
    MenuPill* makeMenuPill (int page, std::function<juce::String()> label, std::vector<juce::String> items, std::function<int()> get, std::function<void (int)> set);
    void editDesc (const std::function<void (neptune::core::organ::OrganDescription&)>&);
    void openManager (bool saveAs);
    void quickSave();
    juce::String presetLabel() const;

    OrganProcessor& proc_;
    neptune::core::organ::OrganDescription desc_;
    std::vector<std::unique_ptr<DrawbarSet>> owned_sets_;
    std::vector<std::unique_ptr<ToggleSet>> owned_toggles_;
    std::vector<std::unique_ptr<LeslieSet>> owned_leslie_;
    std::vector<std::unique_ptr<MenuPill>> owned_menus_;
    std::uint32_t lastUndoRevision_ = 0;
    std::unique_ptr<PresetManager> manager_;
    juce::String statusText_;

    // colonne de gauche
    nova::Panel *metersPanel_ = nullptr, *navPanel_ = nullptr, *lesliePanel_ = nullptr;
    std::vector<MidiBar*> outBars_;    // niveau de sortie gauche et droite (même taille que les barres MIDI)
    std::vector<MidiBar*> midiBars_;   // vu-mètres de contrôle des canaux MIDI des trois claviers (vélocité d'entrée)
    nova::Text* notesText_ = nullptr;
    LeslieSet* leftLeslie_ = nullptr;

    // page DRAWBARS
    nova::Panel *drawbarsPanel_ = nullptr, *percPanel_ = nullptr, *vibPanel_ = nullptr, *manualsPanel_ = nullptr, *keyboardPanel_ = nullptr;
    DrawbarSet* drawbars_ = nullptr;
    ToggleSet* toggles_ = nullptr;
    std::vector<MenuPill*> menuPills_;
    std::vector<NumPill*> numPills_;
    SynthKeyboard* keyboard_ = nullptr;
    nova::Pill *octDown_ = nullptr, *octUp_ = nullptr, *holdPill_ = nullptr;
    struct Column { nova::Panel* card; nova::KnobCard* knob; };
    std::vector<Column> columns_;
    nova::Pill* drivePill_ = nullptr;
    std::vector<MenuPill*> manualMenus_;   // canaux et transpositions (page DRAWBARS)

    // page LESLIE
    nova::Panel* lesliePagePanel_ = nullptr;
    LeslieSet* leslieSet_ = nullptr;
    nova::KnobCard* leslieMixCard_ = nullptr;
    std::vector<DescSlider*> leslieSliders_;

    // page LIVE
    nova::Panel *livePanel_ = nullptr, *liveLowerPanel_ = nullptr;
    DrawbarSet* liveDrawbars_ = nullptr;
    ToggleSet* liveToggles_ = nullptr;
    LeslieSet* liveLeslie_ = nullptr;

    // page MATRICE
    nova::Panel* matrixPanel_ = nullptr;
    TileGrid* tiles_ = nullptr;
    nova::Pill *colsPill_ = nullptr, *rowsPill_ = nullptr, *bankPrev_ = nullptr, *bankNext_ = nullptr, *bankPill_ = nullptr, *fieldPill_ = nullptr;

    // page SETTINGS
    nova::Panel* organ_settings_panel_ = nullptr;
    nova::Text *tuneSection_ = nullptr, *clickSection_ = nullptr, *wiringSection_ = nullptr, *midiText_ = nullptr;
    std::vector<DescSlider*> settingsSliders_;
    MenuPill* wiringMode_ = nullptr;
    nova::Pill *foldbackPill_ = nullptr, *ccPill_ = nullptr, *panicPill_ = nullptr;
    TaperGrid* taperGrid_ = nullptr;
};

// Éditeur du plugin Orgamon (fenêtre CLAP) : thème, contraste, langue ; reconstruit l'interface quand ils changent.
class NovaOrganPluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit NovaOrganPluginEditor (OrganProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void buildContent();
    OrganProcessor& processor_;
    neptune::core::i18n::Translator tr_;
    std::unique_ptr<NovaOrganEditor> content_;
    juce::ComponentBoundsConstrainer constrainer_;
};
