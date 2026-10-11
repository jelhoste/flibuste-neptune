add_library(neptune_core STATIC src/Engine.cpp)
target_include_directories(neptune_core PUBLIC include)
target_compile_features(neptune_core PUBLIC cxx_std_17)
# Règle : aucune dépendance JUCE ici. Le cœur reste portable (tests, web app/WASM plus tard).
