#include "PluginProcessor.h"
#include "NeptuneParameter.h"
#include "PluginEditor.h"
#include "neptune/core/InstrumentEdit.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/SamplerParams.h"
#include "neptune/core/State.h"
#include <set>

#ifndef NEPTUNE_PROBE_TRACKS
 #define NEPTUNE_PROBE_TRACKS 32
#endif

// Nombre de sorties stéréo déclarées à l'hôte (sonde du point 4 : la cible du projet est jusqu'à 64).
#ifndef NEPTUNE_PROBE_OUTPUTS
 #define NEPTUNE_PROBE_OUTPUTS 64
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

// Thread de chargement des samples : construit l'instrument (lit le disque), le publie au moteur, et libère les
// instruments rendus par le thread audio. Seul consommateur de la file de collecte (InstrumentSlot::collect).
class NeptuneProcessor::InstrumentLoader : public juce::Thread
{
public:
    explicit InstrumentLoader (NeptuneProcessor& owner) : juce::Thread ("Neptune sample loader"), owner_ (owner) { startThread(); }
    ~InstrumentLoader() override { stopThread (15000); }

    void request (neptune::core::InstrumentDescription d, bool force)
    {
        {
            const juce::ScopedLock lock (lock_);
            pending_ = std::move (d);
            hasPending_ = true;
            force_ = force_ || force;
        }
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (500);
            owner_.engine_.collectGarbage();

            neptune::core::InstrumentDescription d;
            bool force = false;
            {
                const juce::ScopedLock lock (lock_);
                if (! hasPending_) continue;
                d = std::move (pending_);
                hasPending_ = false;
                force = force_;
                force_ = false;
            }

            if (force) cache_.clear();
            InstrumentStatus st;
            if (d.empty())
            {
                owner_.engine_.publishInstrument (std::make_shared<neptune::core::Instrument>());
                owner_.setStatus (st); // None
                continue;
            }

            st.kind = InstrumentStatus::Kind::Loading;
            st.total = static_cast<int> (d.zones.size());
            owner_.setStatus (st);
            std::vector<std::string> warnings;
            // Abandonne dès qu'une nouvelle demande arrive (le cache garde ce qui est déjà lu) ou à l'arrêt du plugin.
            auto inst = neptune::core::buildInstrument (d, &warnings, &cache_, [&] (int done, int total)
            {
                if (threadShouldExit() || hasPendingRequest()) return false;
                if (done % 16 == 0)
                {
                    InstrumentStatus p;
                    p.kind = InstrumentStatus::Kind::Loading;
                    p.done = done;
                    p.total = total;
                    owner_.setStatus (std::move (p));
                }
                return true;
            });
            if (threadShouldExit()) break;
            if (inst == nullptr) continue; // annulé : la demande suivante est traitée au tour d'après

            owner_.engine_.publishInstrument (inst);
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
            owner_.setStatus (st);
        }
    }

    bool hasPendingRequest()
    {
        const juce::ScopedLock lock (lock_);
        return hasPending_;
    }

private:
    NeptuneProcessor& owner_;
    juce::CriticalSection lock_;
    neptune::core::InstrumentDescription pending_;
    bool hasPending_ = false, force_ = false;
    neptune::core::SampleCache cache_; // thread du chargeur uniquement
};

neptune::core::InstrumentDescription NeptuneProcessor::instrumentDescription() const
{
    const juce::ScopedLock lock (instrumentLock_);
    return instrument_;
}

NeptuneProcessor::InstrumentStatus NeptuneProcessor::instrumentStatus() const
{
    const juce::ScopedLock lock (instrumentLock_);
    return status_;
}

void NeptuneProcessor::setStatus (InstrumentStatus s)
{
    const juce::ScopedLock lock (instrumentLock_);
    status_ = std::move (s);
}

void NeptuneProcessor::setInstrumentDescription (neptune::core::InstrumentDescription d, bool forceReload)
{
    for (auto& z : d.zones) z = neptune::core::sanitised (z);
    {
        const juce::ScopedLock lock (instrumentLock_);
        if (d == instrument_ && ! forceReload) return;
        instrument_ = d;
    }
    ++revision_;
    loader_->request (std::move (d), forceReload);
    notifyStateChanged(); // la description fait partie de l'état du projet
}

void NeptuneProcessor::uiEditInstrument (neptune::core::InstrumentDescription d, std::string name, std::string mergeKey)
{
    for (auto& z : d.zones) z = neptune::core::sanitised (z);
    auto previous = instrumentDescription();
    if (previous == d) return;
    // Commande « par différences » : un geste sur quelques zones d'un gros instrument ne copie pas toute la description.
    undo_.perform (std::make_unique<neptune::core::InstrumentEditCommand> (
        previous, d,
        [this] { return instrumentDescription(); },
        [this] (const neptune::core::InstrumentDescription& v) { setInstrumentDescription (v, false); },
        std::move (name), std::move (mergeKey)));
}

// Chargement d'un état (projet ou autosave) : ne marque pas le projet modifié.
void NeptuneProcessor::adoptInstrumentFromState (const std::optional<neptune::core::InstrumentDescription>& d)
{
    if (! d) return;
    {
        const juce::ScopedLock lock (instrumentLock_);
        if (*d == instrument_) return;
        instrument_ = *d;
    }
    ++revision_;
    loader_->request (*d, false);
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
    juce::ignoreUnused (ok, samplerOk);
    engine_.attachParameters (&params_);

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
}

NeptuneProcessor::~NeptuneProcessor()
{
    autosaveTimer_.stopTimer();
    autosave_->discard(); // fermeture propre : il n'y a rien à récupérer
}

std::string NeptuneProcessor::currentStateText() const
{
    const auto instrument = instrumentDescription();
    return neptune::core::state::save (params_, autosave_->id(), profile_.locked(), &instrument);
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
        adoptInstrumentFromState (result.instrument);
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
    adoptInstrumentFromState (result.instrument);
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
