//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiEmulatorSelector.cpp
//
//  Selector for the per-game alternative emulator. Ported from the MD_ALT_EMULATOR
//  handling in GuiMetaDataEd so that both entry points behave identically.
//

#include "guis/GuiEmulatorSelector.h"

#include "FileData.h"
#include "MenuFontUtil.h"
#include "SystemData.h"
#include "components/TextComponent.h"
#include "views/ViewController.h"

GuiEmulatorSelector::GuiEmulatorSelector(SystemData* system, FileData* game)
    : mMenu {_("SELECT EMULATOR")}
    , mSystem {system}
    , mGame {game}
{
    addChild(&mMenu);

    // The menu is only a child of this component and is not pushed onto the window GUI stack
    // itself, so its BACK button has to delete this component. A GuiSettings instance would
    // instead delete itself when the "b" button is pressed, which leaves this component behind
    // on the GUI stack with a dangling menu pointer, and the next input event then crashes in
    // the virtual call in input() below.
    mMenu.addButton(_("BACK"), _("back"), [this] { delete this; });

    const std::string currentValue {game->metadata.get("altemulator")};

    // A stored label that no longer resolves to a launch command is reported rather than
    // silently ignored, so that it can be cleared.
    const bool invalidEntry {!currentValue.empty() &&
                            system->getLaunchCommandFromLabel(currentValue).empty()};

    if (invalidEntry)
        LOG(LogWarning) << "GuiEmulatorSelector: Invalid alternative emulator \"" << currentValue
                        << "\" configured for game \"" << game->getName() << "\"";

    buildMenu(invalidEntry, currentValue);

    // Take the size of the menu so that this component is centered on screen like a top level
    // GUI, which is also what the scale-up menu opening effect uses.
    setSize(mMenu.getSize());
    setPosition((Renderer::getScreenWidth() - mSize.x) / 2.0f, Renderer::getScreenHeight() * 0.13f);
}

void GuiEmulatorSelector::buildMenu(bool invalidEntry, const std::string& currentValue)
{
    const std::vector<std::pair<std::string, std::string>>& launchCommands {
        mSystem->getSystemEnvData()->mLaunchCommands};

    bool hasAlternative {false};
    for (const auto& cmd : launchCommands) {
        if (!cmd.second.empty()) {
            hasAlternative = true;
            break;
        }
    }
    const bool singleEntry {launchCommands.size() == 1 && hasAlternative == false ? true : (launchCommands.size() == 1)};

    // Only offer a "clear" row when there is a per-game override to clear.
    if (!currentValue.empty()) {
        ComponentListRow clearRow;

        clearRow.addElement(std::make_shared<TextComponent>(
                                ViewController::CROSSEDCIRCLE_CHAR + std::string {" "} +
                                    _("CLEAR ENTRY"),
                                getMenuThemeFont(FONT_SIZE_MEDIUM), mMenuColorPrimary),
                            true);
        clearRow.makeAcceptInputHandler([this] {
            mGame->metadata.set("altemulator", "");
            mSystem->writeMetaData();
            ViewController::getInstance()->onFileChanged(mGame, false);
            delete this;
        });

        mMenu.addRow(clearRow, true);
    }

    for (const auto& entry : launchCommands) {
        // An empty label marks the default command, which is not a selectable alternative.
        if (entry.second.empty())
            continue;

        // When the stored value is invalid and the system only has one command, show only
        // the invalid entry so that the user can act on it.
        if (invalidEntry && singleEntry && entry.second != currentValue && launchCommands.size() <= 1)
            continue;

        std::string label {entry.second};

        // Annotate the command that the system-wide setting currently resolves to.
        if (mSystem->getAlternativeEmulator() == label ||
            (mSystem->getAlternativeEmulator().empty() && !launchCommands.front().second.empty() &&
             launchCommands.front().second == label)) {
            label += " [";
            label += _("SYSTEM-WIDE");
            label += "]";
        }

        auto labelText {
            std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                            mMenuColorPrimary)};
        labelText->setSelectable(true);

        ComponentListRow row;
        row.addElement(labelText, true);
        row.makeAcceptInputHandler([this, labelTextValue {entry.second}, currentValue] {
            if (labelTextValue != currentValue) {
                mGame->metadata.set("altemulator", labelTextValue);
                mSystem->writeMetaData();
                ViewController::getInstance()->onFileChanged(mGame, false);
            }
            delete this;
        });

        mMenu.addRow(row, true);
    }
}

bool GuiEmulatorSelector::input(InputConfig* config, Input input)
{
    // Handle the back button before forwarding to the menu, because deleting this component
    // destroys the menu together with it. Note that this has to be checked for the button
    // release as well, since the menu only acts on the press.
    if (input.value != 0 && (config->isMappedTo("b", input) || config->isMappedTo("back", input))) {
        delete this;
        return true;
    }

    return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiEmulatorSelector::getHelpPrompts()
{
    auto prompts {mMenu.getHelpPrompts()};
    prompts.push_back(HelpPrompt("b", _("back")));
    return prompts;
}
