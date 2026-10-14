#include "MoussaillonPlugin.h"
#include <nlohmann/json.hpp>
#include "NeptuneParameter.h"
#include "neptune/core/UtilityModule.h"

using namespace neptune::core;

namespace
{
constexpr const char* kFormat = "moussaillon-state";

// Même dossier de configuration que Neptune : le thème, le contraste et la langue sont communs à toute la suite.
std::filesystem::path configDirectory()
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif
    return std::filesystem::u8path (base.getChildFile ("Neptune").getFullPathName().toStdString());
}

theme::Palette makePalette (const settings::GlobalSettings& st)
{
    const auto id = theme::themeFromString (st.theme).value_or (theme::ThemeId::Dark);
    return theme::withContrast (theme::definition (id), st.contrast);
}
} // namespace

juce::AudioProcessor::BusesProperties MoussaillonProcessor::makeBuses()
{
    return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

MoussaillonProcessor::MoussaillonProcessor()
    : juce::AudioProcessor (makeBuses()), runner_ (std::make_unique<UtilityModule>())
{
    jassert (runner_.ok());
    auto& set = runner_.params();
    for (std::size_t i = 0; i < set.size(); ++i)
        addParameter (new NeptuneParameter (set, i, probe_)); // l'AudioProcessor en prend la propriété ; même ordre que le jeu
    std::vector<std::string> warnings;
    settings_ = settings::loadFile (configDirectory() / "settings.json", &warnings);
    for (auto& s : modal_) s = {};
}

bool MoussaillonProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MoussaillonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    runner_.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (runner_.latencySamples());
}

void MoussaillonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;
    runner_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n); // en place ; le bypass est le paramètre du plugin
}

// ---- état : JSON, comme Neptune ({"format", "schema_version", "parameters": {id: valeur réelle}})
void MoussaillonProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    nlohmann::json doc;
    doc["format"] = kFormat;
    doc["schema_version"] = 1;
    doc["module"] = "utility";
    nlohmann::json params = nlohmann::json::object();
    const auto& set = runner_.params();
    for (std::size_t i = 0; i < set.size(); ++i) params[std::to_string (set.spec (i).id)] = set.getAt (i);
    doc["parameters"] = std::move (params);
    const auto text = doc.dump (2);
    dest.append (text.data(), text.size());
}

void MoussaillonProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    const auto doc = nlohmann::json::parse (static_cast<const char*> (data), static_cast<const char*> (data) + size, nullptr, false);
    if (doc.is_discarded() || ! doc.is_object()) return;
    const auto fmt = doc.find ("format");
    const auto params = doc.find ("parameters");
    if (fmt == doc.end() || ! fmt->is_string() || fmt->get<std::string>() != kFormat || params == doc.end() || ! params->is_object()) return;
    auto& set = runner_.params();
    set.resetAll();
    for (const auto& [key, value] : params->items())
    {
        if (! value.is_number() || ! std::isfinite (value.get<double>())) continue;
        const long long id = std::strtoll (key.c_str(), nullptr, 10);
        if (id > 0) set.set (static_cast<ParamId> (id), value.get<double>());
    }
    undo_.clear();
    runner_.reset();
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
}

// ---- gestes d'édition (annulation + automation de l'hôte)
void MoussaillonProcessor::uiBeginEdit (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    getParameters()[static_cast<int> (index)]->beginChangeGesture();
    undo_.beginGesture();
}

void MoussaillonProcessor::uiSetNormalised (ParamId id, double n)
{
    auto& set = runner_.params();
    const auto index = set.indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.setParameter (set, id, set.spec (index).fromNormalized (n));
    getParameters()[static_cast<int> (index)]->setValueNotifyingHost (static_cast<float> (set.getNormalizedAt (index)));
}

void MoussaillonProcessor::uiEndEdit (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.endGesture();
    getParameters()[static_cast<int> (index)]->endChangeGesture();
}

void MoussaillonProcessor::uiResetToDefault (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    const auto& spec = runner_.params().spec (index);
    uiBeginEdit (id);
    uiSetNormalised (id, spec.toNormalized (spec.defaultValue));
    uiEndEdit (id);
}

bool MoussaillonProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return done;
}

bool MoussaillonProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return done;
}

juce::AudioProcessorEditor* MoussaillonProcessor::createEditor() { return new MoussaillonEditor (*this); }

// ---- éditeur
MoussaillonEditor::MoussaillonEditor (MoussaillonProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language)
{
    const auto palette = makePalette (p.settings());
    const auto& def = theme::definition (theme::themeFromString (p.settings().theme).value_or (theme::ThemeId::Dark));
    UtilityEditor::Options opt;
    opt.brand = true;
    opt.bypass = true;
    opt.sampleRate = [&p] { return p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0; };
    content_ = std::make_unique<UtilityEditor> (p, &p.runner().module(), palette, tr_, def.knob, opt);
    addAndMakeVisible (*content_);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (560, 380, 1600, 1100);
    setSize (UtilityEditor::preferredWidth, UtilityEditor::preferredHeight + 40);
}

void MoussaillonEditor::resized() { content_->setBounds (getLocalBounds()); }
void MoussaillonEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

bool MoussaillonEditor::keyPressed (const juce::KeyPress& k) { return content_ != nullptr && content_->keyPressed (k); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoussaillonProcessor(); }
