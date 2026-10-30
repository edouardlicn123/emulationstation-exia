//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  ScraperSearchComponent.cpp
//
//  Game search for the single-game scraper. Queries all enabled scraping
//  sources one after the other and shows the collected results together
//  with a preview of the selected entry.
//

#include "components/ScraperSearchComponent.h"
#include "MenuFontUtil.h"

#include "Log.h"
#include "Settings.h"
#include "components/ComponentList.h"
#include "components/ImageComponent.h"
#include "components/ScrollableContainer.h"
#include "components/TextComponent.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>

namespace
{
    // Total time budget for querying all sources.
    const int SEARCH_TIMEOUT {60000};

    // The disabled sources are stored as a semicolon separated list.
    std::vector<std::string> splitList(const std::string& list)
    {
        std::vector<std::string> entries;
        std::string entry;

        for (const char character : list) {
            if (character == ';') {
                if (!entry.empty())
                    entries.push_back(entry);
                entry.clear();
            }
            else {
                entry.push_back(character);
            }
        }

        if (!entry.empty())
            entries.push_back(entry);

        return entries;
    }
} // namespace

ScraperSearchComponent::ScraperSearchComponent(Window*, const ScraperSearchParams& params)
    : mGrid {glm::ivec2 {2, 1}}
    , mParams {params}
    , mCurrentSource {0}
    , mElapsed {0}
    , mDone {false}
    , mCreatedRequests {false}
{
    addChild(&mGrid);

    // Result list.
    mResultList = std::make_shared<ComponentList>();
    mResultList->setCursorChangedCallback([this](CursorState state) {
        if (state == CursorState::CURSOR_STOPPED)
            updatePreview();
    });
    mGrid.setEntry(mResultList, glm::ivec2 {0, 0}, true, true);

    // Preview pane.
    mPreviewGrid = std::make_shared<ComponentGrid>(glm::ivec2 {1, 3});
    mPreviewGrid->setRowHeightPerc(0, 0.55f, false);
    mPreviewGrid->setRowHeightPerc(1, 0.09f, false);

    mResultThumbnail = std::make_shared<ImageComponent>();
    mPreviewGrid->setEntry(mResultThumbnail, glm::ivec2 {0, 0}, false, false);

    mResultName = std::make_shared<TextComponent>("", menuValueFont(), mMenuColorPrimary);
    mPreviewGrid->setEntry(mResultName, glm::ivec2 {0, 1}, false, true);

    mDescContainer = std::make_shared<ScrollableContainer>();
    mDescContainer->setScrollParameters(3000.0f, 3000.0f, 0.8f);

    mResultDesc = std::make_shared<TextComponent>("", menuValueFont(), mMenuColorPrimary, ALIGN_LEFT,
                                                  ALIGN_TOP, glm::ivec2 {0, 1});
    mDescContainer->addChild(mResultDesc.get());
    // setAutoScroll() calls resetComponent() which dereferences the first child, so it has to be
    // called after the child has been added.
    mDescContainer->setAutoScroll(true);
    mPreviewGrid->setEntry(mDescContainer, glm::ivec2 {0, 2}, false, true);

    mGrid.setEntry(mPreviewGrid, glm::ivec2 {1, 0}, false, true);
}

ScraperSearchComponent::~ScraperSearchComponent()
{
    // The manual resets are required to avoid a race condition when the GUI is closed
    // while a search or a thumbnail download is still in progress, as the curl easy
    // handles would otherwise not be cleaned up.
    if (mSearchHandle)
        mSearchHandle.reset();

    if (mThumbnailReqMap.size() > 0)
        mThumbnailReqMap.clear();

    if (mCreatedRequests)
        HttpReq::cleanupCurlMulti();
}

void ScraperSearchComponent::onSizeChanged()
{
    if (mSize.x == 0.0f || mSize.y == 0.0f)
        return;

    mGrid.setColWidthPerc(0, 0.42f, false);
    mGrid.setColWidthPerc(1, 0.58f, false);
    mGrid.setSize(mSize);
    mGrid.onSizeChanged();

    mResultThumbnail->setMaxSize(mPreviewGrid->getColWidth(0) * 0.93f,
                                 mPreviewGrid->getRowHeight(0) * 0.93f);
    mPreviewGrid->onSizeChanged();

    mResultDesc->setSize(mDescContainer->getSize().x, 0.0f);
}

