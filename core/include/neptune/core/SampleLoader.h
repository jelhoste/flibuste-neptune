#pragma once
// Lecture d'un fichier audio en SampleData (palier 1). Hors temps réel : alloue, lit le disque.
// Formats : WAV et AIFF (libsndfile), FLAC (dr_flac). Mono ou stéréo ; plus de canaux : refusé avec un message.
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

SampleLoadResult loadSampleFile (const std::filesystem::path& path);
} // namespace neptune::core
