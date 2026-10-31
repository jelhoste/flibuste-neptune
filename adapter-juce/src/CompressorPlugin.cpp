#include "CompressorPlugin.h"
#include <nlohmann/json.hpp>
#include "NeptuneParameter.h"
#include "neptune/core/CompressorModule.h"

using namespace neptune::core;

namespace
{
constexpr const char* kFormat = "compresseur-state";

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

juce::AudioProcessor::BusesProperties CompressorProcessor::makeBuses()
{
    return BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                            .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)   // clé externe (sidechain de l'hôte)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

CompressorProcessor::CompressorProcessor()
    : juce::AudioProcessor (makeBuses()), runner_ (std::make_unique<CompressorModule>())
{
    jassert (runner_.ok());
    auto& set = runner_.params();
    // Les réglages d'affichage (cachés) ne sont pas déclarés à l'hôte ; ils restent dans l'état du plugin (voir getStateInformation).
    hostIndex_.assign (set.size(), -1);
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        if (set.spec (i).hidden) continue;
        hostIndex_[i] = getParameters().size();
        addParameter (new NeptuneParameter (set, i, probe_)); // l'AudioProcessor en prend la propriété
    }
    std::vector<std::string> warnings;
    settings_ = settings::loadFile (configDirectory() / "settings.json", &warnings);
    for (auto& s : modal_) s = {};
}

bool CompressorProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto sc = l.getChannelSet (true, 1);
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (sc.isDisabled() || sc == juce::AudioChannelSet::stereo());
}

void CompressorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    runner_.prepare (sampleRate, samplesPerBlock);
    declaredLatency_ = runner_.latencySamples();
    setLatencySamples (declaredLatency_);
    startTimerHz (5);
}

// La latence dépend du lookahead et du suréchantillonnage (réglages non automatisables) : on la redéclare à l'hôte quand elle change.
void CompressorProcessor::timerCallback()
{
    const int latency = runner_.latencySamples();
    if (latency == declaredLatency_) return;
    declaredLatency_ = latency;
    setLatencySamples (latency);
}

void CompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;
    // clé externe : le bus sidechain (canaux 2 et 3 du tampon), s'il est branché
    const float* keyL = nullptr;
    const float* keyR = nullptr;
    if (const auto* sc = getBus (true, 1); sc != nullptr && sc->isEnabled() && sc->getNumberOfChannels() >= 1 && buffer.getNumChannels() >= 3)
    {
        keyL = buffer.getReadPointer (2);
        keyR = buffer.getReadPointer (sc->getNumberOfChannels() >= 2 && buffer.getNumChannels() >= 4 ? 3 : 2);
    }
    runner_.setKey (keyL, keyR);
    // source MIDI : les messages du bloc pilotent la clé synthétique du module (sans effet tant que la source n'est pas « MIDI »)
    auto& midiKey = static_cast<CompressorModule&> (runner_.module()).midiKey();
    midiKey.beginBlock();
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const auto* raw = msg.getRawData();
        if (msg.getRawDataSize() >= 2 && raw[0] >= 0x80 && raw[0] < 0xF0)
            midiKey.push (meta.samplePosition, raw[0], raw[1], msg.getRawDataSize() >= 3 ? raw[2] : std::uint8_t (0));
    }
    runner_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n); // en place ; le bypass est le paramètre du plugin
}

// ---- état : JSON, comme Neptune ({"format", "schema_version", "parameters": {id: valeur réelle}})
void CompressorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    nlohmann::json doc;
    doc["format"] = kFormat;
    doc["schema_version"] = 1;
    doc["module"] = "compressor";
    nlohmann::json params = nlohmann::json::object();
    const auto& set = runner_.params();
    for (std::size_t i = 0; i < set.size(); ++i) params[std::to_string (set.spec (i).id)] = set.getAt (i);
    doc["parameters"] = std::move (params);
    const auto text = doc.dump (2);
    dest.append (text.data(), text.size());
}

