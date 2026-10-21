#include "NovaPresetUi.h"

using namespace neptune::core;

namespace nova
{
PresetUi::PresetUi (juce::Component& owner, PresetHost& host, const theme::Palette& palette, const i18n::Translator& tr, std::function<void()> reloaded)
    : owner_ (owner), host_ (host), palette_ (palette), tr_ (tr), reloaded_ (std::move (reloaded)) {}

PresetUi::~PresetUi() = default;

juce::String PresetUi::tx (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

juce::String PresetUi::label() const
{
    const auto* p = host_.currentPreset();
    juce::String name = p != nullptr ? juce::String::fromUTF8 (p->name.c_str()) : tx ("preset.none");
    if (host_.isModified()) name += " *";
    if (p != nullptr && p->locked) name = tx ("preset.tag_locked") + "  " + name;
    return name;
}

void PresetUi::quickSave()
{
    if (host_.canSaveCurrent())
    {
        std::string error;
        if (host_.saveCurrent (&error)) { statusText = tx ("preset.saved"); return; }
    }
    openManager (true);   // preset d'usine, verrouillé ou aucun : « Enregistrer sous »
}

void PresetUi::openManager (bool saveAs)
{
    if (manager_ != nullptr) return;
    manager_ = std::make_unique<PresetManager> (host_, palette_, tr_);
    juce::Component::SafePointer<juce::Component> safe (&owner_);
    manager_->onClose = [this, safe] { juce::MessageManager::callAsync ([this, safe] { if (safe != nullptr) { manager_.reset(); owner_.grabKeyboardFocus(); } }); };
    manager_->onChanged = [this] { if (reloaded_) reloaded_(); };
    owner_.addAndMakeVisible (*manager_);
    manager_->setBounds (owner_.getLocalBounds());
    manager_->toFront (true);
    if (saveAs) manager_->showSaveAs();
}

void PresetUi::resized()
{
    if (manager_ != nullptr) manager_->setBounds (owner_.getLocalBounds());
}

void PresetUi::showMenu (juce::Component* anchor, int matrixBank)
{
    juce::PopupMenu menu;
    menu.addItem (1, tx ("preset.manager"));
    menu.addItem (2, tx ("preset.save"));
    menu.addItem (3, tx ("og.save_as"));
    menu.addSeparator();
    menu.addItem (4, tx ("preset.prev"));
    menu.addItem (5, tx ("preset.next"));
    // presets de la banque du preset courant (sinon de la banque affichée dans la matrice)
    const auto& lib = host_.library();
    int bank = matrixBank;
    if (const auto slot = preset::Navigator::locate (lib.banks(), host_.currentPresetId())) bank = slot->bank;
    std::vector<std::string> ids;
    juce::PopupMenu bankMenu;
    for (int prog = 0; prog < preset::programsPerBank; ++prog)
    {
        const auto id = preset::Navigator::presetAt (lib.banks(), { bank, prog });
        const auto* p = id.empty() ? nullptr : lib.find (id);
        if (p == nullptr || ! host_.acceptsPreset (*p)) continue;
        ids.push_back (id);
        bankMenu.addItem (1000 + static_cast<int> (ids.size()) - 1, juce::String (prog + 1) + "  " + juce::String::fromUTF8 (p->name.c_str()), true, id == host_.currentPresetId());
    }
    if (! ids.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader (tx ("og.bank_header") + " " + juce::String::fromUTF8 (preset::displayName (lib.banks().find (bank) != nullptr ? *lib.banks().find (bank) : preset::Bank()).c_str()));
        menu.addSubMenu (tx ("og.bank_presets"), bankMenu);
    }
    std::vector<std::string> all;
    juce::PopupMenu allMenu;
    for (const auto& p : lib.allPresets())
    {
        if (! host_.acceptsPreset (p)) continue;
        all.push_back (p.id);
        allMenu.addItem (5000 + static_cast<int> (all.size()) - 1, juce::String::fromUTF8 (p.name.c_str()), true, p.id == host_.currentPresetId());
    }
    menu.addSubMenu (tx ("og.all_presets"), allMenu);
    juce::Component::SafePointer<juce::Component> safe (&owner_);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor), [this, safe, ids, all] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r == 1) openManager (false);
        else if (r == 2) quickSave();
        else if (r == 3) openManager (true);
        else if (r == 4) host_.stepPreset (-1);
        else if (r == 5) host_.stepPreset (+1);
        else if (r >= 5000 && static_cast<std::size_t> (r - 5000) < all.size()) host_.loadPreset (all[static_cast<std::size_t> (r - 5000)]);
        else if (r >= 1000 && static_cast<std::size_t> (r - 1000) < ids.size()) host_.loadPreset (ids[static_cast<std::size_t> (r - 1000)]);
        if (reloaded_) reloaded_();
    });
}
} // namespace nova
