#include "PluginProcessor.h"
#include "NeptuneParameter.h"
#include "PluginEditor.h"
#include "neptune/core/ProbePool.h"
#include "neptune/core/State.h"

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
    juce::ignoreUnused (ok);

    for (std::size_t i = 0; i < params_.size(); ++i)
        addParameter (new NeptuneParameter (params_, i, hostProbe_)); // l'AudioProcessor en prend la propriété

    // Réglages globaux : lus une fois au démarrage ; le fichier est créé avec les valeurs par défaut s'il manque.
    const auto configDir = configDirectory();
    settingsFile_ = configDir / "settings.json";
    std::vector<std::string> warnings;
    settings_ = neptune::core::settings::loadFile (settingsFile_, &warnings);
    for (const auto& w : warnings)
        DBG ("Neptune settings: " << juce::String (w));
    std::error_code ec;
    if (! std::filesystem::exists (settingsFile_, ec))
        neptune::core::settings::saveFile (settingsFile_, settings_);

    // Autosave : fichiers orphelins trop anciens supprimés, puis écriture périodique de l'état s'il a changé.
    const auto autosaveDir = configDir / "autosave";
    neptune::core::AutosaveStore::purgeStale (autosaveDir, settings_.autosaveKeepDays);
    autosave_ = std::make_unique<neptune::core::AutosaveStore> (autosaveDir);
    autosave_->setBaseline (neptune::core::state::save (params_, autosave_->id()));

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

void NeptuneProcessor::autosaveNow()
{
    std::string error;
    if (! autosave_->write (neptune::core::state::save (params_, autosave_->id()), &error))
        DBG ("Neptune autosave failed: " << juce::String (error));
}

// L'état du projet porte l'identifiant de l'instance de la session précédente : on le reprend pour retrouver
// son autosave. Le fichier laissé par un crash est mis de côté et proposé à l'utilisateur.
void NeptuneProcessor::adoptStateIdentity (const std::string& idFromState)
{
    if (! idFromState.empty())
        autosave_->adoptId (idFromState); // refusé si invalide ou déjà pris : on garde l'identifiant courant

    const std::string loaded = neptune::core::state::save (params_, autosave_->id());
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
        if (msg.isNoteOn())
            engine_.noteOn (msg.getChannel() - 1, msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine_.noteOff (msg.getChannel() - 1, msg.getNoteNumber());
    }

    engine_.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void NeptuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const std::string text = neptune::core::state::save (params_, autosave_->id());
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
