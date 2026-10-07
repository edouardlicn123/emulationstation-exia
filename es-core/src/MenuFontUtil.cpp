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

    return Font::get(defaultSize * Utils::Localization::sMenuTitleScaleFactor);
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
