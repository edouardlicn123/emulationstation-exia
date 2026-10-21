//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiAlternativeEmulators.h
//
//  User interface to select between alternative emulators per system
//  based on configuration entries in es_systems.xml.
//

#ifndef ES_APP_GUIS_GUI_ALTERNATIVE_EMULATORS_H
#define ES_APP_GUIS_GUI_ALTERNATIVE_EMULATORS_H

#include "GuiComponent.h"
#include "guis/GuiSettings.h"

#include <functional>

template <typename T> class OptionListComponent;

class GuiAlternativeEmulators : public GuiComponent
{
public:
    // Without arguments the page lists every platform that defines alternative emulators.
    // With a systemFilter it instead lists the emulators of that single platform, which is
    // how the platform settings page reuses the selector. onChanged is called after a
    // selection was stored, it lets the caller refresh what it displays.
    explicit GuiAlternativeEmulators(SystemData* systemFilter = nullptr,
                                     std::function<void()> onChanged = nullptr);

    // Opens the selector of the platform passed to the constructor. It has to be called after
    // this component was pushed onto the gui stack, otherwise the selector is pushed first and
    // ends up below this wrapper.
    void openSelector();

private:
    void updateMenu(const std::string& systemName, const std::string& label, bool defaultEmulator);
    void selectorWindow(SystemData* system);

    void update(int deltaTime) override;
    bool input(InputConfig* config, Input input) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

    MenuComponent mMenu;
    bool mHasSystems;

    // Set when the page was opened for a single platform, see the constructor.
    SystemData* mSystemFilter;
    std::function<void()> mOnChanged;

    std::map<std::string, std::shared_ptr<TextComponent>> mCommandRows;
    std::shared_ptr<OptionListComponent<std::string>> mCommandSelection;

    // The selector opened for mSystemFilter, tracked so the wrapper can be closed with it.
    GuiSettings* mSelector {nullptr};
};

#endif // ES_APP_GUIS_GUI_ALTERNATIVE_EMULATORS_H
