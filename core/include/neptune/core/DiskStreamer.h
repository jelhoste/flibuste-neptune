#pragma once
// Streaming disque (palier 1, jalon 4) : un thread dédié lit les fichiers et remplit un tampon circulaire par voix ;
// le thread audio ne fait que consommer. Aucune allocation, aucun verrou, aucune lecture disque côté audio.
//
// Principe : un échantillon streamé garde en mémoire son début (préchargement, SampleData::resident()) ; la voix le lit
// d'abord depuis la mémoire, puis, une fois cette partie épuisée, depuis son tampon (frames >= resident()), que le thread
// disque a rempli entre-temps. Un « emplacement » (Slot) par voix.
//
// Protocole d'un emplacement (un seul producteur audio, un seul consommateur disque) :
//   claim()    : l'audio écrit chemin/canaux/début/total puis publie `request` (valeur impaire = flux voulu).
//   disque     : voit une nouvelle valeur impaire, ouvre le fichier, se place au début, met `writePos` à ce début puis
//                publie `ready` = la valeur de `request`. Remplit ensuite tant qu'il y a de la place.
//   release()  : l'audio publie une valeur paire ; le disque ferme le fichier.
// Le disque n'écrase jamais une frame >= readPos (publié par l'audio : la plus ancienne frame encore utile).
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "neptune/core/SampleData.h"

namespace neptune::core
{
class DiskStreamer
{
public:
    static constexpr int kSlots = 320;              // = voix tenues + emplacements de fondu (SamplerVoices) ; ~42 Mo de tampons
    static constexpr int kRingFrames = 16384;       // puissance de 2 ; par canal ; ~0,37 s à 44,1 kHz
    static constexpr int kChunkFrames = 4096;       // frames lues d'un coup depuis le disque

    DiskStreamer() = default;
    ~DiskStreamer();
    DiskStreamer (const DiskStreamer&) = delete;
    DiskStreamer& operator= (const DiskStreamer&) = delete;

    // Thread de message/chargement : alloue les tampons (une fois) et démarre le thread disque. Idempotent.
    void start();
    bool running() const noexcept { return running_.load (std::memory_order_acquire); }

    // --- thread audio ---------------------------------------------------------------------------------------------
    // Demande le flux du fichier de `sample` à partir de sample.resident(). Rend false si impossible (pas démarré, chemin
    // trop long) : la voix jouera alors ce qui est en mémoire puis s'arrêtera.
    bool claim (int slot, const SampleData& sample) noexcept;
    void release (int slot) noexcept;
    // Publie la plus ancienne frame encore nécessaire à la voix (autorise le disque à écraser avant).
    void setReadPos (int slot, std::int64_t frame) noexcept { slots_[static_cast<std::size_t> (slot)].readPos.store (frame, std::memory_order_release); }

    struct View
    {
        const float* left = nullptr;
        const float* right = nullptr;
        std::int64_t writePos = 0; // frames < writePos sont disponibles
        bool ready = false;
    };
    // Lecture de l'état d'un flux demandé avec la valeur `gen` rendue par claim().
    View view (int slot, std::uint32_t gen) const noexcept;
    std::uint32_t generation (int slot) const noexcept { return slots_[static_cast<std::size_t> (slot)].request.load (std::memory_order_relaxed); }

    // Incidents de sous-alimentation (la voix s'est éteinte faute de données) depuis le démarrage. Incrémenté par l'audio.
    void countUnderrun() noexcept { underruns_.fetch_add (1, std::memory_order_relaxed); }
    std::uint32_t underruns() const noexcept { return underruns_.load (std::memory_order_relaxed); }

private:
    // Paramètres d'un flux, en deux exemplaires alternés (indice (request >> 1) & 1) : une nouvelle demande n'écrase
    // jamais ce que le thread disque est en train de lire pour le flux précédent.
    struct Params
    {
        char path[maxStreamPathBytes] = {};
        int channels = 0;
        std::int64_t startFrame = 0, totalFrames = 0;
    };

    struct Slot
    {
        Params params[2];
        std::atomic<std::uint32_t> request { 0 }; // impair : flux voulu ; pair : libre
        std::atomic<std::uint32_t> ready { 0 };
        std::atomic<std::int64_t> writePos { 0 };
        std::atomic<std::int64_t> readPos { 0 };
        float* ring[2] = { nullptr, nullptr };
    };

    void threadMain();

    // Sur le tas : les emplacements pèsent ~700 Ko (chemins) ; une instance locale (tests) ne doit pas déborder la pile (1 Mo sous Windows).
    std::unique_ptr<Slot[]> slotStorage_ { new Slot[kSlots] };
    Slot* const slots_ = slotStorage_.get();
    std::vector<float> storage_;
    std::thread thread_;
    std::atomic<bool> running_ { false };
    std::atomic<bool> stop_ { false };
    std::atomic<std::uint32_t> underruns_ { 0 };
};
} // namespace neptune::core
