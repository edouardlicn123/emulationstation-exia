//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  IGDBScraper.cpp
//
//  Functions for scraping from the IGDB database (api.igdb.com).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#include "scrapers/IGDBScraper.h"

#include "FileData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <chrono>
#include <exception>
#include <map>
#include <thread>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"

using namespace PlatformIds;
using namespace rapidjson;

namespace
{
    const std::map<PlatformId, std::string> igdbPlatformIds {
        {ARCADE, "52"}, // Has special filtering
        {THREEDO, "50"},
        {NINTENDO_3DS, "37"},
        {COLECO_ADAM, "68"},
        {COMMODORE_AMIGA, "16"},
        {COMMODORE_AMIGA_CD32, "114"},
        {COMMODORE_CDTV, "158"},
        {AMSTRAD_CPC, "25"},
        {APPLE_IIGS, "115"},
        {APPLE_II, "75"},
        {ACORN_ARCHIMEDES, "116"},
        {BALLY_ASTROCADE, "91"},
        {ATARI_2600, "59"},
        {ATARI_5200, "66"},
        {ATARI_7800, "60"},
        {ATARI_800, "89769"},
        {ATARI_JAGUAR, "62"},
        {ATARI_JAGUAR_CD, "410"},
        {ATARI_LYNX, "61"},
        {ATARI_ST, "63"},
        {ATARI_XE, "65"},
        {ATOMISWAVE, "52"},
        {BBC_MICRO, "69"},
        {ACORN_ELECTRON, "134"},
        {COMMODORE_VIC20, "71"},
        {COMMODORE_64, "15"},
        {PHILIPS_CDI, "117"},
        {FAIRCHILD_CHANNELF, "127"},
        {COLECOVISION, "68"},
        {PC, "13"},
        {MICROSOFT_WINDOWS, "13,6"},
        {SEGA_DREAMCAST, "23"},
        {FUJITSU_FM_TOWNS, "118"},
        {NINTENDO_GAME_AND_WATCH, "307"},
        {NINTENDO_GAMECUBE, "21"},
        {SEGA_GAME_GEAR, "35"},
        {NINTENDO_GAME_BOY_ADVANCE, "24"},
        {NINTENDO_GAME_BOY_COLOR, "22"},
        {NINTENDO_GAME_BOY, "33"},
        {SEGA_GENESIS, "29"},
        {AMSTRAD_GX4000, "25"},
        {INTELLIVISION, "67"},
        {SEGA_MASTER_SYSTEM, "64"},
        {SEGA_PICO, "339"},
        {THOMSON_MOTO, "156"},
        {MSX, "27"},
        {NINTENDO_64, "4"},
        {NINTENDO_DS, "20"},
        {SNK_NEO_GEO_CD, "136"},
        {SNK_NEO_GEO, "79"},
        {NINTENDO_ENTERTAINMENT_SYSTEM, "18"},
        {SNK_NEO_GEO_POCKET_COLOR, "120"},
        {SNK_NEO_GEO_POCKET, "119"},
        {NEC_PCFX, "274"},
        {NEC_PC_8800, "125"},
        {NEC_PC_9800, "149"},
        {NEC_PC_ENGINE, "86"},
        {NEC_PC_ENGINE_CD, "150"},
        {COMMODORE_PLUS4, "94"},
        {SONY_PLAYSTATION_3, "9"},
        {SONY_PLAYSTATION_2, "8"},
        {SONY_PLAYSTATION_VITA, "46"},
        {SONY_PLAYSTATION_PORTABLE, "38"},
        {SONY_PLAYSTATION, "131"},
        {NINTENDO_SATELLAVIEW, "306"},
        {SEGA_SATURN, "32"},
        {EPOCH_SCV, "376"},
        {GAMEENGINE_SCUMMVM, "6"},
        {SEGA_32X, "30"},
        {SEGA_CD, "78"},
        {SEGA_SG1000, "84"},
        {SUPER_NINTENDO, "19"},
        {SUPER_NINTENDO_MSU1, "19"},
        {WATARA_SUPERVISION, "415"},
        {NEC_SUPERGRAFX, "128"},
        {NINTENDO_SWITCH, "130"},
        {TEXAS_INSTRUMENTS_TI99, "129"},
        {TANDY_COLOR_COMPUTER, "126"},
        {VECTREX, "70"},
        {VIDEOPAC_ODYSSEY2, "133"},
        {NINTENDO_VIRTUAL_BOY, "87"},
        {NINTENDO_WII_U, "41"},
        {NINTENDO_WII, "5"},
        {BANDAI_WONDERSWAN_COLOR, "123"},
        {BANDAI_WONDERSWAN, "57"},
        {SHARP_X1, "77"},
        {SHARP_X68000, "121"},
        {MICROSOFT_XBOX_360, "12"},
        {MICROSOFT_XBOX, "11"},
        {SINCLAIR_ZX81_SINCLAR, "373"},
        {SINCLAIR_ZX_SPECTRUM, "2"},
        {FUJITSU_FM_7, "152"},
    };

