//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MenuFontUtil.h
//
//  Helper for applying the font from the theme's "menu" view to menu text.
//

#ifndef ES_CORE_MENU_FONT_UTIL_H
#define ES_CORE_MENU_FONT_UTIL_H

#include <memory>
#include <string>

class Font;

// Returns the font to use for menu text. If the theme defines a font in its "menu" view that
// font is returned, otherwise the default font for the given size is returned. This makes it
// possible for the menus to follow the theme without every menu having to know about the theme.
std::shared_ptr<Font> getMenuThemeFont(float defaultSize);

// Returns true if the theme defines a font for menu text.
bool hasMenuThemeFont();

// Returns true if the theme defines a font for the menu's small text (used for labels and
// other secondary text). Falls back to the regular menu font when not defined.
std::shared_ptr<Font> getMenuThemeSmallFont(float defaultSize);

#endif // ES_CORE_MENU_FONT_UTIL_H
