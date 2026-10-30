//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MenuComponent.cpp
//
//  Basic component for building a menu.
//

#include "components/MenuComponent.h"

#include "MenuFontUtil.h"
#include "Settings.h"
#include "ThemeData.h"
#include "components/ButtonComponent.h"

#define BUTTON_GRID_VERT_PADDING menuLabelFont()->getLetterHeight() * 0.915f
#define BUTTON_GRID_HORIZ_PADDING menuLabelFont()->getLetterHeight() * 0.283f

#define TITLE_HEIGHT                                                                               \
    (mTitle->getFont()->getLetterHeight() + (Renderer::getIsVerticalOrientation() ?                \
                                                 Renderer::getScreenWidth() * 0.0637f :            \
                                                 Renderer::getScreenHeight() * 0.0637f))

MenuComponent::MenuComponent(std::string title, const std::shared_ptr<Font>& titleFont)
    : mRenderer {Renderer::getInstance()}
    , mGrid {glm::ivec2 {2, 4}}
    , mNeedsSaving {false}
{
    // Apply the corner size from the theme if the "menu" view defines one.
    if (const auto& menuTheme {ThemeData::getMenuTheme()}; menuTheme->loadedFromTheme)
        mBackground.setCornerSize(menuTheme->Background.cornerSize);

    addChild(&mBackground);
    addChild(&mGrid);

    // Set up title.
    mTitle = std::make_shared<TextComponent>();
    mTitle->setAutoCalcExtent(glm::ivec2 {0, 0});
    mTitle->setHorizontalAlignment(ALIGN_CENTER);
    mTitle->setColor(mMenuColorTitle);
    if (const auto& menuTheme {ThemeData::getMenuTheme()};
        menuTheme->loadedFromTheme && menuTheme->Title.hasFont())
        mTitle->setFont(menuTheme->getFont(menuTheme->Title, FONT_SIZE_LARGE));
    setTitle(title, titleFont);
    mGrid.setEntry(mTitle, glm::ivec2 {0, 0}, false, true, glm::ivec2 {2, 2});

    // Set up list which will never change (externally, anyway).
    mList = std::make_shared<ComponentList>();
    mGrid.setEntry(mList, glm::ivec2 {0, 2}, true, true, glm::ivec2 {2, 1});

    // Set up scroll indicators.
    mScrollUp = std::make_shared<ImageComponent>();
    mScrollDown = std::make_shared<ImageComponent>();

    mScrollUp->setResize(0.0f, mTitle->getFont()->getLetterHeight() / 2.0f);
    mScrollUp->setOrigin(0.0f, -0.35f);

    mScrollDown->setResize(0.0f, mTitle->getFont()->getLetterHeight() / 2.0f);
    mScrollDown->setOrigin(0.0f, 0.35f);

    mScrollIndicator = std::make_shared<ScrollIndicatorComponent>(mList, mScrollUp, mScrollDown);

    // Initialize help panel members (hidden by default until refreshHelp creates them).
    mHelpBackground = std::make_shared<BackgroundComponent>();
    mHelpTitle = std::make_shared<TextComponent>();
    mHelpBody = std::make_shared<TextComponent>();

    addChild(mHelpBackground.get());
    addChild(mHelpTitle.get());
    addChild(mHelpBody.get());

    mGrid.setEntry(mScrollUp, glm::ivec2 {1, 0}, false, false, glm::ivec2 {1, 1});
    mGrid.setEntry(mScrollDown, glm::ivec2 {1, 1}, false, false, glm::ivec2 {1, 1});

    updateGrid();
    updateSize();

    mGrid.resetCursor();

    // Register cursor changed callback for help panel.
    mList->setCursorChangedCallback([this](CursorState) { refreshHelp(); });

    // Initial help refresh after focus.
    refreshHelp();
}

MenuComponent::~MenuComponent()
{
    // Save when destroyed.
    save();
}

void MenuComponent::save()
{
    if (!mSaveFuncs.size())
        return;

    for (auto it = mSaveFuncs.cbegin(); it != mSaveFuncs.cend(); ++it)
        (*it)();

    if (mNeedsSaving) {
        Settings::getInstance()->saveFile();
        mNeedsSaving = false;
    }
}

void MenuComponent::setTitle(std::string title, const std::shared_ptr<Font>& font)
{
    mTitle->setText(title);
    mTitle->setFont(font);
}

float MenuComponent::getButtonGridHeight() const
{
    return (mButtonGrid ? mButtonGrid->getSize().y :
                          menuLabelFont()->getSize() * 1.5f + BUTTON_GRID_VERT_PADDING);
}

