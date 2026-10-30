//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  Scraper.h
//
//  Main scraper logic.
//  Called from GuiScraperSearch.
//

#ifndef ES_APP_SCRAPERS_SCRAPER_H
#define ES_APP_SCRAPERS_SCRAPER_H

#include "AsyncHandle.h"
#include "HttpReq.h"
#include "MetaData.h"
#include "PlatformId.h"

#include <assert.h>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <utility>

// Maximum number of results that are kept and displayed from a single scraping source.
#define MAX_SCRAPER_RESULTS 20

class FileData;
class SystemData;

enum downloadStatus {
    NOT_STARTED,
    IN_PROGRESS,
    COMPLETED
};

struct ScraperSearchParams {
    SystemData* system;
    FileData* game;
    std::string md5Hash;
    long fileSize;

    std::string nameOverride;
    bool automaticMode;
    // Explicitly selected scraping source, an empty value means that the source configured
    // in the settings file is used. Needed to query multiple sources for a single game.
    std::string scraperName;
    // Whether existing media files may be replaced. Only used for the current scrape queue,
    // it is not saved to the settings file.
    bool overwriteMedia {true};

    ScraperSearchParams()
        : fileSize {0}
        , automaticMode {false}
    {
    }
};

struct ScraperSearchResult {
    ScraperSearchResult()
        : mdl(GAME_METADATA)
        , scraperRequestMaxAllowance {0}
        , scraperRequestAllowance {0}
        , mediaURLFetch {NOT_STARTED}
        , thumbnailDownloadStatus {NOT_STARTED}
        , mediaFilesDownloadStatus {NOT_STARTED}
        , savedNewMedia {false}
        , videoRequest {false}
    {
    }

    MetaDataList mdl;
    std::string gameID;
    std::string md5Hash;
    std::vector<PlatformIds::PlatformId> platformIDs;

    // How many more objects the scraper service allows to be downloaded
    // within a given time period.
    unsigned int scraperRequestMaxAllowance;
    unsigned int scraperRequestAllowance;

    enum downloadStatus mediaURLFetch;
    enum downloadStatus thumbnailDownloadStatus;
    enum downloadStatus mediaFilesDownloadStatus;

    std::string thumbnailImageData; // Thumbnail cache, this will contain the entire image.
    std::string thumbnailImageUrl;

    std::string box3DUrl;
    std::string backcoverUrl;
    std::string coverUrl;
    std::string fanartUrl;
    std::string marqueeUrl;
    std::string physicalmediaUrl;
    std::string screenshotUrl;
    std::string titlescreenUrl;
    std::string videoUrl;
    std::string manualUrl;

    // Needed to pre-set the image type.
    std::string box3DFormat;
    std::string backcoverFormat;
    std::string coverFormat;
    std::string fanartFormat;
    std::string marqueeFormat;
    std::string physicalmediaFormat;
    std::string screenshotFormat;
    std::string titlescreenFormat;
    std::string videoFormat;
    std::string manualFormat;

    // Indicates whether any new media files were downloaded and saved.
    bool savedNewMedia;

    // Set for TheGamesDB if it's a video request.
    bool videoRequest;
};

// A scraper search gathers results from (potentially multiple) ScraperRequests.
class ScraperRequest : public AsyncHandle
{
public:
    ScraperRequest(std::vector<ScraperSearchResult>& resultsWrite);
    virtual void update() = 0;

protected:
    std::vector<ScraperSearchResult>& mResults;
};

// A single HTTP request that needs to be processed to get the results.
class ScraperHttpRequest : public ScraperRequest
{
public:
    ScraperHttpRequest(std::vector<ScraperSearchResult>& resultsWrite, const std::string& url);
    // Used by scrapers that need custom headers or a POST body (IGDB and HfsDB).
    ScraperHttpRequest(std::vector<ScraperSearchResult>& resultsWrite,
                       const std::string& url,
                       const HttpReqOptions& options);
    void update() override;

protected:
    virtual void process(const std::unique_ptr<HttpReq>& req,
                         std::vector<ScraperSearchResult>& results) = 0;

private:
    std::unique_ptr<HttpReq> mReq;
};

// A request to get a list of results.
class ScraperSearchHandle : public AsyncHandle
{
public:
    ScraperSearchHandle() { setStatus(ASYNC_IN_PROGRESS); }

    void update();
    const std::vector<ScraperSearchResult>& getResults() const
    {
        assert(mStatus != ASYNC_IN_PROGRESS);
        return mResults;
    }

protected:
    friend class Scraper;

    friend std::unique_ptr<ScraperSearchHandle> startScraperSearch(
        const ScraperSearchParams& params);

    friend std::unique_ptr<ScraperSearchHandle> startMediaURLsFetch(const std::string& gameIDs);

    std::queue<std::unique_ptr<ScraperRequest>> mRequestQueue;
    std::vector<ScraperSearchResult> mResults;
};

// Minimal scraper abstraction inspired by the Batocera implementation. It only exposes the
// capabilities needed to build a dynamic source list, filter sources by platform and query
// multiple sources for a single game. The existing request and download framework is left
// untouched.
class Scraper
{
public:
    enum ScraperMediaSource {
        Screenshot = 1,
        Video,
        Marquee,
        Box2d,
        Box3d,
        FanArt,
        TitleShot,
        Manual,
        Wheel,
        BoxBack,
        PhysicalMedia
    };

    // The keys are the identifiers stored in the Scraper setting and are kept lowercase for
    // backward compatibility with existing configuration files.
    static std::vector<std::pair<std::string, Scraper*>> scrapers;

