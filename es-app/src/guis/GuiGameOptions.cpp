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
#include "GuiMetaDataEd.h"
#include "Sound.h"
#include "UIModeController.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperMenu.h"
#include "scrapers/Scraper.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/GamelistView.h"
#include "views/ViewController.h"

GuiGameOptions::GuiGameOptions(FileData* game)
    : mMenu {Utils::String::toUpper(game->getName())}
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
        close();
        mWindow->startMediaViewer(game, false);
    });

    addEntry(_("SCRAPE"), [this] {
        mWindow->pushGui(new GuiScraperMenu(_("SCRAPE")));
        close();
    });

    addEntry(_("EDIT THIS GAME'S METADATA"), [this] {
        openMetaDataEd();
        close();
    });

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
