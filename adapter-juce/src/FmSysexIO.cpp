#include "FmSysexIO.h"

#include "neptune/core/FmPreset.h"
#include "neptune/core/FmVoices.h"

using namespace neptune::core;

FmSysexIO::FmSysexIO (juce::Component& owner, PresetHost& host, const i18n::Translator& tr)
    : owner_ (owner), host_ (host), tr_ (tr), safeOwner_ (&owner)
{
}

juce::String FmSysexIO::text (const char* key) const { return juce::String::fromUTF8 (std::string (tr_.tr (key)).c_str()); }

void FmSysexIO::notify (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::InfoIcon, title, message, text ("modal.close"), &owner_),
                                  nullptr);
}

void FmSysexIO::importFile (std::function<void (const fm::Patch&)> applySingle)
{
    chooser_ = std::make_unique<juce::FileChooser> (text ("fm.import"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.syx");
    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [this, applySingle] (const juce::FileChooser& fc)
                           {
                               if (safeOwner_ == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return;
                               juce::MemoryBlock data;
                               if (! file.loadFileAsData (data) || data.getSize() == 0 || data.getSize() > 4u * 1024u * 1024u)
                               {
                                   notify (text ("fm.import"), text ("fm.import_failed"));
                                   return;
                               }
                               const auto r = fm::parseSysex (static_cast<const std::uint8_t*> (data.getData()), data.getSize());
                               if (r.patches.empty())
                               {
                                   notify (text ("fm.import"), text ("fm.import_failed"));
                                   return;
                               }
                               if (r.patches.size() == 1)
                               {
                                   if (applySingle) applySingle (r.patches.front());
                                   if (! r.checksumOk) notify (text ("fm.import"), text ("fm.checksum_warning"));
                               }
                               else importBank (r.patches, file.getFileNameWithoutExtension());
                           });
}

void FmSysexIO::importBank (const std::vector<fm::Patch>& patches, const juce::String& name)
{
    auto& lib = host_.library();
    int bankIndex = -1;
    for (int b = 0; b < preset::numBanks && bankIndex < 0; ++b)
    {
        const auto* bank = lib.banks().find (b);
        bool empty = true;
        if (bank != nullptr)
            for (const auto& id : bank->presets) empty = empty && id.empty();
        if (empty) bankIndex = b;
    }
    if (bankIndex < 0) { notify (text ("fm.import"), text ("fm.no_free_bank")); return; }
    int n = 0;
    for (std::size_t i = 0; i < patches.size() && i < static_cast<std::size_t> (preset::programsPerBank); ++i)
    {
        auto p = fm::presetFromPatch (patches[i], "DX7");
        preset::Preset stored;
        if (lib.importPreset (std::move (p), &stored) && lib.assign (bankIndex, static_cast<int> (i), stored.id)) ++n;
    }
    preset::Bank bank = *lib.banks().find (bankIndex);
    bank.name = name.toStdString().substr (0, 40);
    lib.setBank (bank);
    lib.saveBanks();
    host_.touchPresets();
    notify (text ("fm.import"), text ("fm.imported_bank") + " " + juce::String (n) + " -> " + juce::String::fromUTF8 (preset::displayName (bank).c_str()));
}

void FmSysexIO::exportVoice (const fm::Patch& patch)
{
    chooser_ = std::make_unique<juce::FileChooser> (text ("fm.export_voice"),
                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                        .getChildFile (juce::String (patch.name()).trim() + ".syx"),
                                                    "*.syx");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                           [this, patch] (const juce::FileChooser& fc)
                           {
                               if (safeOwner_ == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return;
                               const auto bytes = fm::makeSingleVoiceSysex (patch);
                               if (! file.replaceWithData (bytes.data(), bytes.size())) notify (text ("fm.export_voice"), text ("fm.export_failed"));
                           });
}

void FmSysexIO::exportBank()
{
    auto& lib = host_.library();
    int bankIndex = 0;
    if (const auto slot = preset::Navigator::locate (lib.banks(), host_.currentPresetId())) bankIndex = slot->bank;
    std::vector<fm::Patch> patches;
    if (const auto* bank = lib.banks().find (bankIndex))
        for (const auto& id : bank->presets)
        {
            const auto* p = id.empty() ? nullptr : lib.find (id);
            if (p == nullptr || p->module != fm::moduleId || p->state.empty()) continue;
            patches.push_back (fm::fmFromJson (p->state).patch);
            if (patches.size() == 32) break;
        }
    if (patches.empty()) { notify (text ("fm.export_bank"), text ("fm.no_fm_in_bank")); return; }
    const auto bytes = fm::makeBulkSysex (patches);
    const auto defaultName = juce::String::fromUTF8 (preset::displayName (*lib.banks().find (bankIndex)).c_str()).trim() + ".syx";
    chooser_ = std::make_unique<juce::FileChooser> (text ("fm.export_bank"),
                                                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (defaultName), "*.syx");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                           [this, bytes] (const juce::FileChooser& fc)
                           {
                               if (safeOwner_ == nullptr) return;
                               const auto file = fc.getResult();
                               if (file == juce::File()) return;
                               if (! file.replaceWithData (bytes.data(), bytes.size())) notify (text ("fm.export_bank"), text ("fm.export_failed"));
                           });
}
