//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend (Exia)
//  BackgroundScraper.h
//
//  Non modal batch scraping. The queue is advanced from Window::update() through a per frame
//  poll, so the scraping keeps running while the user browses the gamelist. All handles used
//  here only poll state, the actual network I/O is done by the HttpReq worker thread.
//

#ifndef ES_APP_SCRAPERS_BACKGROUND_SCRAPER_H
#define ES_APP_SCRAPERS_BACKGROUND_SCRAPER_H

#include "scrapers/Scraper.h"

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

class SystemData;

class BackgroundScraper
{
public:
    // Starts a batch scrape. Does nothing if a scrape is already running.
    static void start(
        const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches);

    static bool isRunning() { return sInstance != nullptr; }
    static bool isPaused() { return sInstance != nullptr && sInstance->mPaused; }

    static void pause();
    static void resume();
    static void stop();

    // Example "12/340", empty when no scrape is running.
    static std::string getProgressText();

    // Called once per frame from Window::update().
    static void update();

    // Used by the command line scraping entry which has no window to show notifications in.
    static void setHeadless(bool headless) { sHeadless = headless; }

    // Number of games that were skipped (no result or an error) by the last completed scrape.
    static int getLastSkippedGames() { return sLastSkipped; }

private:
    enum SlotState { IDLE, SEARCHING, FETCHING_URLS, RESOLVING };

    struct Slot {
        ScraperSearchParams params;
        std::unique_ptr<ScraperSearchHandle> search;
        std::unique_ptr<ScraperSearchHandle> mediaURLs;
        std::unique_ptr<MDResolveHandle> resolve;
        std::vector<ScraperSearchResult> results;
        SlotState state {IDLE};
    };

    BackgroundScraper(
        const std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>& searches,
        int slotCount);
    ~BackgroundScraper();

    void processSlot(Slot& slot);
    void beginNext(Slot& slot);
    void skipCurrent(Slot& slot);
    void acceptResult(Slot& slot, const ScraperSearchResult& result);
    void finish();

    void notify(const std::string& text, int duration);

    std::queue<ScraperSearchParams> mSearchQueue;
    std::map<SystemData*, int> mQueueCountPerSystem;
    std::vector<Slot> mSlots;

    int mTotal;
    int mSuccessful;
    int mSkipped;

    bool mPaused;
    bool mStopped;

    static BackgroundScraper* sInstance;
    static bool sHeadless;
    static int sLastSkipped;
};

#endif // ES_APP_SCRAPERS_BACKGROUND_SCRAPER_H
