# Toutes les dépendances externes sont déclarées ICI, avec une version figée.
# Même mécanisme sur Linux, Windows et macOS (FetchContent : aucun paquet système à installer,
# hormis les bibliothèques de développement graphiques/audio de JUCE sous Linux).
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- Format d'état JSON (cœur, palier 0 point 3) --------------------------
# nlohmann/json v3.12.0 (MIT, header-only) : dernière version publiée vérifiée le 2026-10-11.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# --- Tests du cœur --------------------------------------------------------
if(NEPTUNE_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(Catch2)
endif()

# --- Plugin : JUCE + clap-juce-extensions ---------------------------------
# JUCE 8.0.11 + clap-juce-extensions au commit 7adee3a (branche main, 2026-10-06) : combinaison dont la CI amont est verte
# sur Linux, Windows et macOS. clap-juce-extensions n'a pas de vraies versions : ses tags sont anciens (le tag 0.26.0 date de 2022),
# on fige donc un commit. À monter de version volontairement, pas par accident.
if(NEPTUNE_BUILD_PLUGIN)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.11
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES "clap-libs/clap" "clap-libs/clap-helpers")
  FetchContent_MakeAvailable(JUCE clap_juce_extensions)
endif()

# --- Lecture de fichiers audio (palier 1) ----------------------------------
# libsndfile 1.2.2 (LGPL, compatible AGPLv3) : WAV et AIFF, intégrés. Le FLAC n'y est PAS lu (il exige libFLAC, absente de
# cette chaîne de build) : il passe par dr_flac (domaine public / MIT-0), un seul fichier d'en-tête.
# On fige la configuration de libsndfile pour que le résultat ne dépende pas des bibliothèques installées sur la machine :
# ni codecs externes (FLAC/Vorbis/Opus), ni MPEG.
option(NEPTUNE_WITH_LIBSNDFILE "Lire les samples (libsndfile + dr_flac)" ON)
if(NEPTUNE_WITH_LIBSNDFILE)
  set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
  set(ENABLE_MPEG OFF CACHE BOOL "" FORCE)
  set(ENABLE_CPACK OFF CACHE BOOL "" FORCE)
  set(ENABLE_PACKAGE_CONFIG OFF CACHE BOOL "" FORCE)
  set(INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON) # liée dans le .clap
  # libsndfile 1.2.2 déclare une version minimale de CMake trop ancienne pour CMake 4 : on l'autorise explicitement.
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_Declare(libsndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(libsndfile)

  # dr_libs, étiquette flac-0.13.3 : on n'utilise que dr_flac.h (inclus par SampleLoader.cpp).
  FetchContent_Declare(dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG        flac-0.13.3
    GIT_SHALLOW    TRUE)
  FetchContent_GetProperties(dr_libs)
  if(NOT dr_libs_POPULATED)
    FetchContent_Populate(dr_libs)
  endif()
endif()

# --- Opus : libopus v1.6.1 (BSD 3 clauses, compatible AGPLv3), compilée avec ses sources C (aucun paquet système) ---
# Lecture seule des fichiers .opus (Ogg Opus), décodés en mémoire à 48 kHz. Désactiver avec -DNEPTUNE_WITH_OPUS=OFF.
option(NEPTUNE_WITH_OPUS "Lire les fichiers .opus (libopus)" ON)
if(NEPTUNE_WITH_OPUS)
  set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
  set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_Declare(opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.6.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(opus)
endif()

# --- WavPack : libwavpack 5.8.1 (BSD 3 clauses, compatible AGPLv3), compilée avec ses sources C ---
# Lecture seule des fichiers .wv (sans perte ; les échantillons d'orgue GrandOrgue sont des WavPack, souvent nommés .wav), décodés en mémoire.
# Désactiver avec -DNEPTUNE_WITH_WAVPACK=OFF.
option(NEPTUNE_WITH_WAVPACK "Lire les fichiers WavPack (libwavpack)" ON)
if(NEPTUNE_WITH_WAVPACK)
  set(WAVPACK_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
  set(WAVPACK_INSTALL_CMAKE_MODULE OFF CACHE BOOL "" FORCE)
  set(WAVPACK_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(WAVPACK_INSTALL_PKGCONFIG_MODULE OFF CACHE BOOL "" FORCE)
  set(WAVPACK_ENABLE_LEGACY OFF CACHE BOOL "" FORCE)
  set(WAVPACK_ENABLE_THREADS OFF CACHE BOOL "" FORCE)
  set(WAVPACK_ENABLE_DSD OFF CACHE BOOL "" FORCE)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  FetchContent_Declare(wavpack
    GIT_REPOSITORY https://github.com/dbry/WavPack.git
    GIT_TAG        5.8.1
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(wavpack)
endif()

# --- Analyse de niveau : LUFS du Master (EBU R128 / ITU BS.1770) ---
# libebur128 v1.2.6 (MIT). Désactiver avec -DNEPTUNE_WITH_EBUR128=OFF (le compteur affiche alors « -inf »).
option(NEPTUNE_WITH_EBUR128 "Activer libebur128 (LUFS du Master)" ON)
if(NEPTUNE_WITH_EBUR128)
  set(WITH_STATIC_PIC ON CACHE BOOL "libebur128 : code indépendant de la position (lié dans le .clap)" FORCE)
  FetchContent_Declare(libebur128
    GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
    GIT_TAG        v1.2.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  ebur128) # seulement la bibliothèque (pas les tests ni les exemples du dépôt)
  FetchContent_MakeAvailable(libebur128)
  target_include_directories(ebur128 PUBLIC ${libebur128_SOURCE_DIR}/ebur128)
endif()
