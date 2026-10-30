//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiPlatformSettings.cpp
//
//  See GuiPlatformSettings.h for an overview of the two pages.
//

#include "guis/GuiPlatformSettings.h"

#include "GamelistFileParser.h"
#include "GuiComponent.h"
#include "MenuFontUtil.h"
#include "SystemData.h"
#include "Window.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiAlternativeEmulators.h"
#include "guis/GuiMsgBox.h"
#include "guis/PlatformSettings.h"
#include "renderers/Renderer.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#include <algorithm>

static GuiSettings* buildPlatformPage(SystemData* system, Window* window);
static GuiSettings* buildPlatformInfoPage(SystemData* system);

namespace
{
    // Read only row without an arrow and without an action.
    ComponentListRow makeInfoRow(const std::string& label, const std::string& value)
    {
        ComponentListRow row;
        row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                       0x777777FF),
                       true);
        row.addElement(std::make_shared<TextComponent>(value, menuValueFont(),
                                                       0x888888FF, ALIGN_RIGHT),
                       false);
        return row;
    }

    // Label of the emulator currently selected for the platform. An empty string means the
    // first command entry, i.e. the default emulator.
    std::string configuredEmulatorLabel(SystemData* system)
    {
        const std::string configured {system->getAlternativeEmulator()};

        if (configured == "" || configured.substr(0, 9) == "<INVALID>")
            return "";

        for (const auto& command : system->getSystemEnvData()->mLaunchCommands) {
            if (command.second == configured)
                return SystemData::getAlternativeEmulatorDisplayName(command.second);
        }

        return "";
    }
}

// ---------------------------------------------------------------------------------------------
// Platform list page
// ---------------------------------------------------------------------------------------------

GuiPlatformSettings::GuiPlatformSettings(const std::string& title)
    : GuiSettings {title}
{
    // Horizontal size of the platform name column, the same ratio the alternative emulators
    // page uses for its two columns.
    const float nameSizeX {getMenu().getSize().x / 3.27f};

    std::vector<SystemData*> sortedSystems;
    for (SystemData* system : SystemData::sSystemVector) {
        // Collections are not platforms, they are left out of this page entirely. The manual
        // image viewer is excluded as well, it is a helper system without an emulator and has
        // nothing that could be configured here (devdoc/58 section T5).
        if (!system->isCollection() && system->getName() != "manual")
            sortedSystems.push_back(system);
    }

    std::sort(sortedSystems.begin(), sortedSystems.end(), [](SystemData* a, SystemData* b) {
        return Utils::String::toUpper(a->getFullName()) < Utils::String::toUpper(b->getFullName());
    });

    for (SystemData* system : sortedSystems) {
        ComponentListRow row;

        std::shared_ptr<TextComponent> nameText {
            std::make_shared<TextComponent>(system->getFullNameLocalized(),
                                            getMenuThemeFont(FONT_SIZE_MEDIUM),
                                            0x777777FF, ALIGN_LEFT, ALIGN_CENTER,
                                            glm::ivec2 {0, 0})};
        nameText->setSize(nameSizeX, nameText->getSize().y);
        row.addElement(nameText, true);

        // A hidden platform is marked on the right hand side. That marker is what keeps a
        // hidden platform findable, hidden platforms are therefore always listed here.
        if (PlatformSettings::isHidden(system->getName())) {
            std::shared_ptr<TextComponent> valueText {
                std::make_shared<TextComponent>("[" + _("HIDDEN") + "]", menuValueFont(),
                                                0x888888FF, ALIGN_RIGHT, ALIGN_CENTER,
                                                glm::ivec2 {0, 0})};
            valueText->setSize(getMenu().getSize().x - nameSizeX -
                                   20.0f * Renderer::getScreenHeightModifier(),
                               nameText->getSize().y);
            row.addElement(valueText, false);
        }

        row.makeAcceptInputHandler([this, system] { mWindow->pushGui(buildPlatformPage(system, mWindow)); });

        addRow(row);
    }

    if (sortedSystems.empty()) {
        ComponentListRow row;
        row.addElement(std::make_shared<TextComponent>(
                           ViewController::EXCLAMATION_CHAR + " " + _("NO PLATFORMS FOUND"),
                           getMenuThemeFont(FONT_SIZE_MEDIUM), 0x777777FF, ALIGN_CENTER),
                       true);
        addRow(row);
    }
}

// ---------------------------------------------------------------------------------------------
// Per platform page
// ---------------------------------------------------------------------------------------------

static GuiSettings* buildPlatformInfoPage(SystemData* system)
{
    auto page = new GuiSettings(_("PLATFORM INFO"));

    // Read only information, useful when diagnosing a platform.
    page->addRow(makeInfoRow(_("FULL NAME"), system->getFullNameLocalized()));
    page->addRow(makeInfoRow(_("INTERNAL NAME"), system->getName()));
    page->addRow(makeInfoRow(_("THEME DIRECTORY"), system->getThemeFolder()));

    const std::pair<unsigned int, unsigned int> gameCount {system->getDisplayedGameCount()};
    page->addRow(makeInfoRow(_("GAME COUNT"), std::to_string(gameCount.first)));

    return page;
}

