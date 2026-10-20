//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiPlatformSettings.h
//
//  Per platform settings page, reachable from the main menu as PLATFORM SETTINGS.
//
//  Two levels: a list of the real (non collection) systems, and a page with the settings of a
//  single platform.
//
//    PLATFORM SETTINGS        list of systems, the hidden ones are marked with [HIDDEN]
//    <platform>               DEFAULT EMULATOR, DISPLAY MODE, SHOW PLATFORM, the read only
//                             system information and RESET PLATFORM SETTINGS
//
//  Hidden platforms are always listed, otherwise hiding one would make it unreachable.
//  The values themselves live in exia.conf, see PlatformSettings.h.
//
//  This is an Exia (EndeavourOS game station) local addition.
//

#ifndef ES_APP_GUIS_GUI_PLATFORM_SETTINGS_H
#define ES_APP_GUIS_GUI_PLATFORM_SETTINGS_H

#include "guis/GuiSettings.h"

#include <string>

// The platform list. The per platform page is built by a helper in the source file and opened
// through the row handlers, so it is not part of this interface.
class GuiPlatformSettings : public GuiSettings
{
public:
    explicit GuiPlatformSettings(const std::string& title);
};

#endif // ES_APP_GUIS_GUI_PLATFORM_SETTINGS_H
