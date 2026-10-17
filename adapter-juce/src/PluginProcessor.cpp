#include "PluginProcessor.h"
#include "NeptuneParameter.h"
#include "PluginEditor.h"
#include "neptune/core/FmPreset.h"
#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/OrganPreset.h"
#include "neptune/core/Mixer.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/State.h"
#include "neptune/core/Synth.h"
#include <set>

#ifndef NEPTUNE_PROBE_TRACKS
 #define NEPTUNE_PROBE_TRACKS 16
#endif

// Nombre de sorties stéréo déclarées à l'hôte (sonde du point 4 : 32 depuis 2026-10-15).
#ifndef NEPTUNE_PROBE_OUTPUTS
 #define NEPTUNE_PROBE_OUTPUTS 32
#endif

namespace
{
// Dossier de configuration utilisateur de Neptune : ~/.config/Neptune, %APPDATA%\\Neptune, ~/Library/Application Support/Neptune.
std::filesystem::path configDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return std::filesystem::u8path (base.getChildFile ("Neptune").getFullPathName().toStdString());
}
}

// Thread de chargement des samples : construit les instruments (lit le disque), les publie au moteur piste par piste, et
// libère les instruments rendus par le thread audio. Seul consommateur de la file de collecte (InstrumentSlot::collect).
// Le cache de fichiers est PARTAGÉ entre les pistes : deux pistes qui jouent les mêmes fichiers n'en gardent qu'une copie.
class NeptuneProcessor::InstrumentLoader : public juce::Thread
{
public:
    explicit InstrumentLoader (NeptuneProcessor& owner) : juce::Thread ("Neptune sample loader"), owner_ (owner) { startThread(); }
    ~InstrumentLoader() override { stopThread (15000); }

    void request (int track, neptune::core::InstrumentDescription d, bool force)
    {
        {
            const juce::ScopedLock lock (lock_);
            auto& r = pending_[static_cast<std::size_t> (track)];
            r.description = std::move (d);
            r.has = true;
            r.force = r.force || force;
        }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (500);
            owner_.engine_.collectGarbage();

            int track = -1;
            neptune::core::InstrumentDescription d;
            bool force = false;
            {
                const juce::ScopedLock lock (lock_);
                for (std::size_t t = 0; t < pending_.size(); ++t)
                    if (pending_[t].has)
                    {
                        track = static_cast<int> (t);
                        d = std::move (pending_[t].description);
                        force = pending_[t].force;
                        pending_[t].has = false;
                        pending_[t].force = false;
                        break;
                    }
            }
            if (track < 0) continue;
            buildAndPublish (track, std::move (d), force);
            if (hasAnyPending()) notify(); // d'autres pistes attendent : pas d'attente de 500 ms
        }
    }

private:
    struct Request
    {
        neptune::core::InstrumentDescription description;
        bool has = false, force = false;
    };

    void buildAndPublish (int track, neptune::core::InstrumentDescription d, bool force)
    {
        if (force) cache_.clear();
        InstrumentStatus st;
        if (d.empty())
        {
            owner_.engine_.publishInstrument (track, std::make_shared<neptune::core::Instrument>());
            owner_.setStatus (track, st); // None
            return;
        }

        st.kind = InstrumentStatus::Kind::Loading;
        st.total = static_cast<int> (d.zones.size());
        owner_.setStatus (track, st);
        std::vector<std::string> warnings;
        // Fichiers des AUTRES pistes : à conserver dans le cache partagé (buildInstrument purge ce qui n'est pas dans sa description)
        std::set<std::string> keep;
        {
            const auto others = owner_.otherTracksPaths (track);
            keep.insert (others.begin(), others.end());
        }
        // Abandonne dès qu'une nouvelle demande arrive pour CETTE piste (le cache garde ce qui est déjà lu) ou à l'arrêt du plugin.
        auto inst = neptune::core::buildInstrument (d, &warnings, &cache_, [&] (int done, int total)
        {
            if (threadShouldExit() || hasPendingFor (track)) return false;
            if (done % 16 == 0)
            {
                InstrumentStatus p;
                p.kind = InstrumentStatus::Kind::Loading;
                p.done = done;
                p.total = total;
                owner_.setStatus (track, std::move (p));
            }
            return true;
        }, &keep);
        if (threadShouldExit() || inst == nullptr) return; // annulé : la demande suivante est traitée au tour d'après

        owner_.engine_.publishInstrument (track, inst);
        st = {};
        if (! inst->zones.empty())
        {
            const auto& sd = *inst->zones.front().sample;
            st.kind = InstrumentStatus::Kind::Loaded;
            st.name = sd.name;
            st.channels = sd.channels;
            st.bitDepth = sd.sourceBitDepth;
            st.sampleRate = sd.sampleRate;
            st.seconds = sd.durationSeconds();
            st.zones = static_cast<int> (inst->zones.size());
            std::set<const neptune::core::SampleData*> distinct;
            double bytes = 0.0;
            for (const auto& z : inst->zones)
                if (distinct.insert (z.sample.get()).second) bytes += static_cast<double> (z.sample->bytes());
            st.files = static_cast<int> (distinct.size());
            st.megabytes = bytes / (1024.0 * 1024.0);
        }
        else
        {
            st.kind = InstrumentStatus::Kind::Error;
            st.error = warnings.empty() ? "no usable zone" : warnings.front();
        }
        owner_.setStatus (track, st);
    }

    bool hasPendingFor (int track)
    {
        const juce::ScopedLock lock (lock_);
        return pending_[static_cast<std::size_t> (track)].has;
    }

    bool hasAnyPending()
    {
        const juce::ScopedLock lock (lock_);
        for (const auto& r : pending_)
            if (r.has) return true;
        return false;
    }

    NeptuneProcessor& owner_;
    juce::CriticalSection lock_;
    std::array<Request, neptune::core::mixer::numTracks> pending_;
    neptune::core::SampleCache cache_; // thread du chargeur uniquement, partagé par toutes les pistes
};

neptune::core::InstrumentDescription NeptuneProcessor::instrumentDescription (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))].instrument;
}

// Chemins des fichiers que les AUTRES pistes ont besoin de garder en mémoire (cache partagé du chargeur).
std::set<std::string> NeptuneProcessor::otherTracksPaths (int track) const
{
    std::set<std::string> paths;
    const juce::ScopedLock lock (instrumentLock_);
    for (std::size_t t = 0; t < mixer_.tracks.size(); ++t)
    {
        if (static_cast<int> (t) == track) continue;
        const auto& d = mixer_.tracks[t].instrument;
        const auto mask = neptune::core::loadedZones (d);
        for (std::size_t i = 0; i < d.zones.size() && i < mask.size(); ++i)
            if (mask[i]) paths.insert (d.zones[i].samplePath);
    }
    return paths;
}

NeptuneProcessor::InstrumentStatus NeptuneProcessor::instrumentStatus (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return status_[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))];
}

void NeptuneProcessor::setStatus (int track, InstrumentStatus s)
{
    const juce::ScopedLock lock (instrumentLock_);
    status_[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))] = std::move (s);
}

void NeptuneProcessor::setInstrumentDescription (int track, neptune::core::InstrumentDescription d, bool forceReload)
{
    track = juce::jlimit (0, neptune::core::mixer::numTracks - 1, track);
    for (auto& z : d.zones) z = neptune::core::sanitised (z);
    {
        const juce::ScopedLock lock (instrumentLock_);
        auto& cur = mixer_.tracks[static_cast<std::size_t> (track)].instrument;
        if (d == cur && ! forceReload) return;
        cur = d;
    }
    ++revision_;
    loader_->request (track, std::move (d), forceReload);
    notifyStateChanged(); // la description fait partie de l'état du projet
}

void NeptuneProcessor::uiEditInstrument (neptune::core::InstrumentDescription d, std::string name, std::string mergeKey)
{
    const int track = selectedTrack();
    for (auto& z : d.zones) z = neptune::core::sanitised (z);
    auto previous = instrumentDescription (track);
    if (previous == d) return;
    // Commande « par différences » : un geste sur quelques zones d'un gros instrument ne copie pas toute la description.
    undo_.perform (std::make_unique<neptune::core::InstrumentEditCommand> (
        previous, d,
        [this, track] { return instrumentDescription (track); },
        [this, track] (const neptune::core::InstrumentDescription& v) { setInstrumentDescription (track, v, false); },
        std::move (name), std::move (mergeKey)));
}

