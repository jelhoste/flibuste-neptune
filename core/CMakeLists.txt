add_library(neptune_core STATIC src/Autosave.cpp src/Engine.cpp src/FileUtil.cpp src/Parameter.cpp src/Profile.cpp src/ProbePool.cpp src/Envelope.cpp src/EnvelopeLayout.cpp src/KnobInteraction.cpp src/Instrument.cpp src/InstrumentSlot.cpp src/SampleLoader.cpp src/ZoneImport.cpp src/SamplerParams.cpp src/SamplerVoices.cpp src/Settings.cpp src/State.cpp src/Theme.cpp src/UiState.cpp src/I18n.cpp src/Undo.cpp src/MemoryBudget.cpp src/DiskStreamer.cpp)
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
