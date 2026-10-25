#pragma once
// Interface du Sampler au style NOVA, sur le socle commun nova::EditorBase : pages ZONES (liste, matrice notes x vélocité, réglages de la zone, import), SAMPLE (forme d'onde et
// marqueurs de lecture, boucle et fondu enchaîné, fondus, lecture inversée, délai, accordage, gain), SON (enveloppe de la zone, filtres, emplacements assignables), INSTRUMENT (réponse de vélocité,
// polyphonie, mémoire et streaming, Groupes) et SETTINGS. Reprend toute la logique de l'ancienne vue Mapping de Neptune ; les composants existants (matrice, liste, enveloppe, panneaux)
// sont posés dans des panneaux NOVA.
#include <atomic>
#include <future>
#include <mutex>
#include <memory>
#include <map>
#include <thread>
#include "ChokePanel.h"
#include "EnvelopeEditor.h"
#include "FilterEditor.h"
#include "ImportWizard.h"
#include "MappingMatrix.h"
#include "MidiFilterEditor.h"
#include "PoolTable.h"
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
#include "neptune/core/SampleEdit.h"
#include "neptune/core/PitchDetect.h"
#include "neptune/core/PreviewSample.h"
#include "neptune/core/SampleLoader.h"
#include "neptune/core/SampleMeta.h"
#include "neptune/core/TimeStretch.h"
#include "neptune/core/VolumeFootprint.h"

class ModulatorsPanel;

