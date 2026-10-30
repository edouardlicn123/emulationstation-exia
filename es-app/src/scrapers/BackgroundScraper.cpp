//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend (Exia)
//  BackgroundScraper.cpp
//
//  Non modal batch scraping. Everything runs on the main thread, the handles are only polled
//  through AsyncHandle::status(), which in turn polls the HttpReq worker thread.
//

#include "scrapers/BackgroundScraper.h"

#include "CollectionSystemsManager.h"
#include "FileFilterIndex.h"
#include "FileData.h"
#include "GamelistFileParser.h"
#include "Log.h"
#include "Scripting.h"
#include "Settings.h"
#include "SystemData.h"
#include "Window.h"
#include "guis/GuiMetaDataEd.h"
#include "guis/GuiScraperSearch.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <sstream>

BackgroundScraper* BackgroundScraper::sInstance = nullptr;
bool BackgroundScraper::sHeadless = false;
int BackgroundScraper::sLastSkipped = 0;

BackgroundScraper::BackgroundScraper(
    const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches,
    int slotCount)
    : mSearchQueue {searches.first}
    , mQueueCountPerSystem {searches.second}
    , mTotal {static_cast<int>(searches.first.size())}
    , mSuccessful {0}
    , mSkipped {0}
    , mPaused {false}
    , mStopped {false}
{
    mSlots.resize(slotCount < 1 ? 1 : slotCount);
}

BackgroundScraper::~BackgroundScraper()
{
    sInstance = nullptr;
}

void BackgroundScraper::start(
    const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches)
{
    if (sInstance != nullptr || searches.first.empty())
        return;

    Scraper* scraper {Scraper::getScraper()};
    if (scraper == nullptr) {
        LOG(LogError) << "BackgroundScraper::start(): no valid scraper configured";
        return;
    }

    std::string error;
    int threadCount {scraper->getThreadCount(error)};
    if (threadCount < 1)
        threadCount = 1;

    sInstance = new BackgroundScraper(searches, threadCount);

    const std::string scraperService {
        Settings::getInstance()->getString("Scraper") == "thegamesdb" ? "thegamesdb" :
                                                                       "screenscraper"};
    const std::string scraperMode {"automatic"};

    Scripting::fireEvent("scraper-start", scraperService, scraperMode,
                         std::to_string(sInstance->mQueueCountPerSystem.size()),
                         std::to_string(sInstance->mTotal));
}

void BackgroundScraper::pause()
{
    if (sInstance != nullptr)
        sInstance->mPaused = true;
}

void BackgroundScraper::resume()
{
    if (sInstance != nullptr)
        sInstance->mPaused = false;
}

void BackgroundScraper::stop()
{
    if (sInstance == nullptr)
        return;

    std::queue<ScraperSearchParams>().swap(sInstance->mSearchQueue);
    sInstance->mStopped = true;
}

std::string BackgroundScraper::getProgressText()
{
    if (sInstance == nullptr)
        return std::string();

    return std::to_string(sInstance->mSuccessful + sInstance->mSkipped) + "/" +
           std::to_string(sInstance->mTotal);
}

void BackgroundScraper::update()
{
    BackgroundScraper* self {sInstance};
    if (self == nullptr || self->mPaused)
        return;

    for (auto& slot : self->mSlots)
        self->processSlot(slot);

    if (!self->mSearchQueue.empty())
        return;

    for (auto& slot : self->mSlots) {
        if (slot.state != IDLE)
            return;
    }

    self->finish();
}

void BackgroundScraper::beginNext(Slot& slot)
{
    slot.params = mSearchQueue.front();
    mSearchQueue.pop();
    slot.results.clear();
    slot.state = SEARCHING;
    slot.search = startScraperSearch(slot.params);
}

void BackgroundScraper::skipCurrent(Slot& slot)
{
    slot.search.reset();
    slot.mediaURLs.reset();
    slot.resolve.reset();
    slot.results.clear();
    slot.state = IDLE;
    ++mSkipped;
}

void BackgroundScraper::acceptResult(Slot& slot, const ScraperSearchResult& result)
{
    FileData* game {slot.params.game};
    SystemData* system {slot.params.system};

    system->getIndex()->removeFromIndex(game);
    GuiScraperSearch::saveMetadata(result, game->metadata, game);
    GamelistFileParser::updateGamelist(system);
    system->getIndex()->addToIndex(game);

    ++mSuccessful;
    CollectionSystemsManager::getInstance()->refreshCollectionSystems(game);

    notify("SCRAPING " + getProgressText() + " - " + game->getName(), 2500);

    slot.results.clear();
    slot.state = IDLE;
}

