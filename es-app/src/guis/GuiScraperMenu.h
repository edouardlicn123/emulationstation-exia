//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiScraperMenu.h
//
//  Game media scraper, including settings as well as the scraping start button.
//  Submenu to the GuiMenu main menu.
//  Will call GuiScraperMulti to perform the actual scraping.
//

#ifndef ES_APP_GUIS_GUI_SCRAPER_MENU_H
#define ES_APP_GUIS_GUI_SCRAPER_MENU_H

#include "components/MenuComponent.h"
#include "components/SliderComponent.h"
#include "guis/GuiSettings.h"
#include "scrapers/Scraper.h"
#include "views/ViewController.h"

class FileData;
class SwitchComponent;
class SystemData;

template <typename T> class OptionListComponent;

class GuiScraperMenu : public GuiComponent
{
public:
    GuiScraperMenu(std::string title, FileData* singleGame = nullptr);
    ~GuiScraperMenu();

    bool input(InputConfig* config, Input input) override;

    std::vector<HelpPrompt> getHelpPrompts() override;

private:
    void pressedStart();
    void start();

    void addEntry(const std::string&,
                  unsigned int color,
                  bool add_arrow,
                  const std::function<void()>& func);
    void openAccountOptions();
    void openScreenScraperAccount();
    void openHfsDBAccount();
    void openIGDBAccount();
    void addSubMenuRow(GuiSettings* settings, const std::string& label,
                       const std::function<void()>& func);
    void openContentOptions();
    void openMiximageOptions();
    void openOfflineGenerator(GuiSettings* settings);
    void openOtherOptions();

    std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>> getSearches(
        std::vector<SystemData*> systems, GameFilterFunc selector, bool overwriteMedia);

    // Session-only setting, it is intentionally not persisted to the settings file.
    static inline int sSkipRecentDays {0};

    std::shared_ptr<OptionListComponent<std::string>> mScraper;
    std::shared_ptr<OptionListComponent<GameFilterFunc>> mFilters;
    std::shared_ptr<OptionListComponent<SystemData*>> mSystems;
    std::shared_ptr<SliderComponent> mScraperRetryOnErrorCount;

    Renderer* mRenderer;
    MenuComponent mMenu;

    // Set when the menu is opened for a single game from the game options menu, in which
    // case no systems or filters are shown and only this game is added to the queue.
    FileData* mSingleGame;
};

#endif // ES_APP_GUIS_GUI_SCRAPER_MENU_H