void ScraperSearchComponent::startSearch(const std::string& nameOverride)
{
    mSearchHandle.reset();
    mThumbnailReqMap.clear();
    mResults.clear();
    mSources.clear();
    mCurrentSource = 0;
    mDone = false;
    mElapsed = 0;
    mParams.nameOverride = nameOverride;

    mResultList->clear();
    clearPreview();

    const std::vector<std::string> disabledScrapers {
        splitList(Settings::getInstance()->getString("DisabledManualScrapers"))};

    for (auto& scraperEntry : Scraper::scrapers) {
        Scraper* scraper {scraperEntry.second};

        if (!scraper->isConfigured())
            continue;

        if (!scraper->isSupportedPlatform(mParams.system))
            continue;

        if (std::find(disabledScrapers.cbegin(), disabledScrapers.cend(), scraperEntry.first) !=
            disabledScrapers.cend())
            continue;

        mSources.push_back(scraperEntry.first);
    }

    startNextSource();
}

void ScraperSearchComponent::startNextSource()
{
    mSearchHandle.reset();

    while (mCurrentSource < mSources.size()) {
        const std::string sourceId {mSources[mCurrentSource]};
        ++mCurrentSource;

        Scraper* scraper {Scraper::getScraper(sourceId)};
        if (scraper == nullptr)
            continue;

        ScraperSearchParams params;
        params.system = mParams.system;
        params.game = mParams.game;
        params.nameOverride = mParams.nameOverride;
        params.overwriteMedia = mParams.overwriteMedia;
        params.scraperName = sourceId;

        mActiveSourceId = sourceId;
        mActiveSourceName = Utils::String::toUpper(scraper->getName());
        mSearchHandle = scraper->search(params);

        // Without any result yet there is nothing in the list and nothing in the preview, so
        // the active source is shown here to give some feedback while the search is running.
        if (mResults.empty()) {
            mResultName->setText(_("SEARCHING") + " " + mActiveSourceName);
            mPreviewGrid->onSizeChanged();
        }
        return;
    }

    finishSearch();
}

void ScraperSearchComponent::finishSearch()
{
    mSearchHandle.reset();
    mDone = true;

    if (mResults.empty()) {
        ComponentListRow row;
        row.addElement(
            std::make_shared<TextComponent>(_("NO RESULTS"), menuValueFont(), mMenuColorPrimary),
            true);
        mResultList->addRow(row);
    }

    updatePreview();
}

void ScraperSearchComponent::addResults(const std::vector<ScraperSearchResult>& results)
{
    int added {0};

    for (const ScraperSearchResult& result : results) {
        // Only a limited number of entries per source is shown, otherwise one service could fill
        // the whole list and hide the results of the other sources.
        if (added >= MAX_SCRAPER_RESULTS)
            break;

        ResultEntry entry;
        entry.result = result;
        entry.sourceId = mActiveSourceId;
        entry.sourceName = mActiveSourceName;
        mResults.push_back(entry);

        std::string rowText {"[" + mActiveSourceName + "] " + result.mdl.get("name")};
        const std::string releaseDate {result.mdl.get("releasedate")};

        if (releaseDate.size() >= 4 && releaseDate != "19700101T000000")
            rowText += " (" + releaseDate.substr(0, 4) + ")";

        ComponentListRow row;
        row.addElement(
            std::make_shared<TextComponent>(rowText, menuValueFont(), mMenuColorPrimary), true);
        mResultList->addRow(row);

        ++added;
    }

    updatePreview();
}