void NeptuneProcessor::selectTrack (int track)
{
    track = juce::jlimit (0, neptune::core::mixer::numTracks - 1, track);
    if (track == selectedTrack_.load()) return;
    {
        const juce::ScopedLock lock (instrumentLock_);
        if (! mixer_.tracks[static_cast<std::size_t> (track)].active) return; // une piste inactive n'est pas sélectionnable
        mixer_.selectedTrack = track;
    }
    selectedTrack_.store (track);
    ++revision_;
    notifyStateChanged();
}

// ---- pistes : type, activité, patch du synthé
neptune::core::TrackType NeptuneProcessor::trackType (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))].type;
}

bool NeptuneProcessor::trackActive (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))].active;
}

int NeptuneProcessor::activeTrackCount() const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.activeCount();
}

bool NeptuneProcessor::trackHasContent (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    const auto& t = mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))];
    return ! t.instrument.empty() || ! (t.synth == neptune::core::SynthDescription {}) || ! (t.fm == neptune::core::fm::FmDescription {})
        || ! (t.organ == neptune::core::organ::OrganDescription {});
}

neptune::core::SynthDescription NeptuneProcessor::synthDescription (int track) const
{
    const int t = juce::jlimit (0, neptune::core::mixer::numTracks - 1, track);
    const juce::ScopedLock lock (instrumentLock_);
    // temps d'enveloppe, glissando et équilibre : les valeurs courantes des paramètres (l'automation les a pu changer)
    return neptune::core::analogsynth::withLiveParams (mixer_.tracks[static_cast<std::size_t> (t)].synth, params_, t);
}

void NeptuneProcessor::uiEditSynth (neptune::core::SynthDescription d, std::string name, std::string mergeKey, bool presetChange)
{
    using namespace neptune::core;
    const int track = selectedTrack();
    d = sanitised (std::move (d));
    const auto previous = synthDescription (track);
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<SynthDescription>> (
        previous, d,
        [this, track, presetChange] (const SynthDescription& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.tracks[static_cast<std::size_t> (track)].synth = v; }
            publishSynthPatch (track, v, presetChange);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

neptune::core::fm::FmDescription NeptuneProcessor::fmDescription (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    const int t = juce::jlimit (0, neptune::core::mixer::numTracks - 1, track);
    return neptune::core::fm::withLiveParams (mixer_.tracks[static_cast<std::size_t> (t)].fm, params_, t);
}

void NeptuneProcessor::uiEditFm (neptune::core::fm::FmDescription d, std::string name, std::string mergeKey, bool presetChange)
{
    using namespace neptune::core;
    juce::ignoreUnused (presetChange); // une note FM garde le patch avec lequel elle a démarré : rien à figer
    const int track = selectedTrack();
    d = fm::sanitised (std::move (d));
    const auto previous = fmDescription (track);
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<fm::FmDescription>> (
        previous, d,
        [this, track] (const fm::FmDescription& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.tracks[static_cast<std::size_t> (track)].fm = v; }
            publishFmDescription (track, v);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

neptune::core::organ::OrganDescription NeptuneProcessor::organDescription (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))].organ;
}

void NeptuneProcessor::uiEditOrgan (neptune::core::organ::OrganDescription d, std::string name, std::string mergeKey)
{
    using namespace neptune::core;
    const int track = selectedTrack();
    d = organ::sanitised (std::move (d));
    const auto previous = organDescription (track);
    if (previous == d) return;
    undo_.perform (std::make_unique<SnapshotCommand<organ::OrganDescription>> (
        previous, d,
        [this, track] (const organ::OrganDescription& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.tracks[static_cast<std::size_t> (track)].organ = v; }
            publishOrganDescription (track, v);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

void NeptuneProcessor::resetTrackParameters (int track, bool includeMixer)
{
    using namespace neptune::core;
    std::vector<ParamId> ids;
    for (int k = 0; k < mixer::kOrganKinds; ++k) ids.push_back (mixer::organParam (track, k));
    for (int k = 0; k < mixer::kFmKinds; ++k) ids.push_back (mixer::fmParam (track, static_cast<mixer::FmKind> (k)));
    for (int k = 0; k < mixer::kSamplerKinds; ++k) ids.push_back (mixer::samplerParam (track, static_cast<mixer::SamplerKind> (k)));
    for (int k = 0; k < mixer::kSynthKinds; ++k) ids.push_back (mixer::synthParam (track, static_cast<mixer::SynthKind> (k)));
    if (includeMixer)
    {
        for (const auto off : { probe::kVolume, probe::kPan, probe::kMute, probe::kSolo }) ids.push_back (probe::trackParam (track, off));
        for (int s = 0; s < mixer::numSends; ++s) ids.push_back (probe::trackParam (track, probe::kSendLevel + static_cast<ParamId> (s)));
    }
    for (const auto id : ids)
    {
        const auto index = params_.indexOf (id);
        if (index == ParameterSet::npos) continue;
        const auto& spec = params_.spec (index);
        if (params_.get (id) == spec.defaultValue) continue;
        uiResetToDefault (id);
    }
}

void NeptuneProcessor::uiSetTrackType (int track, neptune::core::TrackType type)
{
    using namespace neptune::core;
    track = juce::jlimit (0, mixer::numTracks - 1, track);
    {
        const juce::ScopedLock lock (instrumentLock_);
        auto& t = mixer_.tracks[static_cast<std::size_t> (track)];
        if (t.type == type) return;
        t.type = type;
        t.instrument = InstrumentDescription {};
        t.synth = SynthDescription {};
        t.fm = fm::FmDescription {};
        t.organ = organ::OrganDescription {};
        t.synthPresetId.clear();
    }
    engine_.setTrackType (track, type);
    publishSynthPatch (track, SynthDescription {});
    publishFmDescription (track, fm::FmDescription {});
    publishOrganDescription (track, organ::OrganDescription {});
    loader_->request (track, InstrumentDescription {}, false); // libère les samples de l'ancien instrument
    resetTrackParameters (track, false);
    undo_.clear(); // l'historique parlait de l'ancienne configuration de cette piste
    ++revision_;
    notifyStateChanged();
}

int NeptuneProcessor::uiAddTrack()
{
    using namespace neptune::core;
    int index = -1;
    {
        const juce::ScopedLock lock (instrumentLock_);
        index = mixer_.firstInactive();
        if (index < 0) return -1;
        mixer_.resetTrack (index);
        auto& t = mixer_.tracks[static_cast<std::size_t> (index)];
        t.active = true;
        t.type = TrackType::Synth;
    }
    auto clearTrackInserts = [this] (int track)
    {
        for (int s = 0; s < neptune::core::mixer::numInserts; ++s)
        {
            {
                const juce::ScopedLock lock (instrumentLock_);
                insertInstances_[static_cast<std::size_t> (track)][static_cast<std::size_t> (s)] = nullptr;
            }
            engine_.setInsert (StripKind::Track, track, s, nullptr);
        }
    };
    clearTrackInserts (index);
    resetTrackParameters (index, true);
    engine_.setTrackSettings (index, trackSettings (index));
    engine_.setTrackType (index, TrackType::Synth);
    engine_.setTrackActive (index, true);
    publishSynthPatch (index, SynthDescription {});
    publishFmDescription (index, fm::FmDescription {});
    publishOrganDescription (index, organ::OrganDescription {});
    loader_->request (index, InstrumentDescription {}, false);
    undo_.clear();
    selectTrack (index);
    ++revision_;
    notifyStateChanged();
    return index;
}

bool NeptuneProcessor::uiRemoveTrack (int track)
{
    using namespace neptune::core;
    track = juce::jlimit (0, mixer::numTracks - 1, track);
    int selected = 0;
    std::array<bool, mixer::numParallels> wasActive {};
    std::vector<int> goneParallels;
    {
        const juce::ScopedLock lock (instrumentLock_);
        if (! mixer_.tracks[static_cast<std::size_t> (track)].active || mixer_.activeCount() <= 1) return false;
        for (int p = 0; p < mixer::numParallels; ++p) // les pistes parallèles qui disparaissent avec la piste
            if (mixer_.parallels[static_cast<std::size_t> (p)].active) wasActive[static_cast<std::size_t> (p)] = true;
        mixer_.resetTrack (track); // remet aussi la sélection sur une piste active si c'était celle-ci
        selected = mixer_.selectedTrack;
        for (int p = 0; p < mixer::numParallels; ++p)
            if (wasActive[static_cast<std::size_t> (p)] && ! mixer_.parallels[static_cast<std::size_t> (p)].active) goneParallels.push_back (p);
    }
    for (const int p : goneParallels) dropParallel (p);
    auto clearTrackInserts = [this] (int track)
    {
        for (int s = 0; s < neptune::core::mixer::numInserts; ++s)
        {
            {
                const juce::ScopedLock lock (instrumentLock_);
                insertInstances_[static_cast<std::size_t> (track)][static_cast<std::size_t> (s)] = nullptr;
            }
            engine_.setInsert (StripKind::Track, track, s, nullptr);
        }
    };
    clearTrackInserts (track);
    engine_.setTrackActive (track, false);
    engine_.setTrackType (track, TrackType::Sampler);
    engine_.setTrackSettings (track, trackSettings (track));
    publishSynthPatch (track, SynthDescription {});
    publishFmDescription (track, fm::FmDescription {});
    publishOrganDescription (track, organ::OrganDescription {});
    loader_->request (track, InstrumentDescription {}, false);
    resetTrackParameters (track, true);
    undo_.clear();
    selectedTrack_.store (selected);
    ++revision_;
    notifyStateChanged();
    return true;
}

// ---- pistes parallèles
bool NeptuneProcessor::parallelActive (int p) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.parallels[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numParallels - 1, p))].active;
}

neptune::core::mixer::ParallelSettings NeptuneProcessor::parallelSettings (int p) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.parallels[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numParallels - 1, p))];
}

