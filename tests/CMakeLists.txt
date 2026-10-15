add_executable(neptune_core_tests test_engine.cpp test_parameter.cpp test_autosave.cpp test_envelope.cpp test_envelope_layout.cpp test_knob_interaction.cpp test_probe_pool.cpp test_profile.cpp test_sampler.cpp test_sample_loader.cpp test_zone_import.cpp test_settings.cpp test_state.cpp test_theme.cpp test_ui_state.cpp test_i18n.cpp test_undo.cpp test_memory_budget.cpp test_streaming.cpp test_mapping_edit.cpp test_velocity.cpp test_polyphony.cpp test_choke.cpp test_filter.cpp test_mixer.cpp test_synth.cpp test_utility.cpp test_parallel.cpp test_preset.cpp test_analogsynth.cpp test_midi_filter.cpp test_loudness.cpp test_fm.cpp test_organ.cpp)
target_link_libraries(neptune_core_tests PRIVATE neptune_core SndFile::sndfile Catch2::Catch2WithMain) # SndFile : les tests écrivent des WAV
include(CTest)
include(Catch)
catch_discover_tests(neptune_core_tests)

# Mesure de performance du callback (hors ctest : dépend de la machine, voir docs/performance.md).
add_executable(neptune_perf_callback perf_callback.cpp)
target_link_libraries(neptune_perf_callback PRIVATE neptune_core)
