//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameOptions.cpp
//
//  Context menu for a game, opened by holding the "a" button in the gamelist view.
//
//  The individual editors are covered by their own classes, this class only assembles the
//  entries and wires up the callbacks.
//

#include "guis/GuiGameOptions.h"

#include "MenuFontUtil.h"

#include "CollectionSystemsManager.h"
#include "components/SwitchComponent.h"
#include "GuiMetaDataEd.h"
#include "Sound.h"
#include "UIModeController.h"
#include "guis/GuiEmulatorSelector.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperMenu.h"
#include "scrapers/Scraper.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/GamelistView.h"
#include "views/ViewController.h"

namespace
{
    // Exia local addition: the title of this menu is one third smaller than the title of the
    // other menus. The theme supplies a relative font size (theme.xml "menutitle"), so the
    // size cannot be lowered through the default size argument the way it is done for the
    // menu rows. A copy of the theme's title element with a scaled size is resolved through
    // the normal ThemeData entry point instead, which keeps the theme supplied font path,
    // its fallback handling and the user font scale exactly as they are for other menus.
    // The title height, the scroll indicators and the grid layout of MenuComponent are all
    // derived from the font metrics during construction, so nothing else needs adjusting.
    std::shared_ptr<Font> getReducedTitleFont()
    {
        const auto& menuTheme {ThemeData::getMenuTheme()};

        if (menuTheme == nullptr || !menuTheme->loadedFromTheme ||
            !menuTheme->Title.hasFont())
            return getMenuThemeFont(FONT_SIZE_LARGE * (2.0f / 3.0f));

        MenuTextElement title {menuTheme->Title};
        title.fontSize *= 2.0f / 3.0f;

        return menuTheme->getFont(title, FONT_SIZE_LARGE * (2.0f / 3.0f));
    }
} // namespace

GuiGameOptions::GuiGameOptions(FileData* game)
    : mMenu {Utils::String::toUpper(game->getName()), getReducedTitleFont()}
    , mGame {game}
    , mFavoritesChanged {false}
{
    addChild(&mMenu);

    addEntry(_("LAUNCH"), [this] {
        FileData* game {mGame};
        close();
        ViewController::getInstance()->triggerGameLaunch(game);
    });

    addEntry(_("VIEW GAME MEDIA"), [this] {
        FileData* game {mGame};
        // close() destroys "this", so the window has to be used before the deletion.
        mWindow->startMediaViewer(game, false);
        close();
    });

    addEntry(_("SCRAPE"), [this] {
        mWindow->pushGui(new GuiScraperMenu(_("SCRAPE")));
        close();
    });

    // Offer the per-game emulator choice if the system has alternative launch commands.
    {
        const auto& commands {mGame->getSystem()->getSystemEnvData()->mLaunchCommands};
        bool hasAlternative {false};
        for (const auto& cmd : commands) {
            if (!cmd.second.empty()) {
                hasAlternative = true;
                break;
            }
        }
        if (commands.size() > 1 || hasAlternative) {
            addEntry(_("SELECT EMULATOR"), [this] {
                SystemData* system {mGame->getSystem()};
                FileData* game {mGame};
                // close() destroys "this", so the window has to be used before the deletion.
                mWindow->pushGui(new GuiEmulatorSelector(system, game));
                close();
            });
        }
    }

    addEntry(_("EDIT THIS GAME'S METADATA"), [this] {
        openMetaDataEd();
        close();
    });

    addHiddenSwitch();

    addHiddenSwitch();

    if (mGame->getSystem()->isGameSystem()) {
        addEntry(isFavorite() ? _("REMOVE FROM FAVORITES") : _("ADD TO FAVORITES"), [this] {
            toggleFavorite();
            close();
        });
    }

    if (hasCustomCollections() && mGame->getSystem()->isGameSystem() &&
        mGame->metadata.get("collection") != "") {
        addEntry(_("REMOVE FROM CUSTOM COLLECTION"), [this] {
            removeFromCustomCollection();
            close();
        });
    }

    if (!CollectionSystemsManager::getInstance()->isEditing()) {
        addEntry(_("DELETE GAME"), [this] {
            FileData* game {mGame};

            mWindow->pushGui(new GuiMsgBox(
                Utils::String::format(_("THIS WILL DELETE '%s'!\nARE YOU SURE?"),
                                      Utils::String::toUpper(game->getName()).c_str()),
                _("YES"),
                [this] {
                    deleteGame();
                    close();
                },
                _("NO"), nullptr));
        });
    }

    mMenu.addButton(_("BACK"), _("back"), [this] { close(); });

    setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
    mMenu.setPosition((mSize.x - mMenu.getSize().x) / 2.0f,
                      (mSize.y - mMenu.getSize().y) / 2.0f);
}

GuiGameOptions::~GuiGameOptions()
{
    if (mFavoritesChanged)
        ViewController::getInstance()->reloadAll();
}