int NeptuneProcessor::parallelDepth (int p) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.parallelDepth (p);
}

std::vector<int> NeptuneProcessor::parallelChildren (int parentId) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.parallelChildren (parentId);
}

// Arrête une piste parallèle déjà retirée de l'état : plus de traitement, inserts libérés, paramètres remis à leur valeur d'origine.
void NeptuneProcessor::dropParallel (int p)
{
    namespace mx = neptune::core::mixer;
    engine_.setParallel (p, false, 0, mx::TapPoint::PostPan);
    for (int s = 0; s < mx::numInserts; ++s)
    {
        {
            const juce::ScopedLock lock (instrumentLock_);
            insertInstances_[static_cast<std::size_t> (insertFlatIndex (StripKind::Parallel, p))][static_cast<std::size_t> (s)] = nullptr;
        }
        engine_.setInsert (StripKind::Parallel, p, s, nullptr);
    }
    std::vector<neptune::core::ParamId> ids { mx::parallelParam (p, mx::kParVolume), mx::parallelParam (p, mx::kParPan),
                                              mx::parallelParam (p, mx::kParMute), mx::parallelParam (p, mx::kParSolo) };
    for (int s = 0; s < mx::numSends; ++s) ids.push_back (mx::parallelParam (p, mx::kParSendLevel + static_cast<neptune::core::ParamId> (s)));
    for (int s = 0; s < mx::numInserts; ++s) ids.push_back (mx::parallelParam (p, mx::kParInsertBypass + static_cast<neptune::core::ParamId> (s)));
    for (const auto id : ids)
    {
        const auto index = params_.indexOf (id);
        if (index == neptune::core::ParameterSet::npos) continue;
        if (params_.get (id) == params_.spec (index).defaultValue) continue;
        uiResetToDefault (id);
    }
}

int NeptuneProcessor::uiAddParallel (int parent)
{
    namespace mx = neptune::core::mixer;
    int p = -1;
    {
        const juce::ScopedLock lock (instrumentLock_);
        if (parent < 0 || parent >= mx::numTracks + mx::numParallels) return -1;
        if (parent < mx::numTracks ? ! mixer_.tracks[static_cast<std::size_t> (parent)].active
                                   : ! mixer_.parallels[static_cast<std::size_t> (parent - mx::numTracks)].active)
            return -1;
        p = mixer_.firstInactiveParallel();
        if (p < 0) return -1;
        mx::ParallelSettings ps;
        ps.active = true;
        ps.parent = parent;
        mixer_.parallels[static_cast<std::size_t> (p)] = ps;
    }
    dropParallel (p);      // valeurs d'origine (au cas où d'anciennes valeurs d'automation traînent), puis activation
    engine_.setParallelSettings (p, parallelSettings (p).strip);
    engine_.setParallel (p, true, parent, mx::TapPoint::PostPan);
    undo_.clear();
    ++revision_;
    notifyStateChanged();
    return p;
}

int NeptuneProcessor::uiRemoveParallel (int p)
{
    namespace mx = neptune::core::mixer;
    std::vector<int> removed;
    {
        const juce::ScopedLock lock (instrumentLock_);
        removed = mixer_.removeParallel (p);
    }
    for (const int r : removed) dropParallel (r);
    if (! removed.empty())
    {
        undo_.clear();
        ++revision_;
        notifyStateChanged();
    }
    return static_cast<int> (removed.size());
}

