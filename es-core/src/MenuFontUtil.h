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

// Exia local addition: user controlled scale factor for menu row text, stored as
// menu.font.scale in ~/.config/emu/exia.conf and read once during startup. It is applied to
// the resolved size inside ThemeData::ThemeMenu::getFont() so that every menu font passes
// through it. A value of 1.0 keeps the upstream behavior unchanged.
void setUserFontScale(float scale);
float getUserFontScale();

#endif // ES_CORE_MENU_FONT_UTIL_H
