#pragma once
// Interface du Sampler au style NOVA, sur le socle commun nova::EditorBase : pages ZONES (liste, matrice notes x vélocité, réglages de la zone, import), SAMPLE (forme d'onde et
// marqueurs de lecture, boucle et fondu enchaîné, fondus, lecture inversée, délai, accordage, gain), SON (enveloppe de la zone, filtres, emplacements assignables), INSTRUMENT (réponse de vélocité,
// polyphonie, mémoire et streaming, Groupes) et SETTINGS. Reprend toute la logique de l'ancienne vue Mapping de Neptune ; les composants existants (matrice, liste, enveloppe, panneaux)
// sont posés dans des panneaux NOVA.
#include <map>
#include "ChokePanel.h"
#include "EnvelopeEditor.h"
#include "FilterEditor.h"
#include "ImportWizard.h"
#include "MappingMatrix.h"
#include "NovaEditor.h"
#include "NovaWaveView.h"
#include "NovaWidgets.h"
#include "PluginProcessor.h"
#include "PolyphonyPanel.h"
#include "SlotsPanel.h"
#include "VelocityPanel.h"
#include "VoicesView.h"
#include "ZoneTable.h"
#include "neptune/core/MemoryBudget.h"

class NovaSamplerEditor : public nova::EditorBase
{
public:
    NovaSamplerEditor (NeptuneProcessor&, const neptune::core::theme::Palette&, neptune::core::theme::KnobStyle, const neptune::core::i18n::Translator&, Options);
    ~NovaSamplerEditor() override;
    bool keyPressed (const juce::KeyPress&) override;
    void setPageForTest (int p) { setPage (p); }

private:
    using Zone = neptune::core::ZoneDescription;

    void layout() override;
    void tick() override;
    void paintBackground (juce::Graphics&) override;
    void pageChanged() override;
    void buildLeftColumn();
    void buildZonesPage();
    void buildSamplePage();
    void buildSoundPage();
    void buildInstrumentPage();

    // ---- logique de l'ancienne vue Mapping
    void editZone (const std::function<void (Zone&)>&);
    std::vector<int> selection() const;
    void setSelection (const std::vector<int>&);
    const Zone* shownZone() const;                 // la première zone sélectionnée (sinon la première) : ce que les commandes affichent
    void refreshFromInstrument();
    void chooseSample();
    void startImport();
    void openWizard (std::vector<std::string> paths);
    void closeWizard();
    void applyImport (const ImportWizard::Result&);
    void removeSelectedZones();
    void openChokePanel (juce::Component& anchor);
    void openSlotsPanel (juce::Component& anchor);
    void showSlotAssignMenu (int zone, const std::string& group);
    void openFilterEditor();
    void closeFilterEditor();
    void updateFilterEditor();
    void updateVoices();
    void rebuildLoadPanel();
    void updateSampleInfo();
    juce::String statusText() const;
    juce::String baseStatusText() const;
    juce::String noteName (int note) const;
    double sampleRate() const;

    NeptuneProcessor& proc_;
    neptune::core::InstrumentDescription cached_;
    std::uint32_t lastRevision_ = 0xFFFFFFFFu;
    bool updating_ = false;
    bool listVisible_ = true, showVoices_ = false;
    std::unique_ptr<juce::FileChooser> chooser_, importChooser_;
    std::unique_ptr<ImportWizard> wizard_;
    std::unique_ptr<FilterEditor> filterEditor_;
    neptune::core::memory::FileInfoCache fileInfo_;
    juce::Component::SafePointer<ChokePanel> chokePanel_;
    juce::Component::SafePointer<SlotsPanel> slotsPanel_;
    std::string groupsSignature_;
    std::vector<std::string> groupNames_;
    struct SeenVoice { neptune::core::Engine::VoiceInfo info; juce::uint32 lastSeen = 0; bool present = false; };
    std::map<std::uint64_t, SeenVoice> seenVoices_;
    int lastVoiceCount_ = -1;
    std::string shownSamplePath_;
    struct GroupMenuListener;
    std::unique_ptr<juce::MouseListener> groupMenuListener_;

    // colonne de gauche
    nova::Panel *outPanel_ = nullptr, *statePanel_ = nullptr, *navPanel_ = nullptr;
    std::vector<nova::KnobCard*> outKnobs_;
    nova::Text* statusText_ = nullptr;
    nova::Pill* voicesPill_ = nullptr;

    // page ZONES
    nova::Panel *zonePanel_ = nullptr, *tablePanel_ = nullptr, *matrixPanel_ = nullptr;
    nova::Pill *loadPill_ = nullptr, *importPill_ = nullptr, *removePill_ = nullptr, *listPill_ = nullptr, *monitorPill_ = nullptr, *modePill_ = nullptr, *chokeMorePill_ = nullptr;
    nova::Text* selectionText_ = nullptr;
    nova::NumberPill* rootNumber_ = nullptr;
    nova::ChoicePill *triggerChoice_ = nullptr, *chokeChoice_ = nullptr;
    std::vector<std::unique_ptr<nova::ChoicePill>> choices_;
    ZoneTable* zones_ = nullptr;
    MappingMatrix* matrix_ = nullptr;
    VoicesView* voicesView_ = nullptr;

    // page SAMPLE
    nova::Panel *wavePanel_ = nullptr, *playPanel_ = nullptr;
    nova::WaveView* wave_ = nullptr;
    nova::Text *infoText_ = nullptr, *markerText_ = nullptr;
    nova::Pill *loopPill_ = nullptr, *reversePill_ = nullptr, *showAllPill_ = nullptr;
    nova::ValueSlider *fadeIn_ = nullptr, *fadeOut_ = nullptr, *delay_ = nullptr, *crossfade_ = nullptr, *tune_ = nullptr, *gain_ = nullptr;
    nova::NumberPill* rootNumber2_ = nullptr;

    // page SON
    nova::Panel *envPanel_ = nullptr, *filterPanel_ = nullptr, *slotsAreaPanel_ = nullptr;
    EnvelopeEditor* env_ = nullptr;
    std::vector<nova::KnobCard*> filterKnobs_;
    nova::Pill *filtersPill_ = nullptr, *slotsPill_ = nullptr;
    nova::Text* slotsText_ = nullptr;

    // page INSTRUMENT
    nova::Panel *velPanel_ = nullptr, *polyPanel_ = nullptr, *memPanel_ = nullptr;
    VelocityPanel* velocity_ = nullptr;
    PolyphonyPanel* poly_ = nullptr;
    juce::ToggleButton *rrToggle_ = nullptr, *releaseToggle_ = nullptr, *streamToggle_ = nullptr;
    juce::ComboBox* preloadBox_ = nullptr;
    juce::Label* preloadLabel_ = nullptr;
    juce::Viewport* groupsViewport_ = nullptr;
    juce::Component groupsHolder_;
    juce::OwnedArray<juce::ToggleButton> groupToggles_;
};