void NeptuneProcessor::uiEditParallel (int p, neptune::core::mixer::ParallelSettings s, std::string name, std::string mergeKey)
{
    using namespace neptune::core;
    p = juce::jlimit (0, mixer::numParallels - 1, p);
    s = mixer::sanitised (std::move (s), p);
    const auto previous = parallelSettings (p);
    if (! previous.active || previous == s) return;
    s.active = true;
    undo_.perform (std::make_unique<SnapshotCommand<mixer::ParallelSettings>> (
        previous, s,
        [this, p] (const mixer::ParallelSettings& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.parallels[static_cast<std::size_t> (p)] = v; }
            engine_.setParallelSettings (p, v.strip);
            engine_.setParallel (p, v.active, v.parent, v.tap, v.followParentVolume);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

// ---- réglages du mixeur
namespace
{
// Recopie le type et les valeurs des modules vivants dans l'état à sauvegarder.
void syncInserts (std::array<neptune::core::mixer::InsertState, neptune::core::mixer::numInserts>& out,
                  const std::array<std::shared_ptr<neptune::core::InsertInstance>, neptune::core::mixer::numInserts>& live)
{
    for (std::size_t s = 0; s < out.size(); ++s)
    {
        out[s] = {};
        if (live[s] == nullptr) continue;
        out[s].type = live[s]->type;
        const auto& ps = live[s]->runner.params();
        for (std::size_t i = 0; i < ps.size(); ++i) out[s].values[ps.spec (i).id] = ps.getAt (i);
    }
}
} // namespace

neptune::core::MixerState NeptuneProcessor::mixerState() const
{
    const juce::ScopedLock lock (instrumentLock_);
    auto m = mixer_;
    namespace mx = neptune::core::mixer;
    for (int t = 0; t < mx::numTracks; ++t)
    {
        auto& tr = m.tracks[static_cast<std::size_t> (t)];
        syncInserts (tr.strip.inserts, insertInstances_[static_cast<std::size_t> (t)]);
        tr.synth = neptune::core::analogsynth::withLiveParams (tr.synth, params_, t);
        tr.fm = neptune::core::fm::withLiveParams (tr.fm, params_, t);
    }
    for (int b = 0; b < mx::numBuses; ++b) syncInserts (m.buses[static_cast<std::size_t> (b)].inserts, insertInstances_[static_cast<std::size_t> (mx::numTracks + b)]);
    syncInserts (m.master.inserts, insertInstances_[static_cast<std::size_t> (mx::numTracks + mx::numBuses)]);
    for (int p = 0; p < mx::numParallels; ++p)
        syncInserts (m.parallels[static_cast<std::size_t> (p)].strip.inserts, insertInstances_[static_cast<std::size_t> (mx::numTracks + mx::numBuses + 1 + p)]);
    return m;
}

// ---- inserts
int NeptuneProcessor::insertFlatIndex (StripKind kind, int index) noexcept
{
    namespace mx = neptune::core::mixer;
    switch (kind)
    {
        case StripKind::Track: return juce::jlimit (0, mx::numTracks - 1, index);
        case StripKind::Bus: return mx::numTracks + juce::jlimit (0, mx::numBuses - 1, index);
        case StripKind::Parallel: return mx::numTracks + mx::numBuses + 1 + juce::jlimit (0, mx::numParallels - 1, index);
        default: return mx::numTracks + mx::numBuses;
    }
}

std::string NeptuneProcessor::insertType (StripKind kind, int index, int slot) const
{
    const juce::ScopedLock lock (instrumentLock_);
    const auto& inst = insertInstances_[static_cast<std::size_t> (insertFlatIndex (kind, index))]
                                       [static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numInserts - 1, slot))];
    return inst != nullptr ? inst->type : std::string();
}

neptune::core::ParamId NeptuneProcessor::insertBypassParam (StripKind kind, int index, int slot) const
{
    namespace mx = neptune::core::mixer;
    slot = juce::jlimit (0, mx::numInserts - 1, slot);
    switch (kind)
    {
        case StripKind::Track: return neptune::core::probe::trackParam (index, neptune::core::probe::kInsertBypass + static_cast<neptune::core::ParamId> (slot));
        case StripKind::Bus: return mx::busParam (index, mx::kBusInsertBypass + static_cast<neptune::core::ParamId> (slot));
        case StripKind::Parallel: return mx::parallelParam (index, mx::kParInsertBypass + static_cast<neptune::core::ParamId> (slot));
        default: return mx::masterInsertBypassId (slot);
    }
}

bool NeptuneProcessor::insertBypassed (StripKind kind, int index, int slot) const
{
    return params_.get (insertBypassParam (kind, index, slot)) >= 0.5;
}

void NeptuneProcessor::uiSetInsert (StripKind kind, int index, int slot, const std::string& type)
{
    namespace mx = neptune::core::mixer;
    slot = juce::jlimit (0, mx::numInserts - 1, slot);
    std::shared_ptr<neptune::core::InsertInstance> inst;
    if (! type.empty())
    {
        inst = neptune::core::createInsert (type, engine_.sampleRate(), engine_.maxBlockSize());
        if (inst == nullptr) return;
    }
    {
        const juce::ScopedLock lock (instrumentLock_);
        insertInstances_[static_cast<std::size_t> (insertFlatIndex (kind, index))][static_cast<std::size_t> (slot)] = inst;
    }
    engine_.setInsert (kind, index, slot, inst);
    uiResetToDefault (insertBypassParam (kind, index, slot)); // un nouvel insert n'est pas contourné
    undo_.clear(); // l'historique pouvait pointer vers les paramètres de l'ancien module
    ++revision_;
    notifyStateChanged();
}

// Adaptateur : les gestes d'édition d'un module passent par l'historique d'annulation du plugin.
class NeptuneProcessor::InsertHost : public ParamHost
{
public:
    InsertHost (NeptuneProcessor& owner, std::shared_ptr<neptune::core::InsertInstance> inst) : owner_ (owner), inst_ (std::move (inst)) {}

    neptune::core::ParameterSet& parameters() override { return inst_->runner.params(); }
    void uiBeginEdit (neptune::core::ParamId) override { owner_.undo_.beginGesture(); }
    void uiSetNormalised (neptune::core::ParamId id, double n) override
    {
        auto& ps = parameters();
        const auto index = ps.indexOf (id);
        if (index == neptune::core::ParameterSet::npos) return;
        owner_.undo_.setParameter (ps, id, ps.spec (index).fromNormalized (n));
        ++owner_.revision_;
        owner_.notifyStateChanged();
    }
    void uiEndEdit (neptune::core::ParamId) override { owner_.undo_.endGesture(); }
    void uiResetToDefault (neptune::core::ParamId id) override
    {
        auto& ps = parameters();
        const auto index = ps.indexOf (id);
        if (index == neptune::core::ParameterSet::npos) return;
        uiBeginEdit (id);
        uiSetNormalised (id, ps.spec (index).toNormalized (ps.spec (index).defaultValue));
        uiEndEdit (id);
    }
    bool uiUndo() override { return owner_.uiUndo(); }
    bool uiRedo() override { return owner_.uiRedo(); }
    neptune::core::uistate::Size modalSize (neptune::core::uistate::Terminal t) const override { return owner_.modalSize (t); }
    void setModalSize (neptune::core::uistate::Terminal t, neptune::core::uistate::Size s) override { owner_.setModalSize (t, s); }

private:
    NeptuneProcessor& owner_;
    std::shared_ptr<neptune::core::InsertInstance> inst_;
};

NeptuneProcessor::InsertHandle NeptuneProcessor::openInsert (StripKind kind, int index, int slot)
{
    InsertHandle h;
    {
        const juce::ScopedLock lock (instrumentLock_);
        h.instance = insertInstances_[static_cast<std::size_t> (insertFlatIndex (kind, index))]
                                     [static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numInserts - 1, slot))];
    }
    if (h.instance != nullptr) h.host = std::make_unique<InsertHost> (*this, h.instance);
    return h;
}

neptune::core::mixer::StripSettings NeptuneProcessor::trackSettings (int track) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numTracks - 1, track))].strip;
}

neptune::core::mixer::StripSettings NeptuneProcessor::busSettings (int bus) const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.buses[static_cast<std::size_t> (juce::jlimit (0, neptune::core::mixer::numBuses - 1, bus))];
}

neptune::core::mixer::MasterSettings NeptuneProcessor::masterSettings() const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.master;
}

void NeptuneProcessor::publishFmDescription (int track, const neptune::core::fm::FmDescription& d)
{
    engine_.publishFm (track, std::make_shared<const neptune::core::fm::FmDescription> (neptune::core::fm::sanitised (d)));
    if (! neptune::core::fm::extraParamsMatch (d, params_, track)) // LFO, bend, molette, glissando suivent la description
        seedHostParameters (neptune::core::fm::extraParamValues (d, track));
}

void NeptuneProcessor::publishOrganDescription (int track, const neptune::core::organ::OrganDescription& d)
{
    engine_.publishOrgan (track, std::make_shared<const neptune::core::organ::OrganDescription> (neptune::core::organ::sanitised (d)));
}

void NeptuneProcessor::publishSynthPatch (int track, const neptune::core::SynthDescription& d, bool freezeRunningVoices)
{
    auto patch = std::make_shared<neptune::core::SynthPatch> (d);
    patch->freezeRunningVoices = freezeRunningVoices;
    engine_.publishSynth (track, std::move (patch));
    // Les paramètres d'enveloppe, de glissando et d'équilibre suivent la description (après la publication : les notes figées par
    // la capture par voix gardent les valeurs d'avant). L'hôte est prévenu pour que ses boutons et son automation suivent.
    if (! neptune::core::analogsynth::envelopeParamsMatch (d, params_, track))
        seedHostParameters (neptune::core::analogsynth::envelopeParamValues (d, track));
}

void NeptuneProcessor::seedHostParameters (const std::vector<std::pair<neptune::core::ParamId, double>>& values)
{
    for (const auto& [pid, raw] : values)
    {
        const auto index = params_.indexOf (pid);
        if (index == neptune::core::ParameterSet::npos) continue;
        const double v = params_.spec (index).clamp (raw);
        if (params_.getAt (index) == v) continue;
        params_.set (pid, v);
        if (! notifyHostOnSeed_ || index >= static_cast<std::size_t> (getParameters().size())) continue; // état en cours de chargement : l'hôte relit tout après
        if (auto* hostParam = getParameters()[static_cast<int> (index)])
        {
            hostParam->beginChangeGesture();
            hostParam->setValueNotifyingHost (static_cast<float> (params_.getNormalizedAt (index)));
            hostParam->endChangeGesture();
        }
    }
}

// ---- presets de synthé de la piste sélectionnée
void NeptuneProcessor::setTrackPresetId (int track, const std::string& id)
{
    {
        const juce::ScopedLock lock (instrumentLock_);
        mixer_.tracks[static_cast<std::size_t> (track)].synthPresetId = id;
    }
    ++presetRevision_;
    notifyStateChanged();
}

