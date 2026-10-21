#pragma once
// Enregistreur audio direct (spec §21) : le thread audio dépose l'entrée de l'hôte dans un anneau sans verrou, un thread d'écriture la
// vide dans un fichier WAV (32 bits flottant, stéréo) au fil de l'eau — aucune allocation ni appel disque côté audio, donc aucun impact sur la latence.
// L'enregistrement est une capture parallèle : le chemin de lecture n'en sait rien.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace neptune::core
{
class AudioRecorder
{
public:
    AudioRecorder() = default;
    ~AudioRecorder() { stop(); }
    AudioRecorder (const AudioRecorder&) = delete;
    AudioRecorder& operator= (const AudioRecorder&) = delete;

    // Thread de message : ouvre le fichier (crée les dossiers) et démarre l'écriture. Faux si déjà en cours ou si le fichier ne s'ouvre pas.
    bool start (const std::filesystem::path&, double sampleRate, std::string* error = nullptr);
    // Thread de message : arrête, vide l'anneau, ferme le fichier. Rend le nombre d'échantillons écrits (0 si rien n'était en cours).
    std::int64_t stop();
    bool recording() const noexcept { return recording_.load (std::memory_order_acquire); }
    const std::filesystem::path& path() const noexcept { return path_; }

    // Thread audio : dépose `n` échantillons (right peut être nul : mono dupliqué). Perdus si l'anneau est plein (compté dans `dropped()`).
    void push (const float* left, const float* right, int n) noexcept;

    std::int64_t framesWritten() const noexcept { return written_.load (std::memory_order_relaxed); }
    std::int64_t dropped() const noexcept { return dropped_.load (std::memory_order_relaxed); }
    float peak() noexcept { return peak_.exchange (0.0f, std::memory_order_relaxed); }   // crête depuis le dernier appel (0..1+) : vu-mètre d'entrée
    double sampleRate() const noexcept { return sampleRate_; }

private:
    void run();

    static constexpr std::size_t kRingFrames = 1u << 19;   // ~ 11 s à 48 kHz
    std::vector<float> ring_;                               // L et R entrelacés
    std::atomic<std::uint64_t> head_ { 0 }, tail_ { 0 };
    std::atomic<bool> recording_ { false }, quit_ { false };
    std::atomic<std::int64_t> written_ { 0 }, dropped_ { 0 };
    std::atomic<float> peak_ { 0.0f };
    std::thread thread_;
    std::filesystem::path path_;
    double sampleRate_ = 48000.0;
    void* file_ = nullptr;   // SNDFILE*
};
} // namespace neptune::core
