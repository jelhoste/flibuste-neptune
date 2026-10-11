# Toutes les dépendances externes sont déclarées ICI, avec une version figée.
# Même mécanisme sur Linux, Windows et macOS (FetchContent : aucun paquet système à installer,
# hormis les bibliothèques de développement graphiques/audio de JUCE sous Linux).
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Tests du cœur --------------------------------------------------------
if(NEPTUNE_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(Catch2)
endif()

# --- Plugin : JUCE + clap-juce-extensions ---------------------------------
# JUCE 8.0.11 et clap-juce-extensions 0.26.0 : combinaison testée par la CI de clap-juce-extensions
# (matrice Linux / Windows / macOS). À monter de version volontairement, pas par accident.
if(NEPTUNE_BUILD_PLUGIN)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.11
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        0.26.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "clap-libs/clap" "clap-libs/clap-helpers")
  FetchContent_MakeAvailable(JUCE clap_juce_extensions)
endif()

# --- Lecture/analyse audio (palier 1) : désactivé tant qu'il n'est pas utilisé ---
# libsndfile 1.2.2 (LGPL) et libebur128 v1.2.6 (MIT). Activer avec -DNEPTUNE_WITH_AUDIO_IO=ON.
option(NEPTUNE_WITH_AUDIO_IO "Activer libsndfile + libebur128" OFF)
if(NEPTUNE_WITH_AUDIO_IO)
  set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(libsndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(libebur128
    GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
    GIT_TAG        v1.2.6
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(libsndfile libebur128)
endif()
