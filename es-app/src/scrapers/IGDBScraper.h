//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  IGDBScraper.h
//
//  Functions for scraping from the IGDB database (api.igdb.com).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#ifndef ES_APP_SCRAPERS_IGDB_SCRAPER_H
#define ES_APP_SCRAPERS_IGDB_SCRAPER_H

#include "Scraper.h"

#include <ctime>
#include <string>

class IGDBScraper : public Scraper
{
public:
    const std::string getName() const override { return "IGDB"; }
    bool isSupportedPlatform(SystemData* system) const override;
    const std::set<ScraperMediaSource>& getSupportedMedias() const override;

    // Requires the ScraperClientIdIGDB and ScraperClientSecretIGDB settings.
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

class IGDBRequest : public ScraperHttpRequest
{
public:
    IGDBRequest(std::vector<ScraperSearchResult>& resultsWrite,
                const std::string& url,
                const HttpReqOptions& options)
        : ScraperHttpRequest(resultsWrite, url, options)
    {
    }

protected:
    void process(const std::unique_ptr<HttpReq>& req,
                 std::vector<ScraperSearchResult>& results) override;
};

#endif // ES_APP_SCRAPERS_IGDB_SCRAPER_H
