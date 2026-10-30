//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ArcadeDBJSONScraper.h
//
//  Functions for scraping from ArcadeDB (adb.arcadeitalia.net).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#ifndef ES_APP_SCRAPERS_ARCADE_DB_JSON_SCRAPER_H
#define ES_APP_SCRAPERS_ARCADE_DB_JSON_SCRAPER_H

#include "scrapers/Scraper.h"

class ArcadeDBScraper : public Scraper
{
public:
    const std::string getName() const override { return "ARCADEDB"; }

    bool isSupportedPlatform(SystemData* system) const override;

    const std::set<ScraperMediaSource>& getSupportedMedias() const override;

protected:
    void generateRequests(const ScraperSearchParams& params,
                          std::queue<std::unique_ptr<ScraperRequest>>& requests,
                          std::vector<ScraperSearchResult>& results) override;
};

class ArcadeDBJSONRequest : public ScraperHttpRequest
{
public:
    ArcadeDBJSONRequest(std::vector<ScraperSearchResult>& resultsWrite, const std::string& url)
        : ScraperHttpRequest(resultsWrite, url)
    {
    }

protected:
    void process(const std::unique_ptr<HttpReq>& req,
                 std::vector<ScraperSearchResult>& results) override;
};

#endif // ES_APP_SCRAPERS_ARCADE_DB_JSON_SCRAPER_H
