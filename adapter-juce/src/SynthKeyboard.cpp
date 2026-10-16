#include "SynthKeyboard.h"
#include <cctype>

using namespace neptune::core;
using neptune::core::theme::Token;

// ------------------------------------------------------------------------------------------------ clavier

SynthKeyboard::SynthKeyboard (const theme::Palette& palette) : palette_ (palette) {}

juce::Colour SynthKeyboard::colour (Token t) const
{
    const auto& c = palette_.color (t);
    return juce::Colour (c.r, c.g, c.b, static_cast<float> (c.a));
}

bool SynthKeyboard::isBlack (int key) const noexcept
{
    const int n = key % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

void SynthKeyboard::shiftOctave (int octaves)
{
    const int next = juce::jlimit (24, 96, centreKey_ + 12 * octaves);
    if (next == centreKey_) return;
    releaseAll();
    centreKey_ = next;
    repaint();
}

namespace
{
// Nombre de touches blanches strictement sous chaque note (0..128) : la position d'une touche est son rang x largeur.
const std::array<int, 129>& whitesBelow()
{
    static const std::array<int, 129> table = []
    {
        std::array<int, 129> t {};
        int n = 0;
        for (int k = 0; k < 128; ++k)
        {
            t[static_cast<std::size_t> (k)] = n;
            const int m = k % 12;
            if (! (m == 1 || m == 3 || m == 6 || m == 8 || m == 10)) ++n;
        }
        t[128] = n;
        return t;
    }();
    return table;
}
} // namespace

juce::Rectangle<float> SynthKeyboard::keyRect (int key) const
{
    const float w = whiteKeyWidth;
    const auto& below = whitesBelow();
    // la touche du centre est centrée dans le composant
    const float origin = static_cast<float> (getWidth()) * 0.5f - w * 0.5f - static_cast<float> (below[static_cast<std::size_t> (centreKey_)]) * w;
    const float x = origin + static_cast<float> (below[static_cast<std::size_t> (key)]) * w;
    const float h = static_cast<float> (getHeight());
    if (! isBlack (key)) return { x, 0.0f, w, h };
    return { x - 0.3f * w, 0.0f, 0.6f * w, 0.6f * h };
}

bool SynthKeyboard::visible (int key) const
{
    const auto r = keyRect (key);
    return r.getRight() > 0.0f && r.getX() < static_cast<float> (getWidth());
}

bool SynthKeyboard::heldByMidi (int key) const noexcept
{
    if (key < 0 || key > 127) return false;
    return (key < 64 ? heldLo_ >> key : heldHi_ >> (key - 64)) & 1u;
}

int SynthKeyboard::keyAt (juce::Point<int> p) const
{
    const auto pf = p.toFloat();
    for (int k = 0; k < 128; ++k) // noires d'abord : elles sont au-dessus
        if (isBlack (k) && keyRect (k).contains (pf)) return k;
    for (int k = 0; k < 128; ++k)
        if (! isBlack (k) && keyRect (k).contains (pf)) return k;
    return -1;
}

void SynthKeyboard::paint (juce::Graphics& g)
{
    const auto accent = colour (Token::Accent);
    const auto midiColour = colour (Token::Accent2);
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 128; ++k)
        {
            const bool black = isBlack (k);
            if (black != (pass == 1) || ! visible (k)) continue;
            const auto r = keyRect (k);
            const bool on = down_.count (k) > 0 || latched_.count (k) > 0;
            const bool midi = heldByMidi (k);
            g.setColour (on ? accent : (midi ? midiColour : (black ? juce::Colour (0xff1a1a1a) : juce::Colour (0xfff2f2f2))));
            g.fillRect (r.reduced (0.5f));
            g.setColour (juce::Colour (0xff555555));
            g.drawRect (r, 1.0f);
            if (k % 12 == 0 && ! black) // do : nom de la note (do central = C4 = 60)
            {
                g.setColour (on || midi ? juce::Colour (0xff000000) : juce::Colour (0xff666666));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("C" + juce::String (k / 12 - 1), r.withTrimmedBottom (4.0f).toNearestInt(), juce::Justification::centredBottom);
            }
        }
}

void SynthKeyboard::press (int source, int key, bool latch)
{
    if (key < 0) return;
    if (latch) // maintien : bascule la touche
    {
        if (latched_.count (key) > 0)
        {
            latched_.erase (key);
            if (onNoteOff) onNoteOff (key);
        }
        else
        {
            latched_.insert (key);
            if (onNoteOn) onNoteOn (key, 0.8f);
        }
        sourceKey_.erase (source);
    }
    else
    {
        sourceKey_[source] = key;
        down_.insert (key);
        if (onNoteOn) onNoteOn (key, 0.8f);
    }
    repaint();
}

void SynthKeyboard::release (int source)
{
    const auto it = sourceKey_.find (source);
    if (it == sourceKey_.end()) return;
    const int key = it->second;
    sourceKey_.erase (it);
    // une autre source peut tenir la même touche : on ne la coupe que si plus personne ne la tient
    bool stillDown = false;
    for (const auto& kv : sourceKey_) stillDown = stillDown || kv.second == key;
    if (! stillDown)
    {
        down_.erase (key);
        if (onNoteOff) onNoteOff (key);
    }
    repaint();
}

void SynthKeyboard::mouseDown (const juce::MouseEvent& e)
{
    press (e.source.getIndex(), keyAt (e.getPosition()), hold_ || e.mods.isShiftDown());
}

void SynthKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    const int source = e.source.getIndex();
    const auto it = sourceKey_.find (source);
    if (it == sourceKey_.end()) return; // clic de maintien : pas de glissé
    const int key = keyAt (e.getPosition());
    if (key < 0 || key == it->second) return;
    release (source);
    press (source, key, false);
}

void SynthKeyboard::mouseUp (const juce::MouseEvent& e) { release (e.source.getIndex()); }

void SynthKeyboard::releaseAll()
{
    for (const int k : latched_) if (onNoteOff) onNoteOff (k);
    std::set<int> keys;
    for (const auto& kv : sourceKey_) keys.insert (kv.second);
    for (const int k : keys) if (onNoteOff) onNoteOff (k);
    latched_.clear();
    sourceKey_.clear();
    down_.clear();
    repaint();
}
