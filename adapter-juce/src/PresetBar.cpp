#include "PresetBar.h"

PresetBar::PresetBar (PresetHost& host, const neptune::core::theme::Palette&, const neptune::core::i18n::Translator& tr)
    : host_ (host), tr_ (tr)
{
    prev_.setButtonText ("<");
    prev_.setTooltip (text ("preset.prev"));
    prev_.onClick = [this]
    {
        if (host_.stepPreset (-1) && onLoaded) onLoaded();
        refresh();
    };
    next_.setButtonText (">");
    next_.setTooltip (text ("preset.next"));
    next_.onClick = [this]
    {
        if (host_.stepPreset (+1) && onLoaded) onLoaded();
        refresh();
    };
    name_.onClick = [this] { if (onOpenManager) onOpenManager (false); };
    save_.setButtonText (text ("preset.save"));
    save_.onClick = [this] { quickSave(); };
    manager_.setButtonText (text ("preset.manager"));
    manager_.onClick = [this] { if (onOpenManager) onOpenManager (false); };
    for (auto* b : { &prev_, &next_, &name_, &save_, &manager_ }) addAndMakeVisible (b);
    refresh();
}

juce::String PresetBar::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void PresetBar::refresh()
{
    const auto* p = host_.currentPreset();
    const bool modified = host_.isModified();
    juce::String name = p != nullptr ? juce::String::fromUTF8 (p->name.c_str()) : text ("preset.none");
    if (modified) name += " *";
    if (p != nullptr && p->locked) name = text ("preset.tag_locked") + "  " + name;
    name_.setButtonText (name);
    lastModified_ = modified;
    lastId_ = host_.currentPresetId();
    lastRevision_ = host_.presetRevision();
}

bool PresetBar::needsRefresh() const
{
    return lastRevision_ != host_.presetRevision() || lastId_ != host_.currentPresetId() || lastModified_ != host_.isModified();
}

void PresetBar::quickSave()
{
    if (host_.canSaveCurrent())
    {
        std::string error;
        if (host_.saveCurrent (&error))
        {
            if (onStatus) onStatus (text ("preset.saved"));
            refresh();
            return;
        }
    }
    if (onOpenManager) onOpenManager (true); // factory, verrouillé ou aucun : « Enregistrer sous »
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    prev_.setBounds (r.removeFromLeft (40).reduced (2));
    const int rest = r.getWidth();
    const int nameW = juce::jlimit (150, 380, rest - 40 - 90 - 120);
    name_.setBounds (r.removeFromLeft (nameW).reduced (2));
    next_.setBounds (r.removeFromLeft (40).reduced (2));
    save_.setBounds (r.removeFromLeft (90).reduced (2));
    manager_.setBounds (r.removeFromLeft (120).reduced (2));
}
