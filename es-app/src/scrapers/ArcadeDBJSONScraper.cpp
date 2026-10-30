//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ArcadeDBJSONScraper.cpp
//
//  Functions for scraping from ArcadeDB (adb.arcadeitalia.net).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#include "scrapers/ArcadeDBJSONScraper.h"

#include "FileData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "utils/TimeUtil.h"

#include <exception>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

// Current API version, responses with an older release number are rejected.
#ifndef ARCADE_DB_RELEASE_VERSION
#define ARCADE_DB_RELEASE_VERSION 4
#endif

using namespace PlatformIds;
using namespace rapidjson;

void ArcadeDBScraper::generateRequests(const ScraperSearchParams& params,
                                       std::queue<std::unique_ptr<ScraperRequest>>& requests,
                                       std::vector<ScraperSearchResult>& results)
{
    std::string cleanName {params.nameOverride};

    if (cleanName.empty())
        cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(params.game->getPath()));

    // ArcadeDB matches on the MAME set name, so spaces and dots are not part of the query.
    cleanName = Utils::String::replace(cleanName, " ", "");
    cleanName = Utils::String::replace(cleanName, ".", "");

    const std::string path {"http://adb.arcadeitalia.net/service_scraper.php?ajax=query_mame"
                            "&lang=en&use_parent=1&game_name=" +
                            HttpReq::urlEncode(cleanName)};

    requests.push(std::unique_ptr<ScraperRequest>(new ArcadeDBJSONRequest(results, path)));
}

bool ArcadeDBScraper::isSupportedPlatform(SystemData* system) const
{
    if (system == nullptr)
        return false;

    return system->hasPlatformId(PlatformIds::ARCADE) ||
           system->hasPlatformId(PlatformIds::SNK_NEO_GEO) ||
           system->hasPlatformId(PlatformIds::LCD_GAMES) ||
           system->hasPlatformId(PlatformIds::ATOMISWAVE) ||
           system->hasPlatformId(PlatformIds::SEGA_NAOMI) ||
           system->hasPlatformId(PlatformIds::DAPHNE);
}

const std::set<Scraper::ScraperMediaSource>& ArcadeDBScraper::getSupportedMedias() const
{
    static std::set<ScraperMediaSource> supportedMedias {Screenshot, Box2d, Marquee, TitleShot,
                                                         Video};

    return supportedMedias;
}

namespace
{
    std::string getStringOrThrow(const Value& v, const std::string& key)
    {
        if (!v.HasMember(key.c_str()) || !v[key.c_str()].IsString()) {
            throw std::runtime_error(
                "rapidjson internal assertion failure: missing or non string key: " + key);
        }

        return v[key.c_str()].GetString();
    }

    std::string findMedia(const Value& v, const std::string& key)
    {
        if (v.HasMember(key.c_str()) && v[key.c_str()].IsString())
            return v[key.c_str()].GetString();

        return std::string();
    }

    void processGame(const Value& game, std::vector<ScraperSearchResult>& results)
    {
        ScraperSearchResult result;

        result.mdl.set("name", getStringOrThrow(game, "title"));

        if (game.HasMember("history") && game["history"].IsString())
            result.mdl.set("desc", getStringOrThrow(game, "history"));

        if (game.HasMember("year") && game["year"].IsString())
            result.mdl.set("releasedate",
                           Utils::Time::DateTime(
                               Utils::Time::stringToTime(getStringOrThrow(game, "year"), "%Y")));

        if (game.HasMember("manufacturer") && game["manufacturer"].IsString()) {
            result.mdl.set("developer", getStringOrThrow(game, "manufacturer"));
            result.mdl.set("publisher", getStringOrThrow(game, "manufacturer"));
        }

        if (game.HasMember("genre") && game["genre"].IsString())
            result.mdl.set("genre", getStringOrThrow(game, "genre"));

        if (game.HasMember("players") && game["players"].IsInt())
            result.mdl.set("players", std::to_string(game["players"].GetInt()));

        const std::string screenshot {findMedia(game, "url_image_ingame")};
        if (!screenshot.empty()) {
            result.screenshotUrl = screenshot;
            result.screenshotFormat = ".png";
        }

        const std::string titleScreen {findMedia(game, "url_image_title")};
        if (!titleScreen.empty()) {
            result.titlescreenUrl = titleScreen;
            result.titlescreenFormat = ".png";
        }

        const std::string flyer {findMedia(game, "url_image_flyer")};
        if (!flyer.empty()) {
            result.coverUrl = flyer;
            result.coverFormat = ".png";
        }

        const std::string marquee {findMedia(game, "url_image_marquee")};
        if (!marquee.empty()) {
            result.marqueeUrl = marquee;
            result.marqueeFormat = ".png";
        }

        if (Settings::getInstance()->getBool("ScrapeVideos")) {
            std::string video {findMedia(game, "url_video_shortplay")};
            if (video.empty())
                video = findMedia(game, "url_video_shortplay_hd");
            if (!video.empty())
                result.videoUrl = video;
        }

        // All media URLs are part of the search response, no additional request is needed.
        result.mediaURLFetch = COMPLETED;

        results.push_back(result);
    }
} // namespace

void ArcadeDBJSONRequest::process(const std::unique_ptr<HttpReq>& req,
                                  std::vector<ScraperSearchResult>& results)
{
    Document doc;
    doc.Parse(req->getContent().c_str());

    if (doc.HasParseError()) {
        std::string err {std::string("ArcadeDBJSONRequest - Error parsing JSON. \n\t") +
                         GetParseError_En(doc.GetParseError())};
        setError(err, true);
        LOG(LogError) << err;
        return;
    }

    if (!doc.HasMember("release") || !doc["release"].IsInt() ||
        doc["release"].GetInt() < ARCADE_DB_RELEASE_VERSION) {
        LOG(LogWarning) << "ArcadeDBJSONRequest - Response had wrong format";
        return;
    }

    if (!doc.HasMember("result") || !doc["result"].IsArray()) {
        LOG(LogWarning) << "ArcadeDBJSONRequest - Response had no game data";
        return;
    }

    const Value& games {doc["result"]};

    for (int i {0}; i < static_cast<int>(games.Size()); ++i) {
        auto& v = games[i];
        try {
            processGame(v, results);
        }
        catch (std::runtime_error& e) {
            LOG(LogError) << "Error while processing game: " << e.what();
        }
    }
}
