//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiEmulatorSelector.h
//
//  Selector for the per-game alternative emulator. The selection is stored in the
//  "altemulator" metadata field, which holds the label of one of the system's launch
//  commands. This is the same mechanism and the same metadata as the emulator entry in
//  the metadata editor, so both entry points stay in sync.
//

#ifndef ES_APP_GUIS_GUI_EMULATOR_SELECTOR_H
#define ES_APP_GUIS_GUI_EMULATOR_SELECTOR_H

#include "GuiComponent.h"
#include "SystemData.h"
#include "guis/GuiSettings.h"

#include <utility>

class FileData;

class GuiEmulatorSelector : public GuiComponent
{
public:
    GuiEmulatorSelector(SystemData* system, FileData* game);

    bool input(InputConfig* config, Input input) override;
    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void buildMenu(bool invalidEntry, const std::string& currentValue);

    GuiSettings* mMenu;
    SystemData* mSystem;
    FileData* mGame;
};

#endif // ES_APP_GUIS_GUI_EMULATOR_SELECTOR_H