std::string NeptuneProcessor::currentPresetId() const
{
    const juce::ScopedLock lock (instrumentLock_);
    return mixer_.tracks[static_cast<std::size_t> (selectedTrack())].synthPresetId;
}

const neptune::core::preset::Preset* NeptuneProcessor::currentPreset() const
{
    const auto id = currentPresetId();
    return id.empty() ? nullptr : presetLibrary_->find (id);
}

bool NeptuneProcessor::acceptsPreset (const neptune::core::preset::Preset& p) const
{
    using namespace neptune::core;
    if (p.module.empty()) return true;
    const auto t = trackType (selectedTrack());
    if (t == TrackType::Organ) return p.module == organ::moduleId;
    if (t == TrackType::Fm) return p.module == fm::moduleId;
    if (t == TrackType::Synth) return p.module == analogsynth::moduleId;
    return false;
}

std::vector<std::string> NeptuneProcessor::presetKeys() const
{
    using namespace neptune::core;
    std::vector<std::string> keys;
    if (trackType (selectedTrack()) == TrackType::Organ)
        for (const auto& k : organ::keyedParams()) keys.push_back (k.key);
    else if (trackType (selectedTrack()) == TrackType::Fm)
        for (const auto& k : fm::keyedParams()) keys.push_back (k.key);
    else
    {
        for (const auto& k : analogsynth::keyedParams()) keys.push_back (k.key);
        keys.push_back (analogsynth::glideKey);
    }
    return keys;
}

bool NeptuneProcessor::loadPreset (const std::string& id)
{
    using namespace neptune::core;
    const int track = selectedTrack();
    const auto type = trackType (track);
    if (type != TrackType::Synth && type != TrackType::Fm && type != TrackType::Organ) return false;
    const auto* p = presetLibrary_->find (id);
    if (p == nullptr || ! acceptsPreset (*p)) return false;
    const auto& cfg = presetLibrary_->config();

    undo_.beginTransaction ("Preset");
    std::vector<std::pair<ParamId, double>> values; // identifiants de la piste, valeurs à écrire
    if (type == TrackType::Organ)
    {
        // l'orgue n'a que des valeurs (les claviers ne font pas partie d'un preset) ; une clé absente remet le réglage à son défaut
        for (const auto& k : organ::keyedParams())
        {
            if (preset::captureOf (k.key, *p, cfg) == preset::Capture::Free) continue;
            const ParamId pid = organ::idForTrack (k.id, track);
            const auto index = params_.indexOf (pid);
            if (index == ParameterSet::npos) continue;
            const auto it = p->parameters.find (k.key);
            values.emplace_back (pid, it != p->parameters.end() && ! it->second.isText ? it->second.number : params_.spec (index).defaultValue);
        }
    }
    else if (type == TrackType::Fm)
    {
        uiEditFm (fm::presetDescription (*p, fmDescription (track), cfg), "Preset", {}, true);
        // macros : celles du preset, sinon leur défaut (un patch importé ne porte pas de macros)
        for (const auto& k : fm::keyedParams())
        {
            if (preset::captureOf (k.key, *p, cfg) == preset::Capture::Free) continue;
            const ParamId pid = fm::idForTrack (k.id, track);
            const auto index = params_.indexOf (pid);
            if (index == ParameterSet::npos) continue;
            const auto it = p->parameters.find (k.key);
            values.emplace_back (pid, it != p->parameters.end() && ! it->second.isText ? it->second.number : params_.spec (index).defaultValue);
        }
    }
    else
    {
        // le patch d'abord (capture par voix : l'audio fige les notes en cours avec les valeurs d'avant), puis les valeurs
        uiEditSynth (analogsynth::presetDescription (*p, synthDescription (track), cfg), "Preset", {}, true);
        for (const auto& [key, value] : p->parameters)
        {
            const ParamId base = analogsynth::paramIdForKey (key);
            if (base == 0 || value.isText || preset::captureOf (key, *p, cfg) == preset::Capture::Free) continue;
            values.emplace_back (analogsynth::idForTrack (base, track), value.number);
        }
    }
    for (const auto& [pid, raw] : values)
    {
        const auto index = params_.indexOf (pid);
        if (index == ParameterSet::npos) continue;
        const double v = params_.spec (index).clamp (raw);
        if (params_.getAt (index) == v) continue;
        undo_.setParameter (params_, pid, v);
        auto* hostParam = getParameters()[static_cast<int> (index)];
        hostParam->beginChangeGesture();
        hostParam->setValueNotifyingHost (static_cast<float> (params_.getNormalizedAt (index)));
        hostParam->endChangeGesture();
    }
    undo_.endTransaction();
    control_.resetPickup(); // les assignations « reprise » attendent de nouveau le passage de la source
    setTrackPresetId (track, id);
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged (false).withNonParameterStateChanged (true));
    return true;
}

bool NeptuneProcessor::stepPreset (int direction)
{
    const auto next = presetLibrary_->stepFrom (currentPresetId(), direction,
                                                [this] (const neptune::core::preset::Preset& p) { return acceptsPreset (p); });
    return ! next.empty() && loadPreset (next);
}

bool NeptuneProcessor::isModified() const
{
    using namespace neptune::core;
    const auto* p = currentPreset();
    if (p == nullptr) return false;
    const int track = selectedTrack();
    if (trackType (track) == TrackType::Organ) return organ::differsFromPreset (*p, params_, presetLibrary_->config(), track);
    if (trackType (track) == TrackType::Fm)
        return fm::differsFromPreset (*p, params_, fmDescription (track), presetLibrary_->config(), track);
    return analogsynth::differsFromPreset (*p, params_, synthDescription (track), presetLibrary_->config(), track);
}

bool NeptuneProcessor::canSaveCurrent() const
{
    const auto* p = currentPreset();
    return p != nullptr && ! p->factory && ! p->locked;
}

bool NeptuneProcessor::overwritePreset (const std::string& id, std::string* error)
{
    using namespace neptune::core;
    const auto* base = presetLibrary_->find (id);
    if (base == nullptr || base->factory) { if (error) *error = "factory preset is read-only"; return false; }
    if (base->locked) { if (error) *error = "preset is locked"; return false; }
    const int track = selectedTrack();
    preset::Preset p = trackType (track) == TrackType::Organ
        ? organ::capturePreset (params_, presetLibrary_->config(), base->name, base->type, base, track)
        : trackType (track) == TrackType::Fm
        ? fm::capturePreset (params_, fmDescription (track), presetLibrary_->config(), base->name, base->type, base, track)
        : analogsynth::capturePreset (params_, synthDescription (track), presetLibrary_->config(), base->name, base->type, base, track);
    const bool ok = presetLibrary_->save (std::move (p), error);
    if (ok) ++presetRevision_;
    return ok;
}

bool NeptuneProcessor::saveCurrent (std::string* error)
{
    const auto id = currentPresetId();
    if (id.empty()) { if (error) *error = "no current preset"; return false; }
    return overwritePreset (id, error);
}

bool NeptuneProcessor::saveCurrentAs (const std::string& name, const std::string& type, neptune::core::preset::Preset* out, std::string* error)
{
    using namespace neptune::core;
    const int track = selectedTrack();
    const auto tt = trackType (track);
    if (tt != TrackType::Synth && tt != TrackType::Fm && tt != TrackType::Organ) { if (error) *error = "not a synth, FM or organ track"; return false; }
    auto capture = [&] (const preset::Preset* base, const std::string& n, const std::string& ty)
    {
        if (tt == TrackType::Organ) return organ::capturePreset (params_, presetLibrary_->config(), n, ty, base, track);
        return tt == TrackType::Fm ? fm::capturePreset (params_, fmDescription (track), presetLibrary_->config(), n, ty, base, track)
                                   : analogsynth::capturePreset (params_, synthDescription (track), presetLibrary_->config(), n, ty, base, track);
    };
    auto p = capture (nullptr, name, type);
    if (const auto* cur = currentPreset())
    {
        p.characteristics = cur->characteristics;
        p.customFields = cur->customFields;
        p.rating = cur->rating;
        p.captureOverrides = cur->captureOverrides;
        if (p.type.empty()) p.type = cur->type;
        p.derivedFrom = cur->factory ? cur->id : cur->derivedFrom;
        p = capture (&p, name, p.type);
    }
    preset::Preset stored;
    if (! presetLibrary_->importPreset (std::move (p), &stored, error)) return false;
    setTrackPresetId (track, stored.id);
    if (out != nullptr) *out = stored;
    return true;
}