void MenuComponent::updateSize()
{
    const float maxHeight {mRenderer->getScreenHeight() *
                           (mRenderer->getIsVerticalOrientation() ? 0.70f : 0.80f)};
    float height {TITLE_HEIGHT + mList->getTotalRowHeight() + getButtonGridHeight() +
                  (2.0f * mRenderer->getScreenResolutionModifier())};
    if (height > maxHeight) {
        height = TITLE_HEIGHT + getButtonGridHeight();
        int i {0};
        while (i < mList->size()) {
            // Add the separator height to the row height so that it also gets properly rendered.
            float rowHeight {mList->getRowHeight() + mRenderer->getScreenResolutionModifier()};
            if (height + rowHeight < maxHeight)
                height += rowHeight;
            else
                break;
            ++i;
        }
    }

    // Exia: reduce the menu width by 25% compared to the upstream default (devdoc/30 item 4).
    float width {std::min(mRenderer->getScreenHeight() * 1.05f,
                          mRenderer->getScreenWidth() *
                              (mRenderer->getIsVerticalOrientation() ? 0.94f : 0.90f)) *
                 0.75f};

    setSize(width, height);
    refreshHelp();
}

void MenuComponent::onSizeChanged()
{
    mBackground.fitTo(mSize);

    // Update grid row/column sizes.
    mGrid.setRowHeightPerc(0, TITLE_HEIGHT / mSize.y / 2.0f);
    mGrid.setRowHeightPerc(1, TITLE_HEIGHT / mSize.y / 2.0f);
    mGrid.setRowHeightPerc(3, getButtonGridHeight() / mSize.y);

    mGrid.setColWidthPerc(1, 0.055f);

    mGrid.setSize(mSize);

    // Limit the title size to reserve space for the scroll indicators.
    float indicatorsSize {mSize.x * 0.09f};

    glm::vec2 titleSize {mTitle->getSize()};
    mTitle->setSize(titleSize.x - indicatorsSize, titleSize.y);

    glm::vec3 titlePos {mTitle->getPosition()};
    mTitle->setPosition(titlePos.x + indicatorsSize / 2.0f, titlePos.y, titlePos.z);
}

void MenuComponent::addButton(const std::string& name,
                              const std::string& helpText,
                              const std::function<void()>& callback)
{
    mButtons.push_back(std::make_shared<ButtonComponent>(name, helpText, callback));
    updateGrid();
    updateSize();
}

void MenuComponent::updateGrid()
{
    if (mButtonGrid)
        mGrid.removeEntry(mButtonGrid);

    mButtonGrid.reset();

    if (mButtons.size()) {
        mButtonGrid = makeButtonGrid(mButtons);
        mGrid.setEntry(mButtonGrid, glm::ivec2 {0, 3}, true, false, glm::ivec2 {2, 1});
    }
}

std::shared_ptr<ComponentGrid> MenuComponent::makeButtonGrid(
    const std::vector<std::shared_ptr<ButtonComponent>>& buttons)
{
    std::shared_ptr<ComponentGrid> buttonGrid {
        std::make_shared<ComponentGrid>(glm::ivec2 {static_cast<int>(buttons.size()), 2})};

    // Initialize to padding.
    float buttonGridWidth {BUTTON_GRID_HORIZ_PADDING * buttons.size()};

    for (int i = 0; i < static_cast<int>(buttons.size()); ++i) {
        buttonGrid->setEntry(buttons.at(i), glm::ivec2 {i, 0}, true, false);
        buttonGridWidth += buttons.at(i)->getSize().x;
    }
    for (unsigned int i = 0; i < buttons.size(); ++i)
        buttonGrid->setColWidthPerc(i, (buttons.at(i)->getSize().x + BUTTON_GRID_HORIZ_PADDING) /
                                           buttonGridWidth);

    buttonGrid->setSize(buttonGridWidth,
                        buttons.at(0)->getSize().y + BUTTON_GRID_VERT_PADDING + 2.0f);
    // Spacer row to deal with dropshadow to make buttons look centered.
    buttonGrid->setRowHeightPerc(1, 2.0f / buttonGrid->getSize().y);

    return buttonGrid;
}

float MenuComponent::getMenuLeft()
{
    const Renderer* renderer {Renderer::getInstance()};
    return std::round(std::min(renderer->getScreenHeight() * 0.142f,
                               renderer->getScreenWidth() * 0.09f));
}