void BackgroundScraper::processSlot(Slot& slot)
{
    switch (slot.state) {
        case IDLE:
            if (!mSearchQueue.empty())
                beginNext(slot);
            break;

        case SEARCHING: {
            if (!slot.search) {
                skipCurrent(slot);
                break;
            }

            const AsyncHandleStatus status {slot.search->status()};
            if (status == ASYNC_IN_PROGRESS)
                break;

            slot.results = slot.search->getResults();
            const std::string statusString {slot.search->getStatusString()};
            slot.search.reset();

            if (status == ASYNC_ERROR || slot.results.empty()) {
                LOG(LogInfo) << "BackgroundScraper: no result for "
                             << slot.params.game->getName() << " (" << statusString << ")";
                skipCurrent(slot);
                break;
            }

            if (slot.results.front().mediaURLFetch == COMPLETED) {
                slot.resolve = resolveMetaDataAssets(slot.results.front(), slot.params);
                slot.state = RESOLVING;
                break;
            }

            // TheGamesDB needs a separate API call to get the media URLs.
            std::string gameIDs;
            for (auto it = slot.results.cbegin(); it != slot.results.cend(); ++it)
                gameIDs += it->gameID + ',';
            gameIDs.pop_back();

            slot.mediaURLs = startMediaURLsFetch(gameIDs);
            slot.state = FETCHING_URLS;
            break;
        }

        case FETCHING_URLS: {
            if (!slot.mediaURLs) {
                skipCurrent(slot);
                break;
            }

            const AsyncHandleStatus status {slot.mediaURLs->status()};
            if (status == ASYNC_IN_PROGRESS)
                break;

            std::vector<ScraperSearchResult> resultsMedia {slot.mediaURLs->getResults()};
            slot.mediaURLs.reset();

            if (status == ASYNC_ERROR) {
                skipCurrent(slot);
                break;
            }

            for (auto it = resultsMedia.cbegin(); it != resultsMedia.cend(); ++it) {
                for (auto& result : slot.results) {
                    if (it->videoRequest && result.gameID == it->gameID) {
                        result.videoUrl = it->videoUrl;
                        continue;
                    }

                    if (result.gameID == it->gameID) {
                        result.box3DUrl = it->box3DUrl;
                        result.backcoverUrl = it->backcoverUrl;
                        result.coverUrl = it->coverUrl;
                        result.fanartUrl = it->fanartUrl;
                        result.marqueeUrl = it->marqueeUrl;
                        result.screenshotUrl = it->screenshotUrl;
                        result.titlescreenUrl = it->titlescreenUrl;
                        result.physicalmediaUrl = it->physicalmediaUrl;
                        result.scraperRequestAllowance = it->scraperRequestAllowance;
                        result.mediaURLFetch = COMPLETED;
                    }
                }
            }

            slot.resolve = resolveMetaDataAssets(slot.results.front(), slot.params);
            slot.state = RESOLVING;
            break;
        }

        case RESOLVING: {
            if (!slot.resolve) {
                skipCurrent(slot);
                break;
            }

            // Writing to the gamelist while the metadata editor is open could make the editor
            // save stale values afterwards, so the write is deferred until it is closed.
            Window* window {Window::getInstance()};
            if (window != nullptr && dynamic_cast<GuiMetaDataEd*>(window->peekGui()) != nullptr)
                break;

            const AsyncHandleStatus status {slot.resolve->status()};
            if (status == ASYNC_IN_PROGRESS)
                break;

            if (status == ASYNC_ERROR) {
                slot.resolve.reset();
                skipCurrent(slot);
                break;
            }

            const ScraperSearchResult result {slot.resolve->getResult()};
            slot.resolve.reset();
            acceptResult(slot, result);
            break;
        }
    }
}

void BackgroundScraper::finish()
{
    if (mSuccessful > 0) {
        for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend();
             ++it)
            (*it)->sortSystem();
    }

    Scripting::fireEvent("scraper-end", std::to_string(mSuccessful), std::to_string(mSkipped),
                         mStopped ? "stopped" : "finished", "");

    std::stringstream ss;
    if (mSuccessful == 0) {
        ss << _("NO GAMES WERE SCRAPED");
    }
    else {
        ss << Utils::String::format(
            _n("%i GAME SUCCESSFULLY SCRAPED", "%i GAMES SUCCESSFULLY SCRAPED", mSuccessful),
            mSuccessful);

        if (mSkipped > 0)
            ss << "\n" << Utils::String::format(_n("%i GAME SKIPPED", "%i GAMES SKIPPED", mSkipped),
                                                mSkipped);
    }

    notify(ss.str(), 5000);

    sLastSkipped = mSkipped;

    delete this;
}

void BackgroundScraper::notify(const std::string& text, int duration)
{
    if (sHeadless) {
        LOG(LogInfo) << "[SCRAPING] " << text;
        return;
    }

    Window* window {Window::getInstance()};
    if (window != nullptr)
        window->queueInfoPopup(text, duration);
}