static GuiSettings* buildPlatformPage(SystemData* system, Window* window)
{
    auto page = new GuiSettings(Utils::String::toUpper(system->getFullNameLocalized()));

    const std::string systemName {system->getName()};

    // Default emulator. The value is stored in the platform gamelist, the same place the per
    // game emulator selection writes to, so the two stay in sync. Only offered when the
    // platform actually defines more than one command.
    {
        // Kept as a shared pointer so selecting another emulator can update the shown value
        // without reopening the page.
        auto emulatorValue = std::make_shared<TextComponent>("",
                                                             menuValueFont(),
                                                             0x888888FF, ALIGN_RIGHT);

        auto refreshEmulatorValue = [system, emulatorValue]() {
            const std::string label {configuredEmulatorLabel(system)};
            // An empty label means the first command entry, i.e. the default emulator, which
            // is what the platform ships with.
            if (label.empty())
                emulatorValue->setValue(_("DEFAULT"));
            else
                emulatorValue->setValue(label);
        };

        ComponentListRow row;
        row.addElement(std::make_shared<TextComponent>(_("DEFAULT EMULATOR"),
                                                       getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                       0x777777FF),
                       true);

        if (system->getSystemEnvData()->mLaunchCommands.size() > 1) {
            refreshEmulatorValue();
            row.addElement(emulatorValue, false);
            row.addElement(page->getMenu().makeArrow(), false);
            row.makeAcceptInputHandler([window, system, refreshEmulatorValue] {
                auto* emulatorPage {new GuiAlternativeEmulators(system, refreshEmulatorValue)};
                window->pushGui(emulatorPage);
                // The selector has to be opened after the wrapper is on the gui stack,
                // otherwise it would end up below it.
                emulatorPage->openSelector();
            });
        }
        else {
            // No choice to offer, the row states that instead of pretending there is one.
            emulatorValue->setValue(_("NO ALTERNATIVE EMULATORS"));
            row.addElement(emulatorValue, false);
        }

        page->addRow(row);
    }

    // Display mode. Platforms that are always fullscreen have no such setting and the row is
    // left out entirely. For every other emulator, when windowed mode is not supported the row
    // explains that instead of offering a setting that could not work.
    if (!PlatformSettings::isFixedFullscreen(system)) {
        const PlatformSettings::WindowBackend backend {
            PlatformSettings::windowBackendOfSystem(system)};

        if (backend != PlatformSettings::WindowBackend::UNSUPPORTED) {
            auto displayMode =
                std::make_shared<OptionListComponent<std::string>>(_("DISPLAY MODE"), false);
            const bool windowed {PlatformSettings::isWindowed(systemName)};
            displayMode->add(_("FULLSCREEN"), "0", !windowed);
            displayMode->add(_("WINDOWED"), "1", windowed);
            page->addWithLabel(_("DISPLAY MODE"), displayMode);
            page->addSaveFunc([systemName, displayMode] {
                const bool newWindowed {displayMode->getSelected() == "1"};
                if (newWindowed != PlatformSettings::isWindowed(systemName))
                    PlatformSettings::setWindowed(systemName, newWindowed);
            });
        }
        else {
            page->addRow(makeInfoRow(_("DISPLAY MODE"),
                                     _("NOT SUPPORTED BY THE CURRENT EMULATOR")));
        }
    }

    // Show or hide the platform in the system carousel.
    auto showSwitch = std::make_shared<SwitchComponent>();
    // The component is constructed with the "off" icon, so the state has to be applied
    // explicitly, otherwise the icon and mState disagree (devdoc/35 section 11).
    showSwitch->setState(!PlatformSettings::isHidden(systemName));
    page->addWithLabel(_("SHOW PLATFORM"), showSwitch);
    page->addSaveFunc([window, system, systemName, showSwitch, page] {
        const bool newHidden {!showSwitch->getState()};

        if (newHidden == PlatformSettings::isHidden(systemName))
            return;

        // Never hide the last visible platform, that would leave an empty carousel. The switch
        // is put back so the page matches the configuration again.
        if (newHidden && PlatformSettings::visibleSystemCount() <= 1) {
            showSwitch->setState(true);
            window->pushGui(new GuiMsgBox(_("AT LEAST ONE PLATFORM HAS TO STAY VISIBLE"), _("OK")));
            return;
        }

        PlatformSettings::setHidden(systemName, newHidden);
        // The carousel is rebuilt on close of this page, which is all that hiding needs.
        page->setNeedsReloading();
    });

    // The platform information is read only and kept on its own page, so the settings stay
    // short and the information has room to grow.
    ComponentListRow infoRow;
    infoRow.addElement(std::make_shared<TextComponent>(_("PLATFORM INFO"),
                                                       getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                       0x777777FF),
                       true);
    infoRow.addElement(page->getMenu().makeArrow(), false);
    infoRow.makeAcceptInputHandler(
        [window, system] { window->pushGui(buildPlatformInfoPage(system)); });
    page->addRow(infoRow);

    // Resets all per platform settings at once.
    ComponentListRow resetRow;
    resetRow.addElement(std::make_shared<TextComponent>(_("RESET PLATFORM SETTINGS"),
                                                        getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                        0x777777FF),
                        true);
    resetRow.addElement(page->getMenu().makeArrow(), false);
    resetRow.makeAcceptInputHandler([window, page, system] {
        window->pushGui(new GuiMsgBox(
            _("RESET THE SETTINGS OF THIS PLATFORM?"), _("PROCEED"),
            [window, page, system] {
                // The default emulator lives in the gamelist, the other two values in
                // exia.conf.
                system->setAlternativeEmulator("");
                GamelistFileParser::updateGamelist(system, true);
                PlatformSettings::reset(system->getName());
                // Remove the page from the stack first, because GuiSettings deletes itself
                // on close and cannot be deleted while still tracked.
                window->removeGui(page);
                delete page;
                window->pushGui(buildPlatformPage(system, window));
            },
            _("CANCEL"), nullptr, "", nullptr, "", nullptr, nullptr, false, true));
    });
    page->addRow(resetRow);

    return page;
}
