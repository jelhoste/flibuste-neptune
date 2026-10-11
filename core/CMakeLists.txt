add_library(neptune_core STATIC src/Autosave.cpp src/Engine.cpp src/FileUtil.cpp src/Parameter.cpp src/ProbePool.cpp src/Settings.cpp src/State.cpp src/Undo.cpp)
target_include_directories(neptune_core PUBLIC include)
target_compile_features(neptune_core PUBLIC cxx_std_17)
# Règle : aucune dépendance JUCE ici. Le cœur reste portable (tests, web app/WASM plus tard).
# nlohmann/json reste un détail du cœur (State.cpp) ; PUBLIC seulement parce que StateMigration.h l'expose aux tests de migration.
target_link_libraries(neptune_core PUBLIC nlohmann_json::nlohmann_json)
# Le cœur est lié dans une bibliothèque partagée (.clap) : code indépendant de la position obligatoire sous Linux.
set_target_properties(neptune_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
