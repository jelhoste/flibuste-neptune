add_executable(neptune_core_tests test_engine.cpp test_parameter.cpp test_autosave.cpp test_probe_pool.cpp test_settings.cpp test_state.cpp test_undo.cpp)
target_link_libraries(neptune_core_tests PRIVATE neptune_core Catch2::Catch2WithMain)
include(CTest)
include(Catch)
catch_discover_tests(neptune_core_tests)

# Mesure de performance du callback (hors ctest : dépend de la machine, voir docs/performance.md).
add_executable(neptune_perf_callback perf_callback.cpp)
target_link_libraries(neptune_perf_callback PRIVATE neptune_core)
