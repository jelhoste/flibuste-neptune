#include "AudioInView.h"

#include "neptune/core/ProbePool.h"

using namespace neptune::core;
using neptune::core::theme::Token;

juce::Colour AudioInView::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

AudioInView::AudioInView (NeptuneProcessor& p, const theme::Palette& palette, const i18n::Translator& tr) : processor_ (p), palette_ (palette), tr_ (tr)
{
    hostInput_.setButtonText (text ("audioin.host_input"));
    hostInput_.setColour (juce::ToggleButton::textColourId, colour (Token::Text));
    hostInput_.onClick = [this]
    {
        if (updating_) return;
        auto s = processor_.trackSettings (track_);
        s.hostInput = hostInput_.getToggleState();
        processor_.uiEditTrack (track_, std::move (s), "Audio input from host", "hostinput");
    };
    addAndMakeVisible (hostInput_);
    hint_.setText (text ("audioin.hint"), juce::dontSendNotification);
    hint_.setColour (juce::Label::textColourId, colour (Token::TextDim));
    hint_.setFont (juce::FontOptions (12.0f));
    hint_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint_);
    sources_.setColour (juce::Label::textColourId, colour (Token::Text));
    sources_.setFont (juce::FontOptions (13.0f));
    sources_.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (sources_);
}

void AudioInView::refresh()
{
    updating_ = true;
    hostInput_.setToggleState (processor_.trackSettings (track_).hostInput, juce::dontSendNotification);
    updating_ = false;

    juce::String list;
    const int destination = mixer::firstTrackDestination + track_;
    auto addSends = [&] (const mixer::StripSettings& s, const juce::String& who, auto levelParam)
    {
        for (int i = 0; i < mixer::numSends; ++i)
        {
            const auto& send = s.sends[static_cast<std::size_t> (i)];
            if (send.destination != destination) continue;
            const auto level = processor_.parameters().toText (levelParam (i));
            list += who + "  -  " + text ("audioin.send") + " " + juce::String (i + 1) + " : " + juce::String::fromUTF8 (level.c_str())
                  + (send.mute ? "  (" + text ("audioin.muted") + ")" : juce::String()) + "\n";
        }
    };
    for (int t = 0; t < mixer::numTracks; ++t)
        if (t != track_ && processor_.trackActive (t))
            addSends (processor_.trackSettings (t), text ("audioin.track") + " " + juce::String (t + 1),
                      [t] (int i) { return probe::trackParam (t, probe::kSendLevel + static_cast<ParamId> (i)); });
    for (int q = 0; q < mixer::numParallels; ++q)
        if (processor_.parallelActive (q))
            addSends (processor_.parallelSettings (q).strip, text ("audioin.parallel") + " " + juce::String (q + 1),
                      [q] (int i) { return mixer::parallelParam (q, mixer::kParSendLevel + static_cast<ParamId> (i)); });
    if (list.isEmpty()) list = text ("audioin.no_sends");
    if (list != shownSources_)
    {
        shownSources_ = list;
        sources_.setText (list, juce::dontSendNotification);
    }
}

void AudioInView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (28);
    hostInput_.setBounds (r.removeFromTop (28));
    hint_.setBounds (r.removeFromTop (44));
    r.removeFromTop (28);
    sources_.setBounds (r);
}

void AudioInView::paint (juce::Graphics& g)
{
    g.fillAll (colour (Token::BgCard));
    g.setColour (colour (Token::Accent));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (text ("audioin.title"), 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
    g.setColour (colour (Token::TextDim));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (text ("audioin.sources"), 16, 12 + 28 + 28 + 44, getWidth() - 32, 20, juce::Justification::centredLeft);
}