void ScraperSearchComponent::update(int deltaTime)
{
    GuiComponent::update(deltaTime);

    updateThumbnails();

    if (mDone)
        return;

    mElapsed += deltaTime;

    if (mSearchHandle != nullptr) {
        const AsyncHandleStatus status {mSearchHandle->status()};

        if (status == ASYNC_DONE) {
            const std::vector<ScraperSearchResult> results {mSearchHandle->getResults()};
            mSearchHandle.reset();
            addResults(results);
            startNextSource();
        }
        else if (status == ASYNC_ERROR) {
            LOG(LogError) << "ScraperSearchComponent: Scraper \"" << mActiveSourceId
                          << "\" failed: "
                          << Utils::String::replace(mSearchHandle->getStatusString(), "\n", "");
            mSearchHandle.reset();
            startNextSource();
        }
    }

    if (!mDone && mElapsed >= SEARCH_TIMEOUT) {
        LOG(LogWarning) << "ScraperSearchComponent: Search timed out with " << mResults.size()
                        << " collected results";
        finishSearch();
    }
}

void ScraperSearchComponent::updateThumbnails()
{
    for (auto it = mThumbnailReqMap.begin(); it != mThumbnailReqMap.end();) {
        const HttpReq::Status status {it->second->status()};

        if (status == HttpReq::REQ_IN_PROGRESS) {
            ++it;
            continue;
        }

        if (status == HttpReq::REQ_SUCCESS) {
            std::string content {it->second->getContent()};

            for (auto& entry : mResults) {
                if (entry.result.thumbnailImageUrl != it->first)
                    continue;

                entry.result.thumbnailImageData = content;
                entry.result.thumbnailDownloadStatus = COMPLETED;
            }

            ResultEntry* selected {getSelectedEntry()};

            if (selected != nullptr && selected->result.thumbnailImageUrl == it->first &&
                content.size() > 350) {
                mResultThumbnail->setImage(content.data(), content.length());
                mPreviewGrid->onSizeChanged();
            }
        }
        else {
            LOG(LogWarning) << "ScraperSearchComponent: Error downloading thumbnail: "
                            << it->second->getErrorMsg();
        }

        it = mThumbnailReqMap.erase(it);
    }
}

void ScraperSearchComponent::updatePreview()
{
    ResultEntry* entry {getSelectedEntry()};

    if (entry == nullptr) {
        clearPreview();
        return;
    }

    ScraperSearchResult& result {entry->result};

    mResultName->setText(Utils::String::toUpper(result.mdl.get("name")));

    std::string desc {result.mdl.get("desc")};

    if (desc.size() > 300) {
        desc.erase(300);
        desc += "...";
    }

    mResultDesc->setText(desc);
    mDescContainer->resetComponent();

    mResultThumbnail->setImage("");

    if (!result.thumbnailImageData.empty()) {
        std::string content {result.thumbnailImageData};
        mResultThumbnail->setImage(content.data(), content.length());
    }
    else if (!result.thumbnailImageUrl.empty() &&
             result.thumbnailDownloadStatus != IN_PROGRESS &&
             mThumbnailReqMap.find(result.thumbnailImageUrl) == mThumbnailReqMap.end()) {
        result.thumbnailDownloadStatus = IN_PROGRESS;
        mCreatedRequests = true;
        mThumbnailReqMap.insert(std::pair<std::string, std::unique_ptr<HttpReq>>(
            result.thumbnailImageUrl, std::unique_ptr<HttpReq>(new HttpReq(result.thumbnailImageUrl,
                                                                          true))));
    }

    mPreviewGrid->onSizeChanged();
}

void ScraperSearchComponent::clearPreview()
{
    mResultName->setText("");
    mResultDesc->setText("");
    mDescContainer->resetComponent();
    mResultThumbnail->setImage("");
    mPreviewGrid->onSizeChanged();
}

ScraperSearchComponent::ResultEntry* ScraperSearchComponent::getSelectedEntry()
{
    if (mResults.empty())
        return nullptr;

    const int cursor {mResultList->getCursorId()};

    if (cursor < 0 || static_cast<size_t>(cursor) >= mResults.size())
        return nullptr;

    return &mResults[cursor];
}

const ScraperSearchResult* ScraperSearchComponent::getSelectedResult(std::string& sourceId) const
{
    if (mResults.empty())
        return nullptr;

    const int cursor {mResultList->getCursorId()};

    if (cursor < 0 || static_cast<size_t>(cursor) >= mResults.size())
        return nullptr;

    sourceId = mResults[cursor].sourceId;
    return &mResults[cursor].result;
}
