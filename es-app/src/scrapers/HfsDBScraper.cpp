//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  HfsDBScraper.cpp
//
//  Functions for scraping from the HfsDB database (db.hfsplay.fr).
//  Ported from the Batocera implementation and adapted to the Exia scraper framework.
//

#include "scrapers/HfsDBScraper.h"

#include "FileData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "utils/TimeUtil.h"

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
    const std::map<PlatformId, std::string> hfsdbPlatformIds {
        {ARCADE, "29794"}, // Has special filtering
        {THREEDO, "82826"},
        {NINTENDO_3DS, "148845"},
        {COLECO_ADAM, "240844"},
        {COMMODORE_AMIGA, "36003"},
        {COMMODORE_AMIGA_CD32, "136126"},
        {COMMODORE_CDTV, "35808"},
        {AMSTRAD_CPC, "239404"},
        {APPLE_IIGS, "240837"},
        {APPLE_II, "139807"},
        {ACORN_ARCHIMEDES, "137569"},
        {BALLY_ASTROCADE, "75808"},
        {ATARI_2600, "84224"},
        {ATARI_5200, "84223"},
        {ATARI_7800, "84225"},
        {ATARI_800, "89769"},
        {ATARI_JAGUAR, "84220"},
        {ATARI_JAGUAR_CD, "84220"},
        {ATARI_LYNX, "33756"},
        {ATARI_ST, "82160"},
        {ATARI_XE, "240841"},
        {ATOMISWAVE, "29731"},
        {BBC_MICRO, "190502"},
        {ACORN_ELECTRON, "124274"},
        {COMMODORE_VIC20, "174662"},
        {COMMODORE_64, "168000"},
        {PHILIPS_CDI, "190532"},
        {FAIRCHILD_CHANNELF, "35853"},
        {COLECOVISION, "83499"},
        {PC, "82805"},
        {MICROSOFT_WINDOWS, "82805,84756"},
        {SEGA_DREAMCAST, "83702"},
        {GAMEENGINE_EASYRPG, "82805"},
        {FUJITSU_FM_TOWNS, "238067"},
        {NINTENDO_GAME_AND_WATCH, "190542"},
        {NINTENDO_GAMECUBE, "82825"},
        {SEGA_GAME_GEAR, "33807"},
        {NINTENDO_GAME_BOY_ADVANCE, "89234"},
        {NINTENDO_GAME_BOY_COLOR, "89235"},
        {NINTENDO_GAME_BOY, "36022"},
        {SEGA_GENESIS, "33149"},
        {AMSTRAD_GX4000, "30250"},
        {INTELLIVISION, "84444"},
        {SEGA_MASTER_SYSTEM, "82166"},
        {SEGA_PICO, "175183"},
        {THOMSON_MOTO, "183673"},
        {MSX, "113089"},
        {NOKIA_NGAGE, "128704"},
        {NINTENDO_64, "32765"},
        {SEGA_NAOMI, "36453"},
        {NINTENDO_DS, "98132"},
        {SNK_NEO_GEO_CD, "237944"},
        {SNK_NEO_GEO, "29794"},
        {NINTENDO_ENTERTAINMENT_SYSTEM, "36012"},
        {SNK_NEO_GEO_POCKET_COLOR, "33980"},
        {SNK_NEO_GEO_POCKET, "33980"},
        {TANGERINE_ORIC, "240911"},
        {NEC_PCFX, "240904"},
        {NEC_PC_8800, "240898"},
        {NEC_PC_9800, "240901"},
        {NEC_PC_ENGINE, "30273"},
        {NEC_PC_ENGINE_CD, "138489"},
        {COMMODORE_PLUS4, "240851"},
        {SONY_PLAYSTATION_3, "91841"},
        {SONY_PLAYSTATION_2, "83494"},
        {SONY_PLAYSTATION_VITA, "91844"},
        {SONY_PLAYSTATION_PORTABLE, "91845"},
        {SONY_PLAYSTATION, "82937"},
        {NINTENDO_SATELLAVIEW, "190500"},
        {SEGA_SATURN, "82924"},
        {EPOCH_SCV, "189145"},
        {GAMEENGINE_SCUMMVM, "82805"},
        {SEGA_32X, "33731"},
        {SEGA_CD, "83493"},
        {SEGA_SG1000, "75225"},
        {SUPER_NINTENDO, "82351"},
        {SUPER_NINTENDO_MSU1, "82351"},
        {GAMEENGINE_SOLARUS, "82805"},
        {BANDAI_SUFAMI_TURBO, "190497"},
        {WATARA_SUPERVISION, "190515"},
        {NINTENDO_SUPER_GAME_BOY, "36022"},
        {NEC_SUPERGRAFX, "30443"},
        {NINTENDO_SWITCH, "83485"},
        {TEXAS_INSTRUMENTS_TI99, "240924"},
        {TANDY_COLOR_COMPUTER, "240922"},
        {VECTREX, "80424"},
        {VIDEOPAC_ODYSSEY2, "35882"},
        {NINTENDO_VIRTUAL_BOY, "36396"},
        {NINTENDO_WII_U, "88837"},
        {NINTENDO_WII, "89530"},
        {BANDAI_WONDERSWAN_COLOR, "36345"},
        {BANDAI_WONDERSWAN, "36312"},
        {SHARP_X1, "248941"},
        {SHARP_X68000, "239578"},
        {MICROSOFT_XBOX_360, "145692"},
        {MICROSOFT_XBOX, "144719"},
        {SINCLAIR_ZX81_SINCLAR, "190538"},
        {SINCLAIR_ZX_SPECTRUM, "100295"},
        {FUJITSU_FM_7, "240860"},
        {CASIO_PV1000, "190512"},
        {ARCADIA_2001, "129592"},
        {VTECH_CREATIVISION, "190291"},
        {HARTUNG_GAME_MASTER, "240872"},
        {CREATRONIC_MEGA_DUCK, "239486"},
        {FUNTECH_SUPER_ACAN, "240863"},
    };

    // Simple base64 encoder, only needed for the HTTP basic authentication header.
    std::string base64Encode(const std::string& input)
    {
        static const char* chars {
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

        std::string output;
        int val {0};
        int valb {-6};

        for (const unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                output.push_back(chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6)
            output.push_back(chars[((val << 8) >> (valb + 8)) & 0x3F]);

        while (output.size() % 4)
            output.push_back('=');

        return output;
    }

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

    std::vector<std::string> getMediaTagNames(const std::string& imageSource)
    {
        if (imageSource == "ss" || imageSource == "mixrbv2" || imageSource == "mixrbv1" ||
            imageSource == "mixrbv")
            return {"screenshot/in game", "screenshot/title", "screenshot"};

        if (imageSource == "sstitle")
            return {"screenshot/title", "screenshot/in game", "screenshot"};

        if (imageSource == "box-2D")
            return {"cover2d/front", "cover2d", "artwork/Flyer", "cover3d"};

        if (imageSource == "box-3D")
            return {"cover3d", "cover2d/front"};

        if (imageSource == "wheel" || imageSource == "wheel-hd")
            return {"logo"};

        if (imageSource == "marquee")
            return {"wheel", "artwork/Marquee"};

        if (imageSource == "video")
            return {"video"};

        if (imageSource == "manual")
            return {"manual"};

        if (imageSource == "fanart")
            return {"wallpaper", "artwork"};

        if (imageSource == "box-2D-back")
            return {"cover2d/back"};

        return {};
    }

    std::string findMedia(const Value& v, const std::string& scrapeSource)
    {
        if (!v.HasMember("medias") || !v["medias"].IsArray())
            return "";

        for (const auto& tag : getMediaTagNames(scrapeSource)) {
            std::string tagName {tag};
            std::string tagType;

            const size_t idx {tagName.find("/")};
            if (idx != std::string::npos) {
                tagType = tagName.substr(idx + 1);
                tagName = tagName.substr(0, idx);
            }

            const Value& medias {v["medias"]};
            for (int i {0}; i < static_cast<int>(medias.Size()); ++i) {
                auto& media {medias[i]};
                if (!media.HasMember("type") || !media["type"].IsString() ||
                    !media.HasMember("file") || !media["file"].IsString())
                    continue;

                if (tagName != media["type"].GetString())
                    continue;

                if (!tagType.empty()) {
                    if (media.HasMember("metadata") && media["metadata"].IsArray()) {
                        const Value& metadatas {media["metadata"]};
                        for (int j {0}; j < static_cast<int>(metadatas.Size()); ++j) {
                            auto& metadata {metadatas[j]};
                            if (!metadata.HasMember("name") || !metadata["name"].IsString() ||
                                !metadata.HasMember("value") || !metadata["value"].IsString())
                                continue;

                            if (tagName + "type" == std::string(metadata["name"].GetString()) &&
                                tagType == std::string(metadata["value"].GetString()))
                                return media["file"].GetString();
                        }
                    }

                    if (media.HasMember("description") && media["description"].IsString()) {
                        if (tagType == std::string(media["description"].GetString()))
                            return media["file"].GetString();
                    }

                    continue;
                }

                return media["file"].GetString();
            }
        }

        return "";
    }

    void setMedia(ScraperSearchResult& result,
                  const std::string& url,
                  std::string& targetUrl,
                  std::string& targetFormat)
    {
        if (url.empty())
            return;

        targetUrl = url;
        targetFormat = Utils::FileSystem::getExtension(url);
    }

    void processGame(const Value& game, std::vector<ScraperSearchResult>& results)
    {
        std::string language {Settings::getInstance()->getString("ScraperLanguage")};
        if (language.empty())
            language = "en";
        else {
            const size_t shortNameDivider {language.find("_")};
            if (shortNameDivider != std::string::npos)
                language = Utils::String::toLower(language.substr(0, shortNameDivider));
        }

        ScraperSearchResult result;

        const std::vector<std::string> langs {language, "en"};
        for (const auto& lang : langs) {
            const std::string name {"name_" + lang};
            if (game.HasMember(name.c_str()) && game[name.c_str()].IsString()) {
                result.mdl.set("name", game[name.c_str()].GetString());
                break;
            }
        }

        if (result.mdl.get("name").empty() && game.HasMember("name") && game["name"].IsString())
            result.mdl.set("name", game["name"].GetString());

        for (const auto& lang : langs) {
            const std::string desc {"description_" + lang};
            if (game.HasMember(desc.c_str()) && game[desc.c_str()].IsString()) {
                result.mdl.set("desc", game[desc.c_str()].GetString());
                break;
            }
        }

        if (game.HasMember("metadata") && game["metadata"].IsArray()) {
            const Value& metadatas {game["metadata"]};
            for (int i {0}; i < static_cast<int>(metadatas.Size()); ++i) {
                auto& metadata {metadatas[i]};
                if (!metadata.HasMember("name") || !metadata["name"].IsString() ||
                    !metadata.HasMember("value") || !metadata["value"].IsString())
                    continue;

                const std::string name {metadata["name"].GetString()};
                std::string value {metadata["value"].GetString()};
                if (value.empty())
                    continue;

                if (name == "genre")
                    result.mdl.set("genre", value);
                else if (name == "players") {
                    value = Utils::String::replace(value, " joueurs", "");
                    value = Utils::String::replace(value, " joueur", "");
                    value = Utils::String::replace(value, "+ de ", "");
                    result.mdl.set("players", value);
                }
                else if (name == "editor" || name == "manufacturer")
                    result.mdl.set("publisher", value);
                else if (name == "developer")
                    result.mdl.set("developer", value);
            }
        }

        time_t releaseDate {0};
        for (const auto* release :
             {"released_at_WORLD", "released_at_US", "released_at_PAL", "released_at_JPN"}) {
            if (!game.HasMember(release) || !game[release].IsString())
                continue;

            const std::string value {game[release].GetString()};
            if (value.size() < 10)
                continue;

            const time_t dt {
                Utils::Time::stringToTime(value.substr(0, 10), "%Y-%m-%d")};
            if (dt != 0) {
                releaseDate = dt;
                break;
            }
        }

        if (releaseDate != 0)
            result.mdl.set("releasedate", Utils::Time::DateTime(releaseDate));

        if (Settings::getInstance()->getBool("ScrapeScreenshots"))
            setMedia(result, findMedia(game, "ss"), result.screenshotUrl,
                     result.screenshotFormat);

        if (Settings::getInstance()->getBool("ScrapeTitleScreens"))
            setMedia(result, findMedia(game, "sstitle"), result.titlescreenUrl,
                     result.titlescreenFormat);

        if (Settings::getInstance()->getBool("ScrapeCovers"))
            setMedia(result, findMedia(game, "box-2D"), result.coverUrl, result.coverFormat);

        if (Settings::getInstance()->getBool("Scrape3DBoxes"))
            setMedia(result, findMedia(game, "box-3D"), result.box3DUrl, result.box3DFormat);

        if (Settings::getInstance()->getBool("ScrapeBackCovers"))
            setMedia(result, findMedia(game, "box-2D-back"), result.backcoverUrl,
                     result.backcoverFormat);

        if (Settings::getInstance()->getBool("ScrapeMarquees"))
            setMedia(result, findMedia(game, "wheel"), result.marqueeUrl, result.marqueeFormat);

        if (Settings::getInstance()->getBool("ScrapeVideos"))
            setMedia(result, findMedia(game, "video"), result.videoUrl, result.videoFormat);

        if (Settings::getInstance()->getBool("ScrapeManuals"))
            setMedia(result, findMedia(game, "manual"), result.manualUrl, result.manualFormat);

        if (Settings::getInstance()->getBool("ScrapeFanArt"))
            setMedia(result, findMedia(game, "fanart"), result.fanartUrl, result.fanartFormat);

        if (result.mdl.get("desc").empty() && result.screenshotUrl.empty() &&
            result.coverUrl.empty() && result.marqueeUrl.empty() && result.titlescreenUrl.empty())
            return;

        result.mediaURLFetch = COMPLETED;

        results.push_back(result);
    }
} // namespace

bool HfsDBScraper::isConfigured() const
{
    // HfsDB requires an account as the API rejects anonymous requests with a 403.
    return !Settings::getInstance()->getString("ScraperHfsDBUsername").empty() &&
           !Settings::getInstance()->getString("ScraperHfsDBPassword").empty();
}

bool HfsDBScraper::ensureToken()
{
    if (!mToken.empty() && (std::time(nullptr) - mTokenDate) < 1800)
        return true;

    mToken.clear();

    const std::string user {Settings::getInstance()->getString("ScraperHfsDBUsername")};
    const std::string pass {Settings::getInstance()->getString("ScraperHfsDBPassword")};
    if (user.empty() || pass.empty())
        return false;

    HttpReqOptions options;
    options.dataToPost = "&username=" + HttpReq::urlEncode(user) +
                         "&password=" + HttpReq::urlEncode(pass);
    options.customHeaders.push_back("Authorization: Basic " + base64Encode(user + ":" + pass));
    options.customHeaders.push_back("Content-Type: application/x-www-form-urlencoded");

    HttpReq request("https://db.hfsplay.fr/api/v1/auth/token", true, &options);

    if (!waitForRequest(request, 10)) {
        LOG(LogError) << "HfsDBScraper::ensureToken(): Token request failed";
        return false;
    }

    Document doc;
    doc.Parse(request.getContent().c_str());

    if (doc.HasParseError() || !doc.HasMember("token") || !doc["token"].IsString()) {
        LOG(LogError) << "HfsDBScraper::ensureToken(): Invalid token response";
        return false;
    }

    mToken = doc["token"].GetString();
    mTokenDate = std::time(nullptr);

    return true;
}

void HfsDBScraper::generateRequests(const ScraperSearchParams& params,
                                    std::queue<std::unique_ptr<ScraperRequest>>& requests,
                                    std::vector<ScraperSearchResult>& results)
{
    if (!ensureToken()) {
        LOG(LogError) << "HfsDBScraper::generateRequests(): Invalid or missing credentials";
        return;
    }

    const std::string path {"https://db.hfsplay.fr/api/v1/"};

    std::string cleanName {params.nameOverride};
    if (cleanName.empty())
        cleanName = Utils::String::removeParenthesis(Utils::FileSystem::getStem(
            params.game->getPath()));

    std::vector<std::string> urls;

    if (params.nameOverride.empty() && params.system->hasPlatformId(PlatformIds::ARCADE))
        urls.push_back(path + "games?medias__description=" + HttpReq::urlEncode(cleanName) +
                       "&system_id=29794");

    if (urls.empty() && !params.system->hasPlatformId(PlatformIds::ARCADE)) {
        for (auto platform : params.system->getPlatformIds()) {
            const auto it {hfsdbPlatformIds.find(platform)};
            if (it == hfsdbPlatformIds.cend())
                continue;

            // Some platforms are stored as comma separated lists of HfsDB system ids.
            size_t lastPos {0};
            while (lastPos <= it->second.length()) {
                const size_t commaPos {it->second.find(',', lastPos)};
                const std::string platformId {Utils::String::trim(it->second.substr(
                    lastPos, commaPos == std::string::npos ? std::string::npos :
                                                             commaPos - lastPos))};
                if (!platformId.empty())
                    urls.push_back(path + "games?search=" + HttpReq::urlEncode(cleanName) +
                                   "&system=" + HttpReq::urlEncode(platformId) + "&limit=25");
                if (commaPos == std::string::npos)
                    break;
                lastPos = commaPos + 1;
            }
        }
    }

    if (urls.empty())
        urls.push_back(path + "games?search=" + HttpReq::urlEncode(cleanName) + "&limit=25");

    HttpReqOptions tokenAuth;
    tokenAuth.customHeaders.push_back("Authorization: Token " + mToken);

    for (const auto& url : urls)
        requests.push(std::unique_ptr<ScraperRequest>(new HfsDBRequest(results, url, tokenAuth)));
}

bool HfsDBScraper::isSupportedPlatform(SystemData* system) const
{
    if (system == nullptr)
        return false;

    for (auto platform : system->getPlatformIds())
        if (hfsdbPlatformIds.find(platform) != hfsdbPlatformIds.cend())
            return true;

    return false;
}

const std::set<Scraper::ScraperMediaSource>& HfsDBScraper::getSupportedMedias() const
{
    static std::set<ScraperMediaSource> supportedMedias {
        Screenshot, Box2d, Box3d, Marquee, Wheel, TitleShot,
        Video,      FanArt, Manual, BoxBack};

    return supportedMedias;
}

void HfsDBRequest::process(const std::unique_ptr<HttpReq>& req,
                           std::vector<ScraperSearchResult>& results)
{
    Document doc;
    doc.Parse(req->getContent().c_str());

    if (doc.HasParseError()) {
        const std::string err {std::string("HfsDBRequest - Error parsing JSON. \n\t") +
                               GetParseError_En(doc.GetParseError())};
        setError(err, true);
        LOG(LogError) << err;
        return;
    }

    if (!doc.HasMember("count") || !doc["count"].IsInt() || doc["count"].GetInt() <= 0) {
        LOG(LogWarning) << "HfsDBRequest - Response had no game data";
        return;
    }

    if (!doc.HasMember("results") || !doc["results"].IsArray()) {
        LOG(LogWarning) << "HfsDBRequest - Response results is not an array";
        return;
    }

    const Value& games {doc["results"]};

    for (int i {0}; i < static_cast<int>(games.Size()); ++i) {
        auto& v {games[i]};

        try {
            processGame(v, results);
        }
        catch (std::runtime_error& e) {
            LOG(LogError) << "Error while processing game: " << e.what();
        }
    }
}
