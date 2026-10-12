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

// `maxResidentFrames` > 0 : ne garde en mémoire que les premières frames (streaming disque) ; `frames` reste la longueur
// du fichier et `residentFrames` ce qui est lu. -1 (défaut) : tout le fichier. Un chemin trop long pour DiskStreamer
// (maxStreamPathBytes) est toujours chargé en entier.
SampleLoadResult loadSampleFile (const std::filesystem::path& path, std::int64_t maxResidentFrames = -1);

// Lecture séquentielle d'un fichier en float32 normalisé (mêmes valeurs que le chargement : 16 bits / 32768). Hors temps
// réel ; un lecteur par flux. Non copiable.
class AudioStreamReader
{
public:
    AudioStreamReader();
    ~AudioStreamReader();
    AudioStreamReader (const AudioStreamReader&) = delete;
    AudioStreamReader& operator= (const AudioStreamReader&) = delete;

    bool open (const std::filesystem::path& path, std::string* error = nullptr);
    void close();
    bool isOpen() const noexcept;
    int channels() const noexcept { return channels_; }
    std::int64_t totalFrames() const noexcept { return totalFrames_; }

    bool seek (std::int64_t frame);
    // Lit jusqu'à `frames` frames entrelacées dans `interleaved` (frames * canaux floats) ; rend le nombre lu (0 = fin/erreur).
    std::int64_t read (float* interleaved, std::int64_t frames);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    int channels_ = 0;
    std::int64_t totalFrames_ = 0;
};
} // namespace neptune::core
