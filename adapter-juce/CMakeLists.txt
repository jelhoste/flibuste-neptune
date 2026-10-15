# Nombre de pistes du pool de paramètres « sonde » (palier 0, point 4). 32 pistes = 676 paramètres.
set(NEPTUNE_PROBE_TRACKS 16 CACHE STRING "Pistes du mixeur (paramètres exposés à l'hôte)")

# Sorties stéréo déclarées à l'hôte (décision 2026-10-15 : 32 stéréo = 64 canaux). Le wrapper plafonne à 128 canaux de sortie.
set(NEPTUNE_PROBE_OUTPUTS 32 CACHE STRING "Sorties stéréo de la sonde")

# Identité provisoire : domaine inversé du CLAP_ID à confirmer.
set(NEPTUNE_CLAP_ID "com.tournedosrossini.neptune" CACHE STRING "Identifiant CLAP")

juce_add_plugin(Neptune
  COMPANY_NAME                "Le Tournedos Rossini"
  PRODUCT_NAME                "Neptune"
  PLUGIN_MANUFACTURER_CODE    Trss
  PLUGIN_CODE                 Nept
  IS_SYNTH                    TRUE
  NEEDS_MIDI_INPUT            TRUE
  FORMATS                     Standalone)   # Standalone = banc d'essai local ; le .clap vient de clap-juce-extensions

clap_juce_extensions_plugin(
  TARGET        Neptune
  CLAP_ID       "${NEPTUNE_CLAP_ID}"
  CLAP_FEATURES instrument sampler stereo)

target_sources(Neptune PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/EnvelopeEditor.cpp
  src/ImportWizard.cpp
  src/ZoneTable.cpp
  src/MappingMatrix.cpp
  src/VelocityPanel.cpp
  src/PolyphonyPanel.cpp
  src/ChokePanel.cpp
  src/VoicesView.cpp
  src/FilterEditor.cpp
  src/MixerView.cpp
  src/SynthView.cpp
  src/PresetBar.cpp
  src/PresetManager.cpp
  src/MidiFilterEditor.cpp
  src/UtilityEditor.cpp
  src/InsertOverlay.cpp
  src/NeptuneKnob.cpp
  src/KnobModal.cpp)

target_compile_definitions(Neptune PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  NEPTUNE_PROBE_TRACKS=${NEPTUNE_PROBE_TRACKS}
  NEPTUNE_PROBE_OUTPUTS=${NEPTUNE_PROBE_OUTPUTS})

target_link_libraries(Neptune
  PRIVATE
    neptune_core
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    clap_juce_extensions
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- Moussaillon (nom provisoire) : l'Utilitaire de la suite, plugin CLAP indépendant ---------------------------------------
# Même DSP (neptune_core : StockModule / UtilityModule) et même interface (UtilityEditor, NeptuneKnob, KnobModal) que l'insert
# de Neptune ; seul l'adaptateur change (MoussaillonPlugin : l'hôte porte le plugin).
set(MOUSSAILLON_CLAP_ID "com.tournedosrossini.moussaillon" CACHE STRING "Identifiant CLAP de l'Utilitaire")

juce_add_plugin(Moussaillon
  COMPANY_NAME                "Le Tournedos Rossini"
  PRODUCT_NAME                "Moussaillon"
  PLUGIN_MANUFACTURER_CODE    Trss
  PLUGIN_CODE                 Mous
  IS_SYNTH                    FALSE
  NEEDS_MIDI_INPUT            FALSE
  FORMATS                     Standalone)

clap_juce_extensions_plugin(
  TARGET        Moussaillon
  CLAP_ID       "${MOUSSAILLON_CLAP_ID}"
  CLAP_FEATURES audio-effect utility stereo)

target_sources(Moussaillon PRIVATE
  src/MoussaillonPlugin.cpp
  src/UtilityEditor.cpp
  src/NeptuneKnob.cpp
  src/KnobModal.cpp)

target_compile_definitions(Moussaillon PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Moussaillon
  PRIVATE
    neptune_core
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    clap_juce_extensions
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- AnalogSynth (nom provisoire) : le synthé soustractif de Neptune, plugin CLAP instrument indépendant --------------------
# Même moteur (neptune_core : AnalogSynth.h, Synth.h, SynthVoices.h) et même vue (SynthView) que la piste Synthé de Neptune ;
# en plus, le système de presets (Preset.h, PresetLibrary.h : banques MIDI, grille concert, corbeille) et son gestionnaire.
set(ANALOGSYNTH_CLAP_ID "com.tournedosrossini.analogsynth" CACHE STRING "Identifiant CLAP du synthé indépendant")

juce_add_plugin(AnalogSynth
  COMPANY_NAME                "Le Tournedos Rossini"
  PRODUCT_NAME                "AnalogSynth"
  PLUGIN_MANUFACTURER_CODE    Trss
  PLUGIN_CODE                 Anls
  IS_SYNTH                    TRUE
  NEEDS_MIDI_INPUT            TRUE
  FORMATS                     Standalone)

clap_juce_extensions_plugin(
  TARGET        AnalogSynth
  CLAP_ID       "${ANALOGSYNTH_CLAP_ID}"
  CLAP_FEATURES instrument synthesizer stereo)

target_sources(AnalogSynth PRIVATE
  src/AnalogSynthPlugin.cpp
  src/PresetManager.cpp
  src/PresetBar.cpp
  src/SynthView.cpp
  src/EnvelopeEditor.cpp
  src/NeptuneKnob.cpp
  src/KnobModal.cpp)

target_compile_definitions(AnalogSynth PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AnalogSynth
  PRIVATE
    neptune_core
    juce::juce_audio_utils
    juce::juce_audio_plugin_client
    clap_juce_extensions
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
