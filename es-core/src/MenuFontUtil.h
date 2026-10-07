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

// Exia local addition: the two unified font entry points for menu text (see devdoc/15 §6.5).
// Everything visible in a menu row derives from one of these so that labels, values and
// buttons always come from the same two sizes, and so that menu.font.scale covers both.
// menuLabelFont() is for labels, section headers and all button text (page titles and dialog
// body text are excluded, they keep their upstream fonts). menuValueFont() is for in-row
// values, text edit contents, slider readouts, switch icons and the help bar. The optional
// path keeps the original font weight of each call site (option list, text edit and slider
// values are drawn with the regular Akrobat face, alternatives with the bold one), so only
// the size changes and the weights stay exactly as they were before the unification.
std::shared_ptr<Font> menuLabelFont();
std::shared_ptr<Font> menuValueFont(const std::string& path = "");

#endif // ES_CORE_MENU_FONT_UTIL_H
