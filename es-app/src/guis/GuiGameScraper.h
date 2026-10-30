//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameScraper.h
//
//  Single-game scraping interface. Shows the search results of all enabled
//  scraping sources and lets the user accept one of them. The accepted
//  metadata and media are then written to the gamelist.
//

#ifndef ES_APP_GUIS_GUI_GAME_SCRAPER_H
#define ES_APP_GUIS_GUI_GAME_SCRAPER_H

#include "GuiComponent.h"
#include "components/BackgroundComponent.h"
#include "components/ComponentGrid.h"
#include "scrapers/Scraper.h"

#include <functional>

class FileData;
class ScraperSearchComponent;
class TextComponent;

class GuiGameScraper : public GuiComponent
{
public:
    GuiGameScraper(Window* window, FileData* game, std::function<void()> onDone = nullptr);
    ~GuiGameScraper();

    bool input(InputConfig* config, Input input) override;
    void update(int deltaTime) override;
    void onSizeChanged() override;

    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void acceptResult();
    void resolveAssets();
    void finishAccept();
    void openInputScreen();
    void finish();

    Renderer* mRenderer;
    BackgroundComponent mBackground;
    ComponentGrid mGrid;

    std::shared_ptr<TextComponent> mTitle;
    std::shared_ptr<TextComponent> mSystemName;
    std::shared_ptr<ScraperSearchComponent> mSearchComp;

    FileData* mGame;
    std::function<void()> mOnDone;
    ScraperSearchResult mAcceptedResult;
    std::string mAcceptedSourceId;

    std::unique_ptr<ScraperSearchHandle> mMediaURLsHandle;
    std::unique_ptr<MDResolveHandle> mResolveHandle;

    bool mAccepting;
};

#endif // ES_APP_GUIS_GUI_GAME_SCRAPER_H
