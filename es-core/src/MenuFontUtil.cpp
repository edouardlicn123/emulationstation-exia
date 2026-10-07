//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MenuFontUtil.cpp
//
//  Helper for applying the font from the theme's "menu" view to menu text.
//

#include "MenuFontUtil.h"

#include "ThemeData.h"
#include "resources/Font.h"
#include "utils/LocalizationUtil.h"

std::shared_ptr<Font> getMenuThemeFont(float defaultSize)
{
    const auto& menuTheme {ThemeData::getMenuTheme()};

    if (menuTheme->loadedFromTheme && menuTheme->Text.hasFont())
        return menuTheme->getFont(menuTheme->Text, defaultSize);

    // Same computation as ThemeData::ThemeMenu::getFont() takes for its own fallback branch,
    // the user scale included so the menu.font.scale setting behaves identically with and
    // without a theme supplied menu font (devdoc/15 §6.6).
    return Font::get(defaultSize * Utils::Localization::sMenuTitleScaleFactor * getUserFontScale());
}

std::shared_ptr<Font> getMenuThemeSmallFont(float defaultSize)
{
    const auto& menuTheme {ThemeData::getMenuTheme()};

    if (menuTheme->loadedFromTheme && menuTheme->TextSmall.hasFont())
        return menuTheme->getFont(menuTheme->TextSmall, defaultSize);

    return getMenuThemeFont(defaultSize);
}

bool hasMenuThemeFont()
{
    const auto& menuTheme {ThemeData::getMenuTheme()};
    return menuTheme->loadedFromTheme && menuTheme->Text.hasFont();
}

namespace {
// Exia local addition, see MenuFontUtil.h. 1.0 means no scaling, the upstream behavior.
float sUserFontScale {1.0f};
} // namespace

void setUserFontScale(float scale)
{
    if (scale < 0.5f)
        scale = 0.5f;
    if (scale > 2.0f)
        scale = 2.0f;
    sUserFontScale = scale;
}

float getUserFontScale()
{
    return sUserFontScale;
}

std::shared_ptr<Font> menuLabelFont()
{
    return getMenuThemeFont(FONT_SIZE_MEDIUM);
}

std::shared_ptr<Font> menuValueFont(const std::string& path)
{
    // An empty path means the default face, which is what the plain text values used before
    // the unification. Font::get() caches per (size, path), so the per call site weights
    // coexist without any extra bookkeeping here.
    return Font::get(FONT_SIZE_SMALL * sUserFontScale,
                     path.empty() ? Font::getDefaultPath() : path);
}