void MenuComponent::refreshHelp()
{
    const bool showHelp {Settings::getInstance()->getBool("MenuHelp")};
    const std::string helpStr {mList && mList->isFocused() ?
                                  mList->getRowHelpText(mList->getCursorId()) : ""};

    if (!showHelp || helpStr.empty()) {
        mHelpBackground->setOpacity(0.0f);
        mHelpTitle->setOpacity(0.0f);
        mHelpBody->setOpacity(0.0f);
        return;
    }

    mHelpTitle->setText(_("TIPS"));
    // Exia local change: a theme supplied menu font size overrides the default size passed to
    // getMenuThemeFont(), so scale the resolved font instead of its argument.
    const std::shared_ptr<Font> helpTitleFont {getMenuThemeFont(FONT_SIZE_MEDIUM)};
    mHelpTitle->setFont(Font::get(helpTitleFont->getSize() * 1.3f, helpTitleFont->getPath()));
    mHelpTitle->setColor(mMenuColorTitle);
    mHelpBody->setText(helpStr);
    mHelpBody->setFont(getMenuThemeFont(FONT_SIZE_MEDIUM));
    mHelpBody->setColor(mMenuColorPrimary);

    const float gap {std::round(mRenderer->getScreenHeight() * 0.02f)};
    const float panelWanted {getSize().x * 0.85f};
    const float available {mRenderer->getScreenWidth() * 0.98f - (getPosition().x + getSize().x + gap)};
    float panelW {std::min(panelWanted, std::max(available, 0.0f))};

    if (showHelp && !helpStr.empty() && panelW >= panelWanted * 0.5f && panelW > 0) {
        // Background with square corners.
        mHelpBackground->setCornerSize(glm::vec2(0.0f, 0.0f));
        mHelpBackground->setPosition(getPosition().x + getSize().x + gap, getPosition().y);

        // Title with indent.
        const float titleCharW {mHelpTitle->getFont()->getLetterHeight() * 0.6f};
        const float titleIndent {titleCharW * 6.0f};
        mHelpTitle->setSize(panelW, mHelpTitle->getFont()->getLetterHeight());
        mHelpTitle->setPosition(getPosition().x + getSize().x + gap + titleIndent, getPosition().y);

        // Body text with wrap constraint.
        const float horizontalPadding {20.0f};
        const float bodyContentW {panelW - horizontalPadding};
        mHelpBody->setAutoCalcExtent(glm::ivec2 {0, 1});
        mHelpBody->setSize(bodyContentW, 0.0f);

        // Calculate adaptive height based on actual text size.
        const float titleH {mHelpTitle->getFont()->getLetterHeight()};
        const float bodyH {mHelpBody->getSize().y > 0.0f ? mHelpBody->getSize().y : titleH * 1.5f};
        const float verticalPadding {8.0f};
        const float maxPanelH {mRenderer->getScreenHeight() * 0.45f};
        float panelH {std::min(titleH + bodyH + verticalPadding, maxPanelH)};
        if (panelH < titleH + verticalPadding)
            panelH = titleH + verticalPadding;

        mHelpBackground->setSize(panelW, panelH);
        mHelpBody->setPosition(getPosition().x + getSize().x + gap + 10.0f,
                                getPosition().y + titleH + verticalPadding / 2.0f);

        mHelpBackground->setOpacity(1.0f);
        mHelpTitle->setOpacity(1.0f);
        mHelpBody->setOpacity(1.0f);
    } else {
        mHelpBackground->setOpacity(0.0f);
        mHelpTitle->setOpacity(0.0f);
        mHelpBody->setOpacity(0.0f);
    }
}

bool MenuComponent::input(InputConfig* config, Input input)
{
    // Input callbacks of the rows and buttons dispatch from here, and they may close and delete
    // the Gui that owns this menu component (this menu is typically one of its members). Resolve
    // the owning Gui before dispatching and only touch this object again if it is still shown.
    Window* const window {mWindow};
    GuiComponent* const shownGui {window != nullptr ? window->peekGui() : nullptr};
    GuiComponent* owner {this};
    while (owner->getParent() != nullptr)
        owner = owner->getParent();

    const bool handled {GuiComponent::input(config, input)};

    if (window != nullptr && window->peekGui() == shownGui && shownGui == owner)
        refreshHelp();
    return handled;
}

std::shared_ptr<ImageComponent> MenuComponent::makeArrow()
{
    auto bracket = std::make_shared<ImageComponent>();
    bracket->setLinearInterpolation(true);
    bracket->setResize(0, menuLabelFont()->getLetterHeight());
    bracket->setImage(":/graphics/arrow.svg");
    bracket->setColorShift(mMenuColorPrimary);
    return bracket;
}
