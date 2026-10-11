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
  src/PluginEditor.cpp)

target_compile_definitions(Neptune PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

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