bool NeptuneProcessor::saveConfig (const neptune::core::preset::LibraryConfig& c)
{
    const bool ok = presetLibrary_->setConfig (c);
    if (ok) ++presetRevision_;
    return ok;
}

void NeptuneProcessor::pushMixerSettingsToEngine()
{
    const auto m = mixerState();
    for (int t = 0; t < neptune::core::mixer::numTracks; ++t)
    {
        const auto& ts = m.tracks[static_cast<std::size_t> (t)];
        engine_.setTrackSettings (t, ts.strip);
        engine_.setTrackType (t, ts.type);
        engine_.setTrackActive (t, ts.active);
        publishSynthPatch (t, ts.synth);
        publishFmDescription (t, ts.fm);
        publishOrganDescription (t, ts.organ);
    }
    for (int b = 0; b < neptune::core::mixer::numBuses; ++b) engine_.setBusSettings (b, m.buses[static_cast<std::size_t> (b)]);
    for (int p = 0; p < neptune::core::mixer::numParallels; ++p)
    {
        const auto& ps = m.parallels[static_cast<std::size_t> (p)];
        engine_.setParallelSettings (p, ps.strip);
        engine_.setParallel (p, ps.active, ps.parent, ps.tap, ps.followParentVolume);
    }
    engine_.setMasterSettings (m.master);
}

namespace
{
using neptune::core::mixer::StripSettings;
}

