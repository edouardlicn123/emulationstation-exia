//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiExiaSettings.h
//
//  Exia specific settings page, reachable from the main menu as EXIA SETTINGS.
//
//  The page is backed by the central configuration file ~/.config/emu/exia.conf which uses
//  the batocera.conf syntax (section header comments plus key=value lines, split on the first
//  equals sign). That file is comment heavy documentation, so it must never be rewritten from
//  scratch, all writes therefore go through the ExiaConfig class below which patches single
//  lines and leaves every comment and section header untouched.
//
//  This is an Exia (EndeavourOS game station) local addition, the LightDM and systemd entries
//  are Linux only.
//

#ifndef ES_APP_GUIS_GUI_EXIA_SETTINGS_H
#define ES_APP_GUIS_GUI_EXIA_SETTINGS_H

#include "guis/GuiSettings.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// Comment preserving reader and writer for ~/.config/emu/exia.conf.
// The file is created from a default template if it is missing and is kept at mode 0600
// because it stores the sudo password.
class ExiaConfig
{
public:
    static ExiaConfig& getInstance();

    static std::string getPath();

    std::string get(const std::string& key, const std::string& def = "");
    bool set(const std::string& key, const std::string& value);

private:
    ExiaConfig() = default;
    bool load();
    bool save();

    std::vector<std::string> mLines;
    std::map<std::string, size_t> mIndex;
    bool mLoaded {false};
};

class GuiExiaSettings : public GuiSettings
{
public:
    explicit GuiExiaSettings(const std::string& title);

private:
    ComponentListRow makeNavRow(const std::string& label,
                                const std::string& value,
                                const std::function<void()>& func);
    ComponentListRow makeNoteRow(const std::string& text);

    void openBiosSwitch();
    void openBiosPlatform(const std::string& title,
                          const std::string& confKey,
                          const std::string& target,
                          const std::vector<std::pair<std::string, std::string>>& options);
    void openPaths();
    void openStatistics();
    void openGitProxy();
    void addPasswordRow();
};

#endif // ES_APP_GUIS_GUI_EXIA_SETTINGS_H
