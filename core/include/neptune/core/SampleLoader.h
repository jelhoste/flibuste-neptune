#pragma once
// Lecture d'un fichier audio en SampleData (palier 1). Hors temps réel : alloue, lit le disque.
// Formats : WAV et AIFF (libsndfile), FLAC (dr_flac). Mono ou stéréo ; plus de canaux : refusé avec un message.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "neptune/core/SampleData.h"

namespace neptune::core
{
struct SampleLoadResult
{
    std::shared_ptr<const SampleData> sample; // nul en cas d'échec
    std::string error;                        // en anglais, destiné au journal/UI
    bool ok() const noexcept { return sample != nullptr; }
};

// Ce que le fichier occupera en mémoire une fois décodé, lu à partir de l'en-tête seulement (aucun décodage).
// Mêmes règles que loadSampleFile : 16 bits gardé en int16 (2 octets), tout le reste en float32 (4 octets).
struct AudioFileInfo
{
    bool ok = false;
    std::string error;           // en anglais ; vide si ok
    int channels = 0;
    std::int64_t frames = 0;
    double sampleRate = 0.0;
    int bitDepth = 0;            // 0 = inconnu
    bool keptAsInt16 = false;
    std::uint64_t ramBytes = 0;  // frames * canaux * (2 ou 4) ; 0 si !ok
};

AudioFileInfo probeAudioFile (const std::filesystem::path& path);

SampleLoadResult loadSampleFile (const std::filesystem::path& path);
} // namespace neptune::core