    static Scraper* getScraper(const std::string& name = "");
    static std::string getScraperName(Scraper* scraper);
    static std::vector<std::string> getScraperList();
    static int getScraperIndex(const std::string& name);

    virtual ~Scraper() = default;

    // Display name of the scraping source. This is an Exia extension, the name is provided
    // by the scraper registry in Batocera where the base class has no such method.
    virtual const std::string getName() const = 0;

    virtual bool isSupportedPlatform(SystemData* system) const = 0;
    virtual const std::set<ScraperMediaSource>& getSupportedMedias() const = 0;

    // Whether the source has all the credentials it requires (e.g. IGDB and HfsDB).
    virtual bool isConfigured() const { return true; }

    virtual int getThreadCount(std::string& result) { return 1; }

    virtual bool hasMissingMedia(FileData* file);
    virtual bool hasAnyMedia(FileData* file);

    bool isMediaSupported(const ScraperMediaSource& media);

    std::unique_ptr<ScraperSearchHandle> search(const ScraperSearchParams& params);

protected:
    virtual void generateRequests(const ScraperSearchParams& params,
                                  std::queue<std::unique_ptr<ScraperRequest>>& requests,
                                  std::vector<ScraperSearchResult>& results) = 0;
};

// Will use the current scraper settings to pick the result source.
std::unique_ptr<ScraperSearchHandle> startScraperSearch(const ScraperSearchParams& params);

std::unique_ptr<ScraperSearchHandle> startMediaURLsFetch(const std::string& gameIDs);

// Returns true if the scraper configured in the settings is still valid.
bool isValidConfiguredScraper();

// Returns true if at least one of the media types enabled for scraping is missing for this
// game. Returns false if no media types are enabled at all.
bool hasMissingEnabledMedia(FileData* game);

// Returns true if at least one of the media types enabled for scraping exists for this game.
// Returns true if no media types are enabled at all.
bool hasAnyEnabledMedia(FileData* game);

using generate_scraper_requests_func =
    void (*)(const ScraperSearchParams& params,
             std::queue<std::unique_ptr<ScraperRequest>>& requests,
             std::vector<ScraperSearchResult>& results);

// Returns true if this game should be included in the scrape queue.
using GameFilterFunc = std::function<bool(SystemData*, FileData*)>;

// A game filter as shown in the batch scraping menu.
struct ScrapeFilter {
    // Untranslated name, used by the command line scraping entry.
    std::string name;
    // Localized label shown in the menu.
    std::string label;
    GameFilterFunc func;
};

// The scrape filters shown in the batch scraping menu. The order matches the ScraperFilter
// setting, so entries must not be reordered without adjusting the stored values.
const std::vector<ScrapeFilter>& getScrapeFilters();

// Whether the most recently modified scraped media file of this game is newer than the
// provided number of days. A value of zero or less disables the check.
bool isGameRecentlyUpdated(FileData* game, int skipRecentDays);

// Builds a scrape queue for the provided systems. This contains no GUI dependency so it can
// be shared by the batch scraping menu and the command line scraping entry.
std::pair<std::queue<ScraperSearchParams>, std::map<SystemData*, int>>
buildScrapeQueue(const std::vector<SystemData*>& systems,
                 const GameFilterFunc& selector,
                 bool overwriteMedia,
                 int skipRecentDays);

// -------------------------------------------------------------------------

// Meta data asset downloading stuff.
class MDResolveHandle : public AsyncHandle
{
public:
    MDResolveHandle(const ScraperSearchResult& result, const ScraperSearchParams& search);

    void update() override;
    const ScraperSearchResult& getResult() const
    {
        assert(mStatus == ASYNC_DONE);
        return mResult;
    }
    bool getSavedNewMedia() { return mResult.savedNewMedia; }

private:
    ScraperSearchResult mResult;

    using ResolvePair = std::pair<std::unique_ptr<AsyncHandle>, std::function<void()>>;
    std::vector<ResolvePair> mFuncs;
};

class MediaDownloadHandle : public AsyncHandle
{
public:
    MediaDownloadHandle(const std::string& url,
                        const std::string& path,
                        const std::string& existingMediaPath,
                        const std::string& mediaType,
                        const bool resizeFile,
                        bool& savedNewMedia);

    void update() override;

private:
    std::unique_ptr<HttpReq> mReq;
    std::string mSavePath;
    std::string mExistingMediaFile;
    std::string mMediaType;
    bool mResizeFile;
    bool* mSavedNewMediaPtr;
};

// Downloads media using this subdirectory structure:
// <application data>/downloaded_media/<system_name>/<media_type>/<game_name>.<file_extension>
// The subdirectories are automatically created if they do not exist.
std::string getSaveAsPath(const ScraperSearchParams& params,
                          const std::string& filetypeSubdirectory,
                          const std::string& url);

std::unique_ptr<MediaDownloadHandle> downloadMediaAsync(const std::string& url,
                                                        const std::string& saveAs,
                                                        const std::string& existingMediaPath,
                                                        const std::string& mediaType,
                                                        const bool resizeFile,
                                                        bool& savedNewMedia);

// Resolves all metadata assets that need to be downloaded.
std::unique_ptr<MDResolveHandle> resolveMetaDataAssets(const ScraperSearchResult& result,
                                                       const ScraperSearchParams& search);

bool resizeImage(const std::string& path, const std::string& mediaType);

#endif // ES_APP_SCRAPERS_SCRAPER_H
