//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiGameScraper.cpp
//
//  Single-game scraping interface. Shows the search results of all enabled
//  scraping sources and lets the user accept one of them. The accepted
//  metadata and media are then written to the gamelist.
//

#include "guis/GuiGameScraper.h"
#include "MenuFontUtil.h"

#include "CollectionSystemsManager.h"
#include "FileData.h"
#include "FileFilterIndex.h"
#include "GamelistFileParser.h"
#include "Log.h"
#include "Settings.h"
#include "SystemData.h"
#include "components/ScraperSearchComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiScraperSearch.h"
#include "guis/GuiTextEditKeyboardPopup.h"
#include "guis/GuiTextEditPopup.h"
#include "resources/Font.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/GamelistView.h"
#include "views/ViewController.h"

GuiGameScraper::GuiGameScraper(Window* window, FileData* game, std::function<void()> onDone)
    : mRenderer {Renderer::getInstance()}
    , mGrid {glm::ivec2 {1, 3}}
    , mGame {game}
    , mOnDone {onDone}
    , mAccepting {false}
{
    ScraperSearchParams params;
    params.system = game->getSystem();
    params.game = game;
    params.overwriteMedia = Settings::getInstance()->getBool("ScraperOverwriteData") &&
                            Settings::getInstance()->getBool("ScraperOverwriteMedia");

    addChild(&mBackground);
    addChild(&mGrid);

    const std::string gameName {game->getName().empty() ? game->getDisplayName() : game->getName()};

    mTitle = std::make_shared<TextComponent>(
        _("SCRAPE") + " - " + gameName,
        Font::get(FONT_SIZE_LARGE * Utils::Localization::sMenuTitleScaleFactor), mMenuColorTitle,
        ALIGN_CENTER);
    mGrid.setEntry(mTitle, glm::ivec2 {0, 0}, false, true);

    mSystemName = std::make_shared<TextComponent>(
        Utils::String::toUpper(game->getSystem()->getFullName()), menuLabelFont(), mMenuColorPrimary,
        ALIGN_CENTER);
    mGrid.setEntry(mSystemName, glm::ivec2 {0, 1}, false, true);

    mSearchComp = std::make_shared<ScraperSearchComponent>(window, params);
    mGrid.setEntry(mSearchComp, glm::ivec2 {0, 2}, true, true);

    const float aspectValue {1.778f / mRenderer->getScreenAspectRatio()};
    const float width {glm::clamp(0.95f * aspectValue, 0.70f, 0.95f) * mRenderer->getScreenWidth()};
    const float titleHeight {mRenderer->getIsVerticalOrientation() ?
                                 mRenderer->getScreenWidth() * 0.0637f :
                                 mRenderer->getScreenHeight() * 0.0637f};
    const float height {(mTitle->getFont()->getLetterHeight() + titleHeight) +
                        mSystemName->getFont()->getHeight() * 1.75f +
                        Font::get(FONT_SIZE_MEDIUM)->getHeight() * 10.0f};

    setSize(width, height);
    setPosition((mRenderer->getScreenWidth() - mSize.x) / 2.0f,
                (mRenderer->getScreenHeight() - mSize.y) / 2.0f);

    mSearchComp->startSearch();
}

GuiGameScraper::~GuiGameScraper()
{
    if (mMediaURLsHandle)
        mMediaURLsHandle.reset();

    if (mResolveHandle)
        mResolveHandle.reset();
}

void GuiGameScraper::onSizeChanged()
{
    if (mSize.x == 0.0f || mSize.y == 0.0f)
        return;

    const float screenSize {mRenderer->getIsVerticalOrientation() ?
                                mRenderer->getScreenWidth() :
                                mRenderer->getScreenHeight()};

    // The title row gets the letter height plus the theme padding, the system name row gets
    // its font height plus the extra spacing that the total height calculation reserves for
    // it. Halving the title row would make the two rows overlap.
    mGrid.setRowHeightPerc(
        0, (mTitle->getFont()->getLetterHeight() + screenSize * 0.0637f) / mSize.y, false);
    mGrid.setRowHeightPerc(1, mSystemName->getFont()->getHeight() * 1.75f / mSize.y, false);
    mGrid.setSize(mSize);
    mBackground.fitTo(mSize);
}

bool GuiGameScraper::input(InputConfig* config, Input input)
{
    if (input.value != 0 && config->isMappedTo("b", input)) {
        finish();
        return true;
    }

    if (input.value != 0 && !mAccepting && config->isMappedTo("a", input) &&
        mSearchComp->hasResults()) {
        acceptResult();
        return true;
    }

    if (input.value != 0 && !mAccepting && config->isMappedTo("y", input)) {
        openInputScreen();
        return true;
    }

    return GuiComponent::input(config, input);
}