class NovaSamplerEditor : public nova::EditorBase, public juce::DragAndDropContainer
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
    void openLevelsPanel (juce::Component& anchor);
    void openMidiCondition();
    void openModulators();
    neptune::core::EnvelopeDescription parentEnvelope (const Zone&) const;
    neptune::core::EnvelopeDescription shownEnvelope (bool& editable, bool& linked, juce::String& note) const;
    void applyEnvelope (const neptune::core::EnvelopeDescription&);
    void toggleEnvelopeLink();
    void rebuildPool (bool force = false);
    void requestPitch (const std::string& path);
    bool pitchOf (const std::string& path, neptune::core::PitchResult& out);
    juce::String pitchText (const std::string& path);
    void applyDetectedPitch();
    void openHumanizePanel (juce::Component& anchor);
    void addToPool (const std::vector<std::string>& paths);
    void placeFiles (const std::vector<int>& files, int firstKey, const neptune::core::mapping::View&);
    void showPoolMenu (int row);
    void openSlotsPanel (juce::Component& anchor);
    void showSlotAssignMenu (int zone, const std::string& group);
    // Groupes : choix du sous-menu « Groupe » (affecter les zones, nouveau, renommer, supprimer) et saisie d'un nom.
    void groupMenuChoice (int choice, int zone, const std::string& group, const std::vector<std::string>& names);
    void askGroupName (const juce::String& title, const juce::String& initial, std::function<void (std::string)> done);
    void openFilterEditor();
    void closeFilterEditor();
    void updateFilterEditor();
    void updateVoices();
    void updatePlayButton();
    void startPreview (int source, std::int64_t frame, int loopIndex, int owner);
    void stopPreviewNow();
    void previewFile (const std::string& path);
    void togglePoolPreview (int row);
    void pollPreviewLoad();
    void previewSeam();
    void toggleRecording();
    void finishRecording (const std::string& path, std::int64_t frames);
    void updateRecordButton();
    void rebuildLoadPanel();
    void updateSampleInfo();

    // ---- outils du sample (lot 2) : opérations sur une copie de travail, analyses
    int shownIndex() const;
    void editShownZone (const std::function<void (Zone&)>&);
    bool writeDerived (const Zone&, const char* tag, const neptune::core::sampleedit::Buffer&, std::string& newPath);
    std::int64_t snapFrame (const std::string& path, std::int64_t frame) const;
    void setToolStatus (const juce::String&);
    void toolTrim();
    void toolNormalize();
    void toolRemoveDc();
    void toolSilence();
    void toolSplitHits();
    void toolAutoLoop();
    void toolSaveAs();
    void addLoop();
    void showAddLoopMenu();
    void addAllAutoLoops();
    std::int64_t fileMarkerFrame (const std::string& path);
    std::int64_t effectiveMarker (const Zone&);
    void saveMarker (const std::string& path, std::int64_t frame);
    neptune::core::SampleMeta metaOfZone (const Zone&);
    void applyFileMarks (Zone&, bool withRoot);
    void syncFileMeta();
    void flushFileMeta();
    void showWaveMenu (std::int64_t frame, juce::Point<int> where);
    void openLoopOptions (juce::Component& anchor);
    void removeLoop();
    void toolStretch();
    void updateSeam();
    void updateFootprint();
    int primaryCurve() const;
    void footprintLoudest();
    void footprintPeak (int direction);
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
    std::unique_ptr<ModulatorsPanel> modulatorsPanel_;   // fenêtre des modulateurs
    std::unique_ptr<MidiFilterEditor> midiEditor_;   // condition MIDI de la zone (superposé)
    neptune::core::memory::FileInfoCache fileInfo_;
    juce::Component::SafePointer<ChokePanel> chokePanel_;
    std::unique_ptr<SlotsPanel> slotsPanel_;   // fenêtre des emplacements assignables
    std::string groupsSignature_;
    std::vector<std::string> groupNames_;
    struct SeenVoice { neptune::core::Engine::VoiceInfo info; juce::uint32 lastSeen = 0; bool present = false; };
    std::map<std::uint64_t, SeenVoice> seenVoices_;
    int lastVoiceCount_ = -1;
    std::string shownSamplePath_;
    struct GroupMenuListener;
    // empreintes de volume
    int curveMask_ = 0, rangeLo_ = -30, rangeHi_ = 0, navPoint_ = -1;
    bool rangeOn_ = false;
    std::string footprintPath_;
    std::shared_ptr<const neptune::core::VolumeFootprint> footprint_;
    std::future<std::shared_ptr<const neptune::core::VolumeFootprint>> footprintJob_;
    std::atomic<bool> footprintCancel_ { false };
    std::function<void (nova::WaveView::Marker, std::int64_t)> applyMarker_;   // pose un marqueur (glissé, menu clic droit)
    std::int64_t cursorFrame_ = -1;       // curseur de la forme d'onde (point de départ de l'écoute et des analyses)
    std::string cursorPath_, refLevelKey_;
    double refLevelDb_ = 0.0;
    bool refLevelKnown_ = false;
    neptune::core::sampleedit::LoopSearchOptions loopOptions_;
    struct PendingMeta { bool valid = false; std::string path; neptune::core::SampleMeta meta; std::string sig; juce::uint32 since = 0; } pendingMeta_;
    std::map<std::string, std::string> metaBaseline_;   // ce que le fichier porte (dernier état lu ou écrit), par chemin
    std::map<std::string, std::int64_t> fileMarkers_;   // marqueur de volume des métadonnées de chaque fichier (-1 : aucun)
    bool volumeDragged_ = false;
    int editLoop_ = 0;   // boucle éditée (0 = la première)
    bool snapOn_ = false;
    int stretchPercent_ = 100, pitchTenths_ = 0;      // durée (%) et hauteur (dixièmes de demi-ton) de l'outil d'étirement
    std::thread stretchThread_;
    std::atomic<bool> stretchCancel_ { false }, stretchBusy_ { false };
    int normIndex_ = 1, silenceIndex_ = 1, cutsIndex_ = 1;
    std::vector<neptune::core::sampleedit::LoopCandidate> loopCandidates_;
    int loopCandidateIndex_ = 0;
    std::string loopCandidatesKey_, seamKey_;
    std::unique_ptr<juce::FileChooser> saveChooser_;
    bool previewing_ = false;
    int previewZone_ = -1;                        // zone en cours d'écoute (bouton de la liste ou du bas)
    std::string previewPath_;                     // fichier de la réserve en cours d'écoute libre
    std::future<neptune::core::SampleLoadResult> previewLoad_;           // chargement d'un fichier de la réserve à écouter
    std::string previewLoadPath_;
    std::map<std::string, std::shared_ptr<const neptune::core::SampleData>> previewCache_;   // les derniers fichiers écoutés
    int previewOwner_ = 0;                       // 1 : bouton du bas, 2 : bouton des outils du sample
    int playSourceIndex_ = 3, loopPlayHeard_ = 1;   // outils du sample : quoi jouer (brut, région, curseur, boucle) ; boucle une fois (0) ou telle qu'entendue (1)
    std::string recordingPath_;
    juce::uint32 recordStartMs_ = 0;
    juce::uint32 previewStartMs_ = 0;
    std::unique_ptr<juce::MouseListener> groupMenuListener_;

    // colonne de gauche
    nova::Panel *outPanel_ = nullptr, *statePanel_ = nullptr, *navPanel_ = nullptr;
    std::vector<nova::KnobCard*> outKnobs_;
    nova::Text* statusText_ = nullptr;
    nova::Pill* voicesPill_ = nullptr;

    // page ZONES
    nova::Panel *zonePanel_ = nullptr, *tablePanel_ = nullptr, *matrixPanel_ = nullptr;
    nova::Pill *loadPill_ = nullptr, *importPill_ = nullptr, *removePill_ = nullptr, *listPill_ = nullptr, *monitorPill_ = nullptr, *modePill_ = nullptr, *chokeMorePill_ = nullptr, *levelsPill_ = nullptr;
    nova::Text* selectionText_ = nullptr;
    nova::NumberPill* rootNumber_ = nullptr;
    nova::ChoicePill *triggerChoice_ = nullptr, *chokeChoice_ = nullptr;
    std::vector<std::unique_ptr<nova::ChoicePill>> choices_;
    ZoneTable* zones_ = nullptr;
    PoolTable* pool_ = nullptr;
    nova::Text* detectedText_ = nullptr;
    nova::Pill *applyPitchPill_ = nullptr, *humanizePill_ = nullptr;
    // note détectée, calculée en arrière-plan (une file de fichiers, un seul fil)
    std::mutex pitchMutex_;
    std::map<std::string, neptune::core::PitchResult> pitchDone_;
    std::vector<std::string> pitchQueue_;
    bool pitchRunning_ = false, pitchQuit_ = false;
    std::thread pitchThread_;
    std::atomic<bool> pitchDirty_ { false };
    nova::Pill *zonesViewPill_ = nullptr, *poolViewPill_ = nullptr;
    // Séparateur entre la liste (zones / fichiers) et la matrice, largeur de la liste (unités de la maquette), et agrandissement d'un des deux conteneurs
    juce::Component* splitter_ = nullptr;
    nova::Pill *listExpandPill_ = nullptr, *matrixExpandPill_ = nullptr;
    float listWidth_ = 330.0f, dragStartWidth_ = 330.0f;
    bool listShown() const { return (listVisible_ || expanded_ == 1) && expanded_ != 2; }
    bool matrixShown() const { return expanded_ != 1; }
    int expanded_ = 0;                            // 0 : côte à côte, 1 : la liste prend toute la largeur, 2 : la matrice prend toute la largeur
    bool showPool_ = false;                       // la liste de gauche montre la réserve de fichiers au lieu des zones
    std::vector<std::string> poolPaths_;          // lignes de la réserve (même ordre que PoolTable)
    std::string poolSignature_;
    std::map<std::string, juce::String> poolContents_;   // ce que chaque fichier apporte (boucles, note racine, marqueur de volume), lu une fois
    MappingMatrix* matrix_ = nullptr;
    VoicesView* voicesView_ = nullptr;

    // page SAMPLE
    nova::Panel *wavePanel_ = nullptr, *playPanel_ = nullptr;
    nova::WaveView* wave_ = nullptr;
    nova::Text *infoText_ = nullptr, *markerText_ = nullptr;
    nova::Pill *loopPill_ = nullptr, *reversePill_ = nullptr, *showAllPill_ = nullptr, *playPill_ = nullptr, *recPill_ = nullptr;
    nova::ValueSlider *fadeIn_ = nullptr, *fadeOut_ = nullptr, *delay_ = nullptr, *crossfade_ = nullptr, *tune_ = nullptr, *gain_ = nullptr, *pan_ = nullptr;
    nova::NumberPill* rootNumber2_ = nullptr;
    nova::Panel* toolsPanel_ = nullptr;
    nova::Pill *trimPill_ = nullptr, *normPill_ = nullptr, *dcPill_ = nullptr, *silencePill_ = nullptr, *cutsPill_ = nullptr, *loopFindPill_ = nullptr, *loopOptionsPill_ = nullptr, *snapPill_ = nullptr, *saveAsPill_ = nullptr;
    nova::ChoicePill *normChoice_ = nullptr, *silenceChoice_ = nullptr, *cutsChoice_ = nullptr;
    nova::Text* toolStatus_ = nullptr;
    nova::Pill* toolsPlayPill_ = nullptr;
    nova::ChoicePill *playSourceChoice_ = nullptr, *loopPlayModeChoice_ = nullptr;
    std::unique_ptr<nova::ChoicePill> toolLoopChoice_;
    std::unique_ptr<nova::ChoicePill> loopEditChoice_;
    nova::Pill *loopAddPill_ = nullptr, *loopRemovePill_ = nullptr;
    nova::NumberPill* loopRepeatsNumber_ = nullptr;
    nova::Pill *peakCurvePill_ = nullptr, *rmsCurvePill_ = nullptr, *lufsCurvePill_ = nullptr, *loudestPill_ = nullptr, *prevPeakPill_ = nullptr, *nextPeakPill_ = nullptr, *rangePill_ = nullptr;
    nova::NumberPill *rangeLoNumber_ = nullptr, *rangeHiNumber_ = nullptr;
    nova::NumberPill *durationNumber_ = nullptr, *pitchNumber_ = nullptr;
    nova::Pill* stretchPill_ = nullptr;
    nova::LoopSeamView* seam_ = nullptr;

    // page SON
    nova::Panel *envPanel_ = nullptr, *filterPanel_ = nullptr, *slotsAreaPanel_ = nullptr;
    EnvelopeEditor* env_ = nullptr;
    int envLevel_ = 2;                                 // niveau édité : 0 instrument, 1 Groupe de la zone affichée, 2 zones sélectionnées
    std::unique_ptr<nova::ChoicePill> envLevelChoice_;
    nova::Pill* envLinkPill_ = nullptr;
    nova::Text* envNoteText_ = nullptr;
    std::vector<nova::KnobCard*> filterKnobs_;
    nova::Pill *filtersPill_ = nullptr, *slotsPill_ = nullptr, *modulatorsPill_ = nullptr;
    nova::Text* modulatorsText_ = nullptr;
    nova::Text* slotsText_ = nullptr;

    // page INSTRUMENT
    nova::Panel *velPanel_ = nullptr, *polyPanel_ = nullptr, *memPanel_ = nullptr;
    VelocityPanel* velocity_ = nullptr;
    PolyphonyPanel* poly_ = nullptr;
    juce::ToggleButton *rrToggle_ = nullptr, *releaseToggle_ = nullptr, *streamToggle_ = nullptr, *borrowToggle_ = nullptr;
    juce::ComboBox* preloadBox_ = nullptr;
    juce::Label* preloadLabel_ = nullptr;
    juce::Viewport* groupsViewport_ = nullptr;
    juce::Component groupsHolder_;
    juce::OwnedArray<juce::ToggleButton> groupToggles_;
};