void CompressorProcessor::setStateInformation (const void* data, int size)
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
void CompressorProcessor::uiBeginEdit (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    if (hostIndex_[index] >= 0) getParameters()[hostIndex_[index]]->beginChangeGesture();
    undo_.beginGesture();
}

void CompressorProcessor::uiSetNormalised (ParamId id, double n)
{
    auto& set = runner_.params();
    const auto index = set.indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.setParameter (set, id, set.spec (index).fromNormalized (n));
    if (hostIndex_[index] >= 0) getParameters()[hostIndex_[index]]->setValueNotifyingHost (static_cast<float> (set.getNormalizedAt (index)));
}

void CompressorProcessor::uiEndEdit (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    undo_.endGesture();
    if (hostIndex_[index] >= 0) getParameters()[hostIndex_[index]]->endChangeGesture();
}

void CompressorProcessor::uiResetToDefault (ParamId id)
{
    const auto index = runner_.params().indexOf (id);
    if (index == ParameterSet::npos) return;
    const auto& spec = runner_.params().spec (index);
    uiBeginEdit (id);
    uiSetNormalised (id, spec.toNormalized (spec.defaultValue));
    uiEndEdit (id);
}

bool CompressorProcessor::uiUndo()
{
    const bool done = undo_.undo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return done;
}

bool CompressorProcessor::uiRedo()
{
    const bool done = undo_.redo();
    if (done) updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    return done;
}

void CompressorProcessor::applySettings (const settings::GlobalSettings& s)
{
    settings_ = s;
    std::string error;
    settings::saveFile (configDirectory() / "settings.json", s, &error);
}

juce::AudioProcessorEditor* CompressorProcessor::createEditor() { return new CompressorPluginEditor (*this); }

// ---- éditeur
CompressorPluginEditor::CompressorPluginEditor (CompressorProcessor& p)
    : juce::AudioProcessorEditor (p), processor_ (p), tr_ (p.settings().language)
{
    buildContent();
    setWantsKeyboardFocus (true);
    // proportions de la maquette conservées ; l'interface se redessine à la vraie taille (aucune transformation)
    nova::configureStretchConstrainer (constrainer_, nova::EditorBase::designWidth, nova::EditorBase::designHeight, nova::EditorBase::minScale, nova::EditorBase::maxScale, 4096);
    setConstrainer (&constrainer_);
    setResizable (true, true);
    setSize (nova::EditorBase::designWidth, nova::EditorBase::designHeight);
}

void CompressorPluginEditor::buildContent()
{
    auto& p = processor_;
    tr_ = i18n::Translator (p.settings().language);
    const auto palette = makePalette (p.settings());
    const auto& def = theme::definition (theme::themeFromString (p.settings().theme).value_or (theme::ThemeId::Dark));
    NovaCompressorEditor::Options opt;
    opt.brand = true;
    opt.midiSource = true;
    opt.moduleName = "compressor";
    opt.stretchAuto = true;   // fenêtre étirable en largeur
    opt.sampleRate = [&p] { return p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0; };
    opt.getSettings = [&p] { return p.settings(); };
    // appliqué hors de l'éditeur qui l'appelle : l'interface est détruite puis recréée avec la nouvelle palette
    opt.applySettings = [&p, safe = juce::Component::SafePointer<CompressorPluginEditor> (this)] (const settings::GlobalSettings& s)
    {
        p.applySettings (s);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->buildContent(); safe->resized(); } });
    };
    content_.reset();
    content_ = std::make_unique<NovaCompressorEditor> (p, static_cast<const CompressorModule*> (&p.runner().module()), palette, def.knob, tr_, opt);
    addAndMakeVisible (*content_);
    content_->setBounds (getLocalBounds());
}

void CompressorPluginEditor::resized() { content_->setBounds (getLocalBounds()); }
void CompressorPluginEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

bool CompressorPluginEditor::keyPressed (const juce::KeyPress& k) { return content_ != nullptr && content_->keyPressed (k); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CompressorProcessor(); }