void GuiGameScraper::update(int deltaTime)
{
    if (mMediaURLsHandle != nullptr && mMediaURLsHandle->status() != ASYNC_IN_PROGRESS) {
        if (mMediaURLsHandle->status() == ASYNC_DONE) {
            const std::vector<ScraperSearchResult> mediaResults {
                mMediaURLsHandle->getResults()};
            mMediaURLsHandle.reset();

            for (const ScraperSearchResult& mediaResult : mediaResults) {
                if (mediaResult.gameID != mAcceptedResult.gameID)
                    continue;

                if (mediaResult.videoRequest) {
                    mAcceptedResult.videoUrl = mediaResult.videoUrl;
                    continue;
                }

                mAcceptedResult.box3DUrl = mediaResult.box3DUrl;
                mAcceptedResult.backcoverUrl = mediaResult.backcoverUrl;
                mAcceptedResult.coverUrl = mediaResult.coverUrl;
                mAcceptedResult.fanartUrl = mediaResult.fanartUrl;
                mAcceptedResult.marqueeUrl = mediaResult.marqueeUrl;
                mAcceptedResult.screenshotUrl = mediaResult.screenshotUrl;
                mAcceptedResult.titlescreenUrl = mediaResult.titlescreenUrl;
                mAcceptedResult.physicalmediaUrl = mediaResult.physicalmediaUrl;
                mAcceptedResult.mediaURLFetch = COMPLETED;

                if (!mediaResult.videoUrl.empty())
                    mAcceptedResult.videoUrl = mediaResult.videoUrl;
            }
        }
        else {
            LOG(LogError) << "GuiGameScraper: Media URL request failed: "
                          << Utils::String::replace(mMediaURLsHandle->getStatusString(), "\n", "");
            mMediaURLsHandle.reset();
        }

        resolveAssets();
    }

    if (mResolveHandle != nullptr && mResolveHandle->status() != ASYNC_IN_PROGRESS) {
        if (mResolveHandle->status() == ASYNC_ERROR) {
            LOG(LogError) << "GuiGameScraper: Media download failed: "
                          << Utils::String::replace(mResolveHandle->getStatusString(), "\n", "");
        }

        finishAccept();
        return;
    }

    GuiComponent::update(deltaTime);
}

void GuiGameScraper::acceptResult()
{
    std::string sourceId;
    const ScraperSearchResult* selected {mSearchComp->getSelectedResult(sourceId)};

    if (selected == nullptr)
        return;

    mAccepting = true;
    mAcceptedResult = *selected;
    mAcceptedSourceId = sourceId;

    // The game is taken out of the system index while its metadata is replaced, otherwise
    // the lookups would work on stale values. It is added back in finishAccept().
    mGame->getSystem()->getIndex()->removeFromIndex(mGame);

    GuiScraperSearch::saveMetadata(mAcceptedResult, mGame->metadata, mGame);

    if (mAcceptedSourceId == "thegamesdb" && mAcceptedResult.mediaURLFetch != COMPLETED)
        mMediaURLsHandle = startMediaURLsFetch(mAcceptedResult.gameID);
    else
        resolveAssets();
}

void GuiGameScraper::resolveAssets()
{
    ScraperSearchParams params;
    params.system = mGame->getSystem();
    params.game = mGame;
    params.overwriteMedia = Settings::getInstance()->getBool("ScraperOverwriteData") &&
                            Settings::getInstance()->getBool("ScraperOverwriteMedia");

    mResolveHandle = resolveMetaDataAssets(mAcceptedResult, params);
}

void GuiGameScraper::finishAccept()
{
    mResolveHandle.reset();

    SystemData* system {mGame->getSystem()};
    GamelistFileParser::updateGamelist(system);
    system->getIndex()->addToIndex(mGame);
    CollectionSystemsManager::getInstance()->refreshCollectionSystems(mGame);

    ViewController::getInstance()->getGamelistView(system)->onFileChanged(mGame, true);

    finish();
}

void GuiGameScraper::openInputScreen()
{
    std::string searchString {mSearchComp->getNameOverride()};

    if (searchString.empty())
        searchString = mGame->getCleanName();

    auto searchForFunc {[this](std::string name) {
        // Trim leading and trailing whitespaces.
        name = Utils::String::trim(name);
        mSearchComp->startSearch(name);
    }};

    if (Settings::getInstance()->getBool("VirtualKeyboard"))
        mWindow->pushGui(new GuiTextEditKeyboardPopup(0.0f, _("REFINE SEARCH"), searchString,
                                                      searchForFunc, false, _("SEARCH"),
                                                      _("SEARCH USING REFINED NAME?")));
    else
        mWindow->pushGui(new GuiTextEditPopup(_("REFINE SEARCH"), searchString, searchForFunc,
                                              false, _("SEARCH"),
                                              _("SEARCH USING REFINED NAME?")));
}

void GuiGameScraper::finish()
{
    if (mOnDone)
        mOnDone();

    delete this;
}

std::vector<HelpPrompt> GuiGameScraper::getHelpPrompts()
{
    std::vector<HelpPrompt> prompts;
    prompts.push_back(HelpPrompt("y", _("refine search")));
    prompts.push_back(HelpPrompt("a", _("accept result")));
    prompts.push_back(HelpPrompt("b", _("back")));
    return prompts;
}
