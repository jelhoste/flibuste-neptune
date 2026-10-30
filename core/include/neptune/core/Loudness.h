#pragma once
// Sonie (LUFS) du Master (spec-modules §3, EBU R128 / ITU-R BS.1770) : momentanée (400 ms), à court terme (3 s) et intégrée (avec
// portes), plus la crête vraie. Mesure faite par libebur128 sur un thread d'analyse : le thread audio ne fait que copier le signal
// dans un tampon circulaire sans verrou (jamais d'allocation ni d'appel à la bibliothèque côté audio).
// Sans libebur128 (option CMake désactivée), les valeurs restent à -infini.
#include <atomic>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace neptune::core
{
class LoudnessMeter
{
public:
    struct Reading
    {
        float momentary = -200.0f;   // LUFS, -200 = « -inf »
        float shortTerm = -200.0f;
        float integrated = -200.0f;
        float truePeakDb = -200.0f;  // dBTP (maximum depuis la dernière remise à zéro)
        float range = 0.0f;          // plage de sonie (LRA), en LU ; 0 tant qu'il manque de la matière
    };

    LoudnessMeter() = default;
    ~LoudnessMeter();
    LoudnessMeter (const LoudnessMeter&) = delete;
    LoudnessMeter& operator= (const LoudnessMeter&) = delete;

    static constexpr bool available() noexcept
    {
       #ifdef NEPTUNE_HAS_EBUR128
        return true;
       #else
        return false;
       #endif
    }

    // Thread de message : (re)démarre l'analyse pour cette fréquence d'échantillonnage.
    void prepare (double sampleRate);
    void stop();
    // Thread audio : copie `n` échantillons stéréo (perdus si le thread d'analyse a du retard : c'est un compteur).
    void push (const float* left, const float* right, int n) noexcept;
    // Remet à zéro la mesure intégrée (et la crête vraie).
    void reset() noexcept { resetRequested_.store (true, std::memory_order_release); }
    Reading read() const noexcept;

private:
    void run();

    std::vector<float> ring_;                   // L et R entrelacés : 2 flottants par trame
    std::size_t frames_ = 0;
    std::atomic<std::size_t> head_ { 0 }, tail_ { 0 };
    std::atomic<bool> resetRequested_ { false }, quit_ { false };
    std::atomic<float> momentary_ { -200.0f }, shortTerm_ { -200.0f }, integrated_ { -200.0f }, truePeak_ { -200.0f }, range_ { 0.0f };
    double sampleRate_ = 48000.0;
    std::thread thread_;
};
} // namespace neptune::core