    // Blocks the calling thread until the request has completed or the timeout is reached.
    bool waitForRequest(HttpReq& request, int timeoutSeconds)
    {
        const auto deadline {std::chrono::steady_clock::now() +
                             std::chrono::seconds(timeoutSeconds)};

        while (request.status() == HttpReq::REQ_IN_PROGRESS) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return request.status() == HttpReq::REQ_SUCCESS;
    }

    void processGame(const Value& game, std::vector<ScraperSearchResult>& results)
    {
        ScraperSearchResult result;

        if (game.HasMember("name") && game["name"].IsString())
            result.mdl.set("name", game["name"].GetString());

        if (game.HasMember("summary") && game["summary"].IsString())
            result.mdl.set("desc", game["summary"].GetString());

        if (game.HasMember("involved_companies") && game["involved_companies"].IsArray()) {
            const Value& involvedCompanies {game["involved_companies"]};
            for (int i {0}; i < static_cast<int>(involvedCompanies.Size()); ++i) {
                auto& involvedCompany {involvedCompanies[i]};
                if (!involvedCompany.HasMember("company") ||
                    !involvedCompany["company"].IsObject() ||
                    !involvedCompany["company"].HasMember("name") ||
                    !involvedCompany["company"]["name"].IsString())
                    continue;

                const std::string companyName {involvedCompany["company"]["name"].GetString()};
                if (companyName.empty())
                    continue;

                if (involvedCompany.HasMember("developer") &&
                    involvedCompany["developer"].IsBool() &&
                    involvedCompany["developer"].GetBool())
                    result.mdl.set("developer", companyName);

                if (involvedCompany.HasMember("publisher") &&
                    involvedCompany["publisher"].IsBool() &&
                    involvedCompany["publisher"].GetBool())
                    result.mdl.set("publisher", companyName);
            }
        }

        if (game.HasMember("cover") && game["cover"].IsObject() &&
            game["cover"].HasMember("image_id") && game["cover"]["image_id"].IsString()) {
            result.coverUrl =
                "https://images.igdb.com/igdb/image/upload/t_original/" +
                std::string(game["cover"]["image_id"].GetString()) + ".jpg";
            result.coverFormat = ".jpg";
        }

        if (Settings::getInstance()->getBool("ScrapeScreenshots") &&
            game.HasMember("screenshots") && game["screenshots"].IsArray()) {
            const Value& screenshots {game["screenshots"]};
            for (int i {0}; i < static_cast<int>(screenshots.Size()); ++i) {
                auto& screenshot {screenshots[i]};
                if (!screenshot.HasMember("image_id") || !screenshot["image_id"].IsString())
                    continue;

                result.screenshotUrl =
                    "https://images.igdb.com/igdb/image/upload/t_screenshot_huge/" +
                    std::string(screenshot["image_id"].GetString()) + ".jpg";
                result.screenshotFormat = ".jpg";
                break;
            }
        }

        if (Settings::getInstance()->getBool("ScrapeFanArt") && game.HasMember("artworks") &&
            game["artworks"].IsArray()) {
            const Value& artworks {game["artworks"]};
            for (int i {0}; i < static_cast<int>(artworks.Size()); ++i) {
                auto& artwork {artworks[i]};
                if (!artwork.HasMember("image_id") || !artwork["image_id"].IsString())
                    continue;

                result.fanartUrl = "https://images.igdb.com/igdb/image/upload/t_1080p/" +
                                   std::string(artwork["image_id"].GetString()) + ".jpg";
                result.fanartFormat = ".jpg";
                break;
            }
        }

        result.mediaURLFetch = COMPLETED;

        results.push_back(result);
    }
} // namespace

bool IGDBScraper::isConfigured() const
{
    return !Settings::getInstance()->getString("ScraperClientIdIGDB").empty() &&
           !Settings::getInstance()->getString("ScraperClientSecretIGDB").empty();
}

