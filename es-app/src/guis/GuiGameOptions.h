//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameOptions.h
//
//  Context menu for a game, opened by holding the "a" button in the gamelist view.
//

#ifndef ES_APP_GUIS_GUI_GAME_OPTIONS_H
#define ES_APP_GUIS_GUI_GAME_OPTIONS_H

#include "FileData.h"
#include "GuiComponent.h"
#include "SystemData.h"
#include "components/MenuComponent.h"

class GuiGameOptions : public GuiComponent
{
public:
    GuiGameOptions(FileData* game);
    ~GuiGameOptions() override;

    bool input(InputConfig* config, Input input) override;
    std::vector<HelpPrompt> getHelpPrompts() override;
    void close();

private:
    // Adds a selectable entry that runs the given function when accepted.
    void addEntry(const std::string& label, const std::function<void()>& func);
    void addInfoEntry(const std::string& label, const std::string& value);

    void openMetaDataEd();
    void onMetadataSaved(FileData* file, bool reload);
    void toggleFavorite();
    void removeFromCustomCollection();
    void deleteGame();
    bool isFavorite() const;
    bool hasCustomCollections() const;

    MenuComponent mMenu;
    FileData* mGame;
    bool mFavoritesChanged;
};

#endif // ES_APP_GUIS_GUI_GAME_OPTIONS_H
