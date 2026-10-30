//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  HfsDBScraper.h
//
//  Functions for scraping from the HfsDB database (db.hfsplay.fr).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#ifndef ES_APP_SCRAPERS_HFSDB_SCRAPER_H
#define ES_APP_SCRAPERS_HFSDB_SCRAPER_H

#include "Scraper.h"

#include <ctime>
#include <string>

class HfsDBScraper : public Scraper
{
public:
    const std::string getName() const override { return "HFSDB"; }
    bool isSupportedPlatform(SystemData* system) const override;
    const std::set<ScraperMediaSource>& getSupportedMedias() const override;

    // The credentials are read from the ScraperHfsDBLogin setting ("user:password").
    bool isConfigured() const override;

protected:
    void generateRequests(const ScraperSearchParams& params,
                          std::queue<std::unique_ptr<ScraperRequest>>& requests,
                          std::vector<ScraperSearchResult>& results) override;

private:
    bool ensureToken();

    std::string mToken;
    time_t mTokenDate {0};
};

class HfsDBRequest : public ScraperHttpRequest
{
public:
    HfsDBRequest(std::vector<ScraperSearchResult>& resultsWrite,
                 const std::string& url,
                 const HttpReqOptions& options)
        : ScraperHttpRequest(resultsWrite, url, options)
    {
    }

protected:
    void process(const std::unique_ptr<HttpReq>& req,
                 std::vector<ScraperSearchResult>& results) override;
};

#endif // ES_APP_SCRAPERS_HFSDB_SCRAPER_H