bool IGDBScraper::ensureToken()
{
    if (!mToken.empty() && (std::time(nullptr) - mTokenDate) < 1800)
        return true;

    mToken.clear();

    if (!isConfigured())
        return false;

    HttpReqOptions options;
    options.customHeaders.push_back("Content-Type: application/x-www-form-urlencoded");
    options.dataToPost = "client_id=" +
                         HttpReq::urlEncode(Settings::getInstance()->getString(
                             "ScraperClientIdIGDB")) +
                         "&client_secret=" +
                         HttpReq::urlEncode(Settings::getInstance()->getString(
                             "ScraperClientSecretIGDB")) +
                         "&grant_type=client_credentials";

    HttpReq request("https://id.twitch.tv/oauth2/token", true, &options);

    if (!waitForRequest(request, 10)) {
        LOG(LogError) << "IGDBScraper::ensureToken(): Token request failed";
        return false;
    }

    Document doc;
    doc.Parse(request.getContent().c_str());

    if (doc.HasParseError() || !doc.HasMember("access_token") ||
        !doc["access_token"].IsString()) {
        LOG(LogError) << "IGDBScraper::ensureToken(): Invalid token response";
        return false;
    }

    mToken = doc["access_token"].GetString();
    mTokenDate = std::time(nullptr);

    return true;
}

void IGDBScraper::generateRequests(const ScraperSearchParams& params,
                                   std::queue<std::unique_ptr<ScraperRequest>>& requests,
                                   std::vector<ScraperSearchResult>& results)
{
    if (!ensureToken()) {
        LOG(LogError) << "IGDBScraper::generateRequests(): Invalid or missing credentials";
        return;
    }

    std::string cleanName {params.nameOverride};
    if (cleanName.empty())
        cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(
            params.game->getPath()));

    std::string data {
        "fields id,name,platforms.name,genres.name,game_modes.name,"
        "multiplayer_modes.offlinemax,release_dates.date,release_dates.region,"
        "release_dates.platform,cover.*,screenshots.*,artworks.*,url,summary,"
        "aggregated_rating,involved_companies.company.name,involved_companies.developer,"
        "involved_companies.publisher;"};
    data += " search \"" + cleanName + "\";";
    data += " limit 10;";

    const std::string url {"https://api.igdb.com/v4/games"};

    HttpReqOptions tokenAuth;
    tokenAuth.customHeaders.push_back("Client-ID: " + Settings::getInstance()->getString(
                                          "ScraperClientIdIGDB"));
    tokenAuth.customHeaders.push_back("Authorization: Bearer " + mToken);
    tokenAuth.customHeaders.push_back("Content-Type: text/plain");
    tokenAuth.customHeaders.push_back("Accept: application/json");

    for (auto platform : params.system->getPlatformIds()) {
        const auto it {igdbPlatformIds.find(platform)};
        if (it == igdbPlatformIds.cend())
            continue;

        // Some platforms are stored as comma separated lists of IGDB platform ids.
        size_t lastPos {0};
        while (lastPos <= it->second.length()) {
            const size_t commaPos {it->second.find(',', lastPos)};
            const std::string platformId {Utils::String::trim(it->second.substr(
                lastPos, commaPos == std::string::npos ? std::string::npos :
                                                         commaPos - lastPos))};
            if (!platformId.empty()) {
                HttpReqOptions options {tokenAuth};
                options.dataToPost = data + " where release_dates.platform = " + platformId + ";";
                requests.push(
                    std::unique_ptr<ScraperRequest>(new IGDBRequest(results, url, options)));
            }
            if (commaPos == std::string::npos)
                break;
            lastPos = commaPos + 1;
        }
    }

    if (requests.empty()) {
        HttpReqOptions options {tokenAuth};
        options.dataToPost = data;
        requests.push(std::unique_ptr<ScraperRequest>(new IGDBRequest(results, url, options)));
    }
}

bool IGDBScraper::isSupportedPlatform(SystemData* system) const
{
    if (system == nullptr)
        return false;

    for (auto platform : system->getPlatformIds())
        if (igdbPlatformIds.find(platform) != igdbPlatformIds.cend())
            return true;

    return false;
}

const std::set<Scraper::ScraperMediaSource>& IGDBScraper::getSupportedMedias() const
{
    static std::set<ScraperMediaSource> supportedMedias {Screenshot, Box2d, FanArt};

    return supportedMedias;
}

void IGDBRequest::process(const std::unique_ptr<HttpReq>& req,
                          std::vector<ScraperSearchResult>& results)
{
    Document doc;
    doc.Parse(req->getContent().c_str());

    if (doc.HasParseError()) {
        const std::string err {std::string("IGDBRequest - Error parsing JSON. \n\t") +
                               GetParseError_En(doc.GetParseError())};
        setError(err, true);
        LOG(LogError) << err;
        return;
    }

    if (!doc.IsArray() || doc.Empty()) {
        LOG(LogWarning) << "IGDBRequest - Response is not valid";
        return;
    }

    for (int i {0}; i < static_cast<int>(doc.Size()); ++i) {
        auto& v {doc[i]};

        try {
            processGame(v, results);
        }
        catch (std::runtime_error& e) {
            LOG(LogError) << "Error while processing game: " << e.what();
        }
    }
}