void NeptuneProcessor::uiEditTrack (int track, StripSettings s, std::string name, std::string mergeKey)
{
    using namespace neptune::core;
    track = juce::jlimit (0, mixer::numTracks - 1, track);
    s = mixer::sanitisedStrip (std::move (s), false, track);
    const auto previous = trackSettings (track);
    if (previous == s) return;
    undo_.perform (std::make_unique<SnapshotCommand<StripSettings>> (
        previous, s,
        [this, track] (const StripSettings& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.tracks[static_cast<std::size_t> (track)].strip = v; }
            engine_.setTrackSettings (track, v);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

void NeptuneProcessor::uiEditBus (int bus, StripSettings s, std::string name, std::string mergeKey)
{
    using namespace neptune::core;
    bus = juce::jlimit (0, mixer::numBuses - 1, bus);
    s = mixer::sanitisedStrip (std::move (s), true, bus);
    const auto previous = busSettings (bus);
    if (previous == s) return;
    undo_.perform (std::make_unique<SnapshotCommand<StripSettings>> (
        previous, s,
        [this, bus] (const StripSettings& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.buses[static_cast<std::size_t> (bus)] = v; }
            engine_.setBusSettings (bus, v);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

void NeptuneProcessor::uiEditMaster (neptune::core::mixer::MasterSettings m, std::string name, std::string mergeKey)
{
    using namespace neptune::core;
    m = mixer::sanitised (m);
    const auto previous = masterSettings();
    if (previous == m) return;
    undo_.perform (std::make_unique<SnapshotCommand<mixer::MasterSettings>> (
        previous, m,
        [this] (const mixer::MasterSettings& v)
        {
            { const juce::ScopedLock lock (instrumentLock_); mixer_.master = v; }
            engine_.setMasterSettings (v);
            ++revision_;
            notifyStateChanged();
        },
        std::move (name), std::move (mergeKey)));
}

// Chargement d'un état (projet ou autosave) : ne marque pas le projet modifié.
void NeptuneProcessor::adoptMixerFromState (const std::optional<neptune::core::MixerState>& m)
{
    if (! m) return;
    std::array<bool, neptune::core::mixer::numTracks> changed {};
    {
        const juce::ScopedLock lock (instrumentLock_);
        for (std::size_t t = 0; t < mixer_.tracks.size(); ++t)
        {
            changed[t] = ! (mixer_.tracks[t].instrument == m->tracks[t].instrument);
            mixer_.tracks[t] = m->tracks[t];
        }
        mixer_.buses = m->buses;
        mixer_.parallels = m->parallels;
        mixer_.master = m->master;
        mixer_.selectedTrack = m->selectedTrack;
    }
    selectedTrack_.store (m->selectedTrack);
    pushMixerSettingsToEngine();
    undo_.clear(); // l'historique pointait vers les modules et les instruments remplacés
    {
        namespace mx = neptune::core::mixer;
        auto build = [this] (StripKind kind, int index, const std::array<mx::InsertState, mx::numInserts>& states)
        {
            for (int s = 0; s < mx::numInserts; ++s)
            {
                std::shared_ptr<neptune::core::InsertInstance> inst;
                const auto& st = states[static_cast<std::size_t> (s)];
                if (! st.empty())
                {
                    inst = neptune::core::createInsert (st.type, engine_.sampleRate(), engine_.maxBlockSize());
                    if (inst != nullptr)
                        for (const auto& [id, v] : st.values) inst->runner.params().set (id, v);
                }
                {
                    const juce::ScopedLock lock (instrumentLock_);
                    insertInstances_[static_cast<std::size_t> (insertFlatIndex (kind, index))][static_cast<std::size_t> (s)] = inst;
                }
                engine_.setInsert (kind, index, s, inst);
            }
        };
        for (int t = 0; t < mx::numTracks; ++t) build (StripKind::Track, t, m->tracks[static_cast<std::size_t> (t)].strip.inserts);
        for (int b = 0; b < mx::numBuses; ++b) build (StripKind::Bus, b, m->buses[static_cast<std::size_t> (b)].inserts);
        build (StripKind::Master, 0, m->master.inserts);
        for (int p = 0; p < mx::numParallels; ++p) build (StripKind::Parallel, p, m->parallels[static_cast<std::size_t> (p)].strip.inserts);
    }
    ++revision_;
    for (std::size_t t = 0; t < m->tracks.size(); ++t)
        if (changed[t]) loader_->request (static_cast<int> (t), m->tracks[t].instrument, false);
}

NeptuneProcessor::BusesProperties NeptuneProcessor::makeBuses (int numOutputs)
{
    BusesProperties props;
    for (int i = 0; i < numOutputs; ++i)
        props = props.withOutput ("Out " + juce::String (i + 1), juce::AudioChannelSet::stereo(), true);
    return props;
}

NeptuneProcessor::NeptuneProcessor()
    : juce::AudioProcessor (makeBuses (NEPTUNE_PROBE_OUTPUTS)),
      probeTracks_ (NEPTUNE_PROBE_TRACKS),
      probeOutputs_ (NEPTUNE_PROBE_OUTPUTS)
{
    std::string error;
    const bool ok = neptune::core::probe::buildPool (params_, probeTracks_, &error);
    jassert (ok);
    const bool samplerOk = neptune::core::sampler::addParams (params_, &error);
    jassert (samplerOk);
    const bool mixerOk = neptune::core::mixer::addParams (params_, probeTracks_, neptune::core::mixer::numBuses, &error);
    jassert (mixerOk);
    juce::ignoreUnused (ok, samplerOk, mixerOk);
    engine_.attachParameters (&params_);
    mixer_ = neptune::core::MixerState::freshProject(); // projet neuf : une seule piste, un synthé
    pushMixerSettingsToEngine();

    for (std::size_t i = 0; i < params_.size(); ++i)
        addParameter (new NeptuneParameter (params_, i, hostProbe_)); // l'AudioProcessor en prend la propriété

    // Réglages globaux : lus une fois au démarrage ; le fichier est créé avec les valeurs par défaut s'il manque.
    const auto configDir = configDirectory();
    settingsFile_ = configDir / "settings.json";
    std::vector<std::string> warnings;
    settings_ = neptune::core::settings::loadFile (settingsFile_, &warnings);
    for (const auto& w : warnings)
        DBG ("Neptune settings: " << juce::String (w));
    loader_ = std::make_unique<InstrumentLoader> (*this);
    {
        // Presets de synthé : la même bibliothèque qu'AnalogSynth (dossier choisi dans les réglages, sinon <config>/AnalogSynth).
        auto root = settings_.presetsDir.empty() ? configDir / "AnalogSynth" : std::filesystem::u8path (settings_.presetsDir);
        presetLibrary_ = std::make_unique<neptune::core::preset::Library> (std::move (root), [] { auto all = neptune::core::analogsynth::factoryPresets(); for (auto& f : neptune::core::fm::factoryPresets()) all.push_back (std::move (f)); for (auto& f : neptune::core::organ::factoryPresets()) all.push_back (std::move (f)); return all; }());
        presetLibrary_->open (&warnings);
        if (presetLibrary_->config().parameterCaptureDefaults.empty()) presetLibrary_->setConfig (neptune::core::analogsynth::defaultConfig());
        {
            // la vitesse du Leslie est libre par défaut (configuration partagée avec les autres plugins : on complète)
            auto cfg = presetLibrary_->config();
            if (cfg.parameterCaptureDefaults.count (neptune::core::organ::leslieSpeedKey) == 0)
            {
                cfg.parameterCaptureDefaults[neptune::core::organ::leslieSpeedKey] = neptune::core::preset::Capture::Free;
                presetLibrary_->setConfig (cfg);
            }
        }
    }
    uiStateFile_ = configDir / "ui_state.json";
    uiState_ = neptune::core::uistate::loadFile (uiStateFile_);

    // Première instance du processus : le profil global vient de settings.json ; les suivantes le partagent.
    neptune::core::SharedProfile::process().initialiseOnce (settings_.profile);
    std::error_code ec;
    if (! std::filesystem::exists (settingsFile_, ec))
        neptune::core::settings::saveFile (settingsFile_, settings_);

    // Autosave : fichiers orphelins trop anciens supprimés, puis écriture périodique de l'état s'il a changé.
    const auto autosaveDir = configDir / "autosave";
    neptune::core::AutosaveStore::purgeStale (autosaveDir, settings_.autosaveKeepDays);
    autosave_ = std::make_unique<neptune::core::AutosaveStore> (autosaveDir);
    autosave_->setBaseline (currentStateText());

    if (settings_.autosaveIntervalSeconds > 0)
    {
        autosaveTimer_.onTick = [this] { autosaveNow(); };
        autosaveTimer_.startTimer (settings_.autosaveIntervalSeconds * 1000);
    }

    control_.configure (neptune::core::control::Config {}, params_); // CC et OSC désactivés tant que l'utilisateur ne les active pas
    controlTimer_.onTick = [this] { controlTick(); };
    controlTimer_.startTimer (33);
}

NeptuneProcessor::~NeptuneProcessor()
{
    controlTimer_.stopTimer();
    autosaveTimer_.stopTimer();
    autosave_->discard(); // fermeture propre : il n'y a rien à récupérer
}

void NeptuneProcessor::uiSetControlConfig (neptune::core::control::Config c)
{
    control_.configure (c, params_);
    notifyStateChanged();
}

void NeptuneProcessor::uiSetCcEnabled (bool on)
{
    auto c = control_.config();
    if (c.settings.ccEnabled == on) return;
    c.settings.ccEnabled = on;
    uiSetControlConfig (std::move (c));
}

bool NeptuneProcessor::uiAssignOrganControllers()
{
    using namespace neptune::core;
    const int track = selectedTrack();
    if (trackType (track) != TrackType::Organ) return false;
    auto c = control_.config();
    // les assignations de cette piste d'orgue sont remplacées, celles des autres pistes et des autres cibles restent
    c.bindings.erase (std::remove_if (c.bindings.begin(), c.bindings.end(),
                                      [track] (const control::Binding& b) { return ! b.target.isAction && organ::isOrganParam (b.target.param, track); }),
                      c.bindings.end());
    for (auto& b : organ::organControlBindings (organDescription (track), track)) c.bindings.push_back (std::move (b));
    uiSetControlConfig (std::move (c));
    return true;
}

void NeptuneProcessor::uiClearControllers()
{
    auto c = control_.config();
    c.bindings.clear();
    uiSetControlConfig (std::move (c));
}

// Relais du thread de message : actions demandées par une source (preset suivant, panique...) et paramètres modifiés par une source
// externe, signalés à l'hôte (regroupés : une notification par paramètre et par tic).
void NeptuneProcessor::controlTick()
{
    using neptune::core::control::Action;
    control_.collectGarbage();
    for (Action a = Action::None; control_.popAction (a);)
    {
        switch (a)
        {
            case Action::PresetNext: stepPreset (1); break;
            case Action::PresetPrevious: stepPreset (-1); break;
            case Action::Panic: uiPanic(); break;
            case Action::TrackNext:
            case Action::TrackPrevious:
            {
                const int step = a == Action::TrackNext ? 1 : -1;
                const int n = neptune::core::mixer::numTracks;
                for (int k = 1; k < n; ++k)
                {
                    const int t = ((selectedTrack() + step * k) % n + n) % n;
                    if (trackActive (t)) { selectTrack (t); break; }
                }
                break;
            }
            default: break;
        }
    }
    std::vector<std::size_t> changed;
    if (control_.drainChanges (changed) == 0) return;
    for (const auto index : changed)
        if (index < static_cast<std::size_t> (getParameters().size()))
        {
            auto* p = getParameters()[static_cast<int> (index)];
            p->beginChangeGesture();
            p->setValueNotifyingHost (static_cast<float> (params_.getNormalizedAt (index)));
            p->endChangeGesture();
        }
}

std::string NeptuneProcessor::currentStateText() const
{
    const auto mixer = mixerState();
    const auto control = neptune::core::control::toJson (control_.config());
    return neptune::core::state::save (params_, autosave_->id(), profile_.locked(), &mixer, control);
}

// Changement d'état qui n'est pas un paramètre (le verrou de profil) : le wrapper CLAP le traduit en
// clap_host_state.mark_dirty, ce qui fait marquer le projet comme modifié par l'hôte.
void NeptuneProcessor::notifyStateChanged()
{
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
}

void NeptuneProcessor::uiBeginEdit (neptune::core::ParamId id)
{
    const auto index = params_.indexOf (id);
    if (index == neptune::core::ParameterSet::npos) return;
    getParameters()[static_cast<int> (index)]->beginChangeGesture();
    undo_.beginGesture();
}

void NeptuneProcessor::uiSetNormalised (neptune::core::ParamId id, double normalised)
{
    const auto index = params_.indexOf (id);
    if (index == neptune::core::ParameterSet::npos) return;
    // D'abord le cœur par la pile d'annulation (elle lit l'ancienne valeur), puis l'hôte est prévenu.
    undo_.setParameter (params_, id, params_.spec (index).fromNormalized (normalised));
    getParameters()[static_cast<int> (index)]->setValueNotifyingHost (static_cast<float> (params_.getNormalizedAt (index)));
}

void NeptuneProcessor::uiEndEdit (neptune::core::ParamId id)
{
    const auto index = params_.indexOf (id);
    if (index == neptune::core::ParameterSet::npos) return;
    undo_.endGesture();
    getParameters()[static_cast<int> (index)]->endChangeGesture();
}

void NeptuneProcessor::uiResetToDefault (neptune::core::ParamId id)
{
    const auto index = params_.indexOf (id);
    if (index == neptune::core::ParameterSet::npos) return;
    uiBeginEdit (id);
    uiSetNormalised (id, params_.spec (index).toNormalized (params_.spec (index).defaultValue));
    uiEndEdit (id);
}

// Annuler/rétablir changent des valeurs hors des événements de paramètres : l'hôte les relit (comme au chargement d'un état).
bool NeptuneProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done)
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

bool NeptuneProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done)
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true).withNonParameterStateChanged (true));
    return done;
}

void NeptuneProcessor::setLastImportTemplate (const std::string& json)
{
    if (json == uiState_.lastImportTemplate) return;
    uiState_.lastImportTemplate = json;
    std::string error;
    if (! neptune::core::uistate::saveFile (uiStateFile_, uiState_, &error))
        DBG ("Neptune ui state not saved: " << juce::String (error));
}

void NeptuneProcessor::setModalSize (neptune::core::uistate::Terminal t, neptune::core::uistate::Size s)
{
    auto next = uiState_;
    next.setModalSize (t, s);
    if (next == uiState_) return;
    uiState_ = next;
    std::string error;
    if (! neptune::core::uistate::saveFile (uiStateFile_, uiState_, &error))
        DBG ("Neptune ui state not saved: " << juce::String (error));
}

void NeptuneProcessor::autosaveNow()
{
    std::string error;
    if (! autosave_->write (currentStateText(), &error))
        DBG ("Neptune autosave failed: " << juce::String (error));
}

// L'état du projet porte l'identifiant de l'instance de la session précédente : on le reprend pour retrouver
// son autosave. Le fichier laissé par un crash est mis de côté et proposé à l'utilisateur.
void NeptuneProcessor::adoptStateIdentity (const std::string& idFromState)
{
    if (! idFromState.empty())
        autosave_->adoptId (idFromState); // refusé si invalide ou déjà pris : on garde l'identifiant courant

    const std::string loaded = currentStateText();
    autosave_->setBaseline (loaded);

    auto rec = autosave_->takeRecoverable();
    if (! rec) return;
    if (rec->state == loaded) // identique à ce que le projet vient de charger : rien à proposer
    {
        autosave_->resolveRecovered();
        return;
    }
    const juce::ScopedLock lock (recoveryLock_);
    recovered_ = std::move (rec);
}

bool NeptuneProcessor::hasRecoverableState() const
{
    const juce::ScopedLock lock (recoveryLock_);
    return recovered_.has_value();
}

juce::String NeptuneProcessor::recoverableDescription() const
{
    const juce::ScopedLock lock (recoveryLock_);
    if (! recovered_) return {};
    return juce::Time (static_cast<juce::int64> (recovered_->savedAt) * 1000).toString (true, true);
}

void NeptuneProcessor::restoreRecovered()
{
    std::optional<neptune::core::RecoveredState> rec;
    {
        const juce::ScopedLock lock (recoveryLock_);
        rec = std::move (recovered_);
        recovered_.reset();
    }
    if (! rec) return;

    const auto result = neptune::core::state::load (params_, rec->state);
    if (! result.ok())
    {
        DBG ("Neptune: recovered state not loaded: " << juce::String (result.message));
    }
    else
    {
        profile_.setLocked (result.profileLock);
        control_.configure (result.controlJson.empty() ? neptune::core::control::Config {} : neptune::core::control::configFromJson (result.controlJson), params_);
        adoptMixerFromState (result.mixer);
    }
    autosave_->resolveRecovered();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    autosaveNow();
}

void NeptuneProcessor::discardRecovered()
{
    {
        const juce::ScopedLock lock (recoveryLock_);
        recovered_.reset();
    }
    autosave_->resolveRecovered();
}

void NeptuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);
}

void NeptuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    engine_.setProfile (profile_.effective()); // lecture atomique, sans verrou
    ++hostProbe_.blocks;
    hostProbe_.lastBlockSize = buffer.getNumSamples();
    hostProbe_.outputChannels = buffer.getNumChannels();
    if (auto* head = getPlayHead())
    {
        if (const auto pos = head->getPosition())
        {
            hostProbe_.hasTransport = true;
            hostProbe_.playing = pos->getIsPlaying();
            hostProbe_.bpm = pos->getBpm().orFallback (0.0);
            hostProbe_.ppq = pos->getPpqPosition().orFallback (0.0);
            if (const auto sig = pos->getTimeSignature())
            {
                hostProbe_.timeSigNum = sig->numerator;
                hostProbe_.timeSigDen = sig->denominator;
            }
        }
        else
            hostProbe_.hasTransport = false;
    }

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            ++hostProbe_.midiNoteOns;
        if (msg.isAllSoundOff())
            engine_.panic();
        else if (msg.isAllNotesOff())
            engine_.releaseAll();
        else if (msg.isNoteOn())
            engine_.noteOn (msg.getChannel() - 1, msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine_.noteOff (msg.getChannel() - 1, msg.getNoteNumber());
        else if (msg.isController())
            control_.controlChange (msg.getChannel() - 1, msg.getControllerNumber(), msg.getControllerValue(), params_); // désactivé par défaut
    }

    engine_.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void NeptuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const std::string text = currentStateText();
    dest.append (text.data(), text.size());
}

void NeptuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    const auto result = neptune::core::state::load (
        params_, std::string_view (static_cast<const char*> (data), static_cast<size_t> (sizeInBytes)));
    if (! result.ok())
    {
        // refusé en bloc : aucun paramètre modifié
        DBG ("Neptune: state not loaded: " << juce::String (result.message));
        return;
    }

    undo_.clear(); // l'historique d'un autre état ne s'applique plus (il restaurerait un ancien instrument)
    profile_.setLocked (result.profileLock); // avant adoptStateIdentity : la référence de l'autosave le contient
    // couche de contrôle : absente des projets d'avant (tout désactivé)
    control_.configure (result.controlJson.empty() ? neptune::core::control::Config {} : neptune::core::control::configFromJson (result.controlJson), params_);
    notifyHostOnSeed_ = false; // le rescan ci-dessous prévient l'hôte des valeurs d'enveloppe recopiées depuis les descriptions
    adoptMixerFromState (result.mixer);
    notifyHostOnSeed_ = true;
    adoptStateIdentity (result.instanceId);

    // Les valeurs ont changé hors des événements de paramètres : le wrapper en tire un `params.rescan(VALUES)`
    // vers l'hôte (exigé par le CLAP et vérifié par clap-validator, test state-reproducibility).
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

bool NeptuneProcessor::supportsDirectEvent (uint16_t spaceId, uint16_t type)
{
    if (spaceId != CLAP_CORE_EVENT_SPACE_ID) return false;
    return type == CLAP_EVENT_NOTE_ON || type == CLAP_EVENT_NOTE_OFF || type == CLAP_EVENT_NOTE_CHOKE
        || type == CLAP_EVENT_NOTE_EXPRESSION;
}

// Thread audio, appelé par le wrapper au milieu du bloc, avant processBlock.
void NeptuneProcessor::handleDirectEvent (const clap_event_header_t* event, int)
{
    switch (event->type)
    {
        case CLAP_EVENT_NOTE_ON:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (event);
            engine_.noteOn (n->channel, n->key, static_cast<float> (n->velocity));
            hostProbe_.lastNoteId = n->note_id;
            hostProbe_.lastKey = n->key;
            ++hostProbe_.clapNoteOns;
            break;
        }
        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
        {
            const auto* n = reinterpret_cast<const clap_event_note*> (event);
            engine_.noteOff (n->channel, n->key);
            ++hostProbe_.clapNoteOffs;
            // Le moteur ne produit pas encore de son : la note est terminée tout de suite.
            if (numPendingEnds_ < static_cast<int> (pendingEnds_.size()))
                pendingEnds_[static_cast<size_t> (numPendingEnds_++)] = { n->note_id, n->port_index, n->channel, n->key };
            break;
        }
        case CLAP_EVENT_NOTE_EXPRESSION:
        {
            const auto* e = reinterpret_cast<const clap_event_note_expression*> (event);
            if (e->expression_id >= 0 && e->expression_id < HostProbe::numExpressions)
            {
                const auto i = static_cast<size_t> (e->expression_id);
                hostProbe_.expressionLast[i] = e->value;
                ++hostProbe_.expressionCount[i];
            }
            break;
        }
        default: break;
    }
}

void NeptuneProcessor::addOutboundEventsToQueue (const clap_output_events* out, const juce::MidiBuffer&, int sampleOffset)
{
    for (int i = 0; i < numPendingEnds_; ++i)
    {
        const auto& p = pendingEnds_[static_cast<size_t> (i)];
        clap_event_note ev {};
        ev.header.size = sizeof (clap_event_note);
        ev.header.time = static_cast<std::uint32_t> (sampleOffset);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_NOTE_END;
        ev.header.flags = 0;
        ev.note_id = p.noteId;
        ev.port_index = p.port;
        ev.channel = p.channel;
        ev.key = p.key;
        ev.velocity = 0.0;
        if (out->try_push (out, &ev.header))
            ++hostProbe_.noteEndsSent;
    }
    numPendingEnds_ = 0;
}

juce::AudioProcessorEditor* NeptuneProcessor::createEditor() { return new NeptuneEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeptuneProcessor(); }