void GuiGameOptions::addEntry(const std::string& label, const std::function<void()>& func)
{
    ComponentListRow row;

    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary, ALIGN_LEFT, ALIGN_CENTER,
                                                   glm::ivec2 {0, 0}),
                   true);
    row.addElement(mMenu.makeArrow(), false);
    row.makeAcceptInputHandler(func);

    mMenu.addRow(row, false, false);
}

void GuiGameOptions::addInfoEntry(const std::string& label, const std::string& value)
{
    ComponentListRow row;

    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary, ALIGN_LEFT, ALIGN_CENTER,
                                                   glm::ivec2 {0, 0}),
                   true);
    row.addElement(std::make_shared<TextComponent>(value, getMenuThemeSmallFont(FONT_SIZE_SMALL),
                                                   mMenuColorSecondary, ALIGN_RIGHT,
                                                   ALIGN_CENTER),
                   false);

    mMenu.addRow(row, false, false);
}

void GuiGameOptions::addHiddenSwitch()
{
    // Hide or show this game. The compensation (write to the store, unlink from the indexes,
    // refresh the collections) is shared with the metadata editor so both entry points behave
    // identically, see devdoc/38 section 8.4. This switch is the only hide entry point in the
    // gamelist, it replaced the former text row of the SELECT menu.
    if (!UIModeController::getInstance()->isUIModeFull() || mGame->isPlaceHolder() ||
        (mGame->getSystem()->isCollection() && mGame->getType() == FOLDER))
        return;

    auto hiddenSwitch = std::make_shared<SwitchComponent>();
    hiddenSwitch->setState(mGame->getHidden());

    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(_("HIDDEN"), getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary, ALIGN_LEFT, ALIGN_CENTER,
                                                   glm::ivec2 {0, 0}),
                   true);
    row.addElement(hiddenSwitch, false);
    row.makeAcceptInputHandler([this, hiddenSwitch] {
        // The list gives the input to this handler before the switch component sees it, so the
        // new state has to be derived here instead of reading the state of the component. The
        // component is updated as well so the menu reflects the change if it stays open.
        const bool hide {!hiddenSwitch->getState()};
        hiddenSwitch->setState(hide);
        mGame->setHidden(hide);
        close();
    });

    mMenu.addRow(row, false, false);
}

bool GuiGameOptions::input(InputConfig* config, Input input)
{
    if (input.value != 0 && (config->isMappedTo("b", input) || config->isMappedTo("back", input))) {
        close();
        return true;
    }

    return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiGameOptions::getHelpPrompts()
{
    auto prompts {mMenu.getHelpPrompts()};
    prompts.push_back(HelpPrompt("b", _("back")));
    return prompts;
}

void GuiGameOptions::close()
{
    delete this;
}

void GuiGameOptions::openMetaDataEd()
{
    FileData* game {mGame};

    ScraperSearchParams params;
    params.system = game->getSystem();
    params.game = game;

    std::function<void()> clearGameFunc;
    std::function<void()> deleteGameFunc;

    deleteGameFunc = [this] {
        deleteGame();
        close();
    };

    mWindow->pushGui(new GuiMetaDataEd(&game->metadata, game->metadata.getMDD(GAME_METADATA),
                                       params,
                                       std::bind(&GuiGameOptions::onMetadataSaved, this, game, true),
                                       clearGameFunc, deleteGameFunc));
}

void GuiGameOptions::onMetadataSaved(FileData* file, bool reload)
{
    ViewController::getInstance()->getGamelistView(file->getSystem())->onFileChanged(file, reload);
}

bool GuiGameOptions::isFavorite() const
{
    return mGame->metadata.get("favorite") == "true";
}

void GuiGameOptions::toggleFavorite()
{
    FileData* game {mGame};

    if (isFavorite()) {
        game->metadata.set("favorite", "false");
        mWindow->queueInfoPopup(_("REMOVED GAME FROM FAVORITES"), 4000);
    }
    else {
        game->metadata.set("favorite", "true");
        mWindow->queueInfoPopup(_("ADDED GAME TO FAVORITES"), 4000);
    }

    game->getSystem()->writeMetaData();
    mFavoritesChanged = true;
}

bool GuiGameOptions::hasCustomCollections() const
{
    return CollectionSystemsManager::getInstance()->getCustomCollectionSystems().size() > 0;
}

void GuiGameOptions::removeFromCustomCollection()
{
    FileData* game {mGame};

    CollectionSystemsManager::getInstance()->refreshCollectionSystems(game, true);

    game->metadata.set("collection", "");
    game->getSystem()->writeMetaData();

    mWindow->queueInfoPopup(_("REMOVED GAME FROM CUSTOM COLLECTION"), 4000);
}

void GuiGameOptions::deleteGame()
{
    FileData* game {mGame};

    Utils::FileSystem::removeFile(game->getPath());

    ViewController::getInstance()->getGamelistView(game->getSystem())->remove(game, true);
}
