add_library(neptune_core STATIC src/Autosave.cpp src/Engine.cpp src/FileUtil.cpp src/Parameter.cpp src/Profile.cpp src/ProbePool.cpp src/Envelope.cpp src/EnvelopeLayout.cpp src/KnobInteraction.cpp src/Instrument.cpp src/SampleLoader.cpp src/ZoneImport.cpp src/SamplerParams.cpp src/SamplerVoices.cpp src/Settings.cpp src/State.cpp src/Theme.cpp src/UiState.cpp src/I18n.cpp src/Undo.cpp src/MemoryBudget.cpp src/DiskStreamer.cpp src/MappingEdit.cpp src/InstrumentEdit.cpp src/VelocityResponse.cpp src/Filter.cpp src/Mixer.cpp src/MixerState.cpp src/Synth.cpp src/WaveCatalog.cpp src/SynthVoices.cpp src/StockModule.cpp src/UtilityModule.cpp src/Preset.cpp src/PresetLibrary.cpp src/AnalogSynth.cpp src/MidiFilter.cpp src/Loudness.cpp src/FmPatch.cpp src/FmMsfa.cpp src/FmVoices.cpp src/FmPreset.cpp src/FmSynth.cpp src/OrganCore.c src/OrganEngine.cpp src/OrganSynth.cpp src/OrganPreset.cpp src/ZoneSlots.cpp src/Control.cpp src/Osc.cpp src/Oversampler.cpp src/ShaperModule.cpp src/ShaperPresets.cpp src/CompressorModule.cpp src/CompressorPresets.cpp src/EqualizerModule.cpp)
target_include_directories(neptune_core PUBLIC include)
target_compile_features(neptune_core PUBLIC cxx_std_17)
# Règle : aucune dépendance JUCE ici. Le cœur reste portable (tests, web app/WASM plus tard).
# nlohmann/json reste un détail du cœur (State.cpp) ; PUBLIC seulement parce que StateMigration.h l'expose aux tests de migration.
target_link_libraries(neptune_core PUBLIC nlohmann_json::nlohmann_json)
# Le cœur est lié dans une bibliothèque partagée (.clap) : code indépendant de la position obligatoire sous Linux.
set_target_properties(neptune_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Lecture des samples (palier 1) : libsndfile pour WAV/AIFF, dr_flac (en-tête seul) pour le FLAC.
target_link_libraries(neptune_core PRIVATE SndFile::sndfile)
target_include_directories(neptune_core PRIVATE ${dr_libs_SOURCE_DIR})

# Thread disque du streaming (std::thread) : pthread explicite pour les glibc anciennes.
find_package(Threads REQUIRED)
target_link_libraries(neptune_core PUBLIC Threads::Threads)

# LUFS du Master (libebur128) : lié seulement si l'option est active ; sinon le compteur rend « -inf ».
if(NEPTUNE_WITH_EBUR128)
  target_link_libraries(neptune_core PRIVATE ebur128)
  target_compile_definitions(neptune_core PUBLIC NEPTUNE_HAS_EBUR128=1)
endif()

# Orgue à roues phoniques : moteur de setBfree (C, GPL v2 ou ultérieure, third_party/setbfree) et sa liaison (OrganCore.c).
set(SETBFREE_DIR ${CMAKE_SOURCE_DIR}/third_party/setbfree)
target_sources(neptune_core PRIVATE
  ${SETBFREE_DIR}/src/tonegen.c ${SETBFREE_DIR}/src/vibrato.c
  ${SETBFREE_DIR}/b_whirl/whirl.c ${SETBFREE_DIR}/b_whirl/eqcomp.c
  ${SETBFREE_DIR}/b_overdrive/overdrive.c ${SETBFREE_DIR}/b_overdrive/filterTools.c
  ${SETBFREE_DIR}/b_reverb/reverb.c)
set(SETBFREE_SOURCES src/OrganCore.c ${SETBFREE_DIR}/src/tonegen.c ${SETBFREE_DIR}/src/vibrato.c
  ${SETBFREE_DIR}/b_whirl/whirl.c ${SETBFREE_DIR}/b_whirl/eqcomp.c ${SETBFREE_DIR}/b_overdrive/overdrive.c
  ${SETBFREE_DIR}/b_overdrive/filterTools.c ${SETBFREE_DIR}/b_reverb/reverb.c)
set_source_files_properties(${SETBFREE_SOURCES} PROPERTIES COMPILE_DEFINITIONS "VERSION=\"neptune\";_USE_MATH_DEFINES")
target_include_directories(neptune_core PRIVATE
  ${SETBFREE_DIR}/src ${SETBFREE_DIR}/b_whirl ${SETBFREE_DIR}/b_overdrive ${SETBFREE_DIR}/b_reverb)
if(MSVC)
  target_include_directories(neptune_core PRIVATE ${SETBFREE_DIR}/compat) # <strings.h>
endif()
