//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ScraperSearchComponent.h
//
//  Game search for the single-game scraper. Queries all enabled scraping
//  sources one after the other and shows the collected results together
//  with a preview of the selected entry.
//

#ifndef ES_APP_COMPONENTS_SCRAPER_SEARCH_COMPONENT_H
#define ES_APP_COMPONENTS_SCRAPER_SEARCH_COMPONENT_H

#include "GuiComponent.h"
#include "components/ComponentGrid.h"
#include "scrapers/Scraper.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

class ComponentList;
class ImageComponent;
class ScrollableContainer;
class TextComponent;

class ScraperSearchComponent : public GuiComponent
{
public:
    struct ResultEntry {
        ScraperSearchResult result;
        std::string sourceId;
        std::string sourceName;
    };

    ScraperSearchComponent(Window* window, const ScraperSearchParams& params);
    ~ScraperSearchComponent();

    // Restarts the search and queues all sources that are enabled for this platform.
    void startSearch(const std::string& nameOverride = "");

    void update(int deltaTime) override;
    void onSizeChanged() override;

    bool isDone() const { return mDone; }
    bool hasResults() const { return !mResults.empty(); }
    const ScraperSearchResult* getSelectedResult(std::string& sourceId) const;

    const std::string& getNameOverride() const { return mParams.nameOverride; }
    const ScraperSearchParams& getParams() const { return mParams; }

private:
    ResultEntry* getSelectedEntry();
    void startNextSource();
    void finishSearch();
    void addResults(const std::vector<ScraperSearchResult>& results);
    void updatePreview();
    void clearPreview();
    void updateThumbnails();

    ComponentGrid mGrid;
    std::shared_ptr<ComponentGrid> mPreviewGrid;
    std::shared_ptr<ComponentList> mResultList;
    std::shared_ptr<TextComponent> mResultName;
    std::shared_ptr<ScrollableContainer> mDescContainer;
    std::shared_ptr<TextComponent> mResultDesc;
    std::shared_ptr<ImageComponent> mResultThumbnail;

    ScraperSearchParams mParams;
    std::vector<std::string> mSources;
    size_t mCurrentSource;
    std::string mActiveSourceId;
    std::string mActiveSourceName;
    std::unique_ptr<ScraperSearchHandle> mSearchHandle;
    std::vector<ResultEntry> mResults;
    std::map<std::string, std::unique_ptr<HttpReq>> mThumbnailReqMap;

    int mElapsed;
    bool mDone;
    bool mCreatedRequests;
};

#endif // ES_APP_COMPONENTS_SCRAPER_SEARCH_COMPONENT_H
