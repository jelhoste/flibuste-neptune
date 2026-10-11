#include "PluginProcessor.h"
#include "NeptuneParameter.h"
#include "PluginEditor.h"
#include "neptune/core/ProbePool.h"

#ifndef NEPTUNE_PROBE_TRACKS
 #define NEPTUNE_PROBE_TRACKS 32
#endif

namespace
{
constexpr juce::uint32 kStateMagic = 0x5354504e; // 'NPTS'
constexpr int kStateVersion = 1;
}

NeptuneProcessor::NeptuneProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)),
      probeTracks_ (NEPTUNE_PROBE_TRACKS)
{
    std::string error;
    const bool ok = neptune::core::probe::buildPool (params_, probeTracks_, &error);
    jassert (ok);
    juce::ignoreUnused (ok);

    for (std::size_t i = 0; i < params_.size(); ++i)
        addParameter (new NeptuneParameter (params_, i)); // l'AudioProcessor en prend la propriété
}

void NeptuneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate, samplesPerBlock);
}

void NeptuneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            engine_.noteOn (msg.getChannel() - 1, msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine_.noteOff (msg.getChannel() - 1, msg.getNoteNumber());
    }

    engine_.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void NeptuneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out (dest, false);
    out.writeInt (static_cast<int> (kStateMagic));
    out.writeInt (kStateVersion);
    out.writeInt (static_cast<int> (params_.size()));
    for (std::size_t i = 0; i < params_.size(); ++i)
    {
        out.writeInt (static_cast<int> (params_.spec (i).id));
        out.writeDouble (params_.getAt (i));
    }
}

void NeptuneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream in (data, static_cast<size_t> (sizeInBytes), false);
    if (in.getNumBytesRemaining() < 12 || static_cast<juce::uint32> (in.readInt()) != kStateMagic) return;
    if (in.readInt() != kStateVersion) return; // état d'une autre version : on ne tente rien

    const int count = in.readInt();
    for (int i = 0; i < count && in.getNumBytesRemaining() >= 12; ++i)
    {
        const auto id = static_cast<neptune::core::ParamId> (in.readInt());
        const double value = in.readDouble();
        params_.set (id, value); // id inconnu : ignoré
    }
}

juce::AudioProcessorEditor* NeptuneProcessor::createEditor() { return new NeptuneEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NeptuneProcessor(); }
