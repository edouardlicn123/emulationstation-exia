//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  PlatformSettings.h
//
//  Per platform settings that live in the central configuration file ~/.config/emu/exia.conf
//  rather than in es_settings.xml, because they are keyed by the platform name:
//
//    system.<name>.windowed   0 = fullscreen (default), 1 = windowed
//    system.<name>.hidden     0 = visible (default), 1 = hidden from the system carousel
//
//  The default emulator is deliberately not stored here, it keeps using the alternativeEmulator
//  entry of the platform gamelist so that it stays compatible with the per game emulator
//  selection and with the batocera side.
//
//  The windowed mode is implemented by appending arguments to the launch command, so a table
//  maps the emulator token of the command to the arguments it needs. Adding another emulator
//  later means one more table entry and one more branch, the user interface stays untouched.
//
//  This is an Exia (EndeavourOS game station) local addition.
//

#ifndef ES_APP_GUIS_PLATFORM_SETTINGS_H
#define ES_APP_GUIS_PLATFORM_SETTINGS_H

#include <string>
#include <vector>

class SystemData;

namespace PlatformSettings
{
    // Which emulator a launch command belongs to. Anything that is not in the table is
    // reported as UNSUPPORTED so the user interface can disable the display mode row.
    enum class WindowBackend {
        UNSUPPORTED,
        RETROARCH,
        MAME
    };

    // Key part of the two per platform settings, the full key is "system.<name>.<key>".
    constexpr const char* KEY_WINDOWED {"windowed"};
    constexpr const char* KEY_HIDDEN {"hidden"};

    bool isWindowed(const std::string& systemName);
    void setWindowed(const std::string& systemName, bool windowed);

    bool isHidden(const std::string& systemName);
    void setHidden(const std::string& systemName, bool hidden);

    // Number of real (non collection) systems that are currently not hidden.
    int visibleSystemCount();

    // The first visible real system, falling back to the first entry of sSystemVector if every
    // system is hidden. Never returns nullptr unless sSystemVector itself is empty.
    SystemData* firstVisibleSystem();

    // True if the system is a real system and is not hidden.
    bool isSystemVisible(const SystemData* system);

    // Token of the given launch command, i.e. the "%EMULATOR_..." or "%CORE_..." entry, or an
    // empty string if the command does not contain one.
    WindowBackend windowBackendOf(const std::string& launchCommand);
    WindowBackend windowBackendOfSystem(const SystemData* system);

    // Arguments appended to the launch command for windowed mode. Empty for UNSUPPORTED and
    // for fullscreen.
    std::string windowedArguments(WindowBackend backend);

    // Absolute path of the RetroArch append configuration used for windowed mode. The file is
    // created on demand, an existing file is left alone.
    std::string retroarchWindowedConfigPath();
    bool ensureRetroarchWindowedConfig();

    // Appends the windowed arguments to a fully expanded launch command. Returns the command
    // unchanged when the system is in fullscreen mode. When the backend is unsupported the
    // command is returned unchanged and a warning is logged once.
    std::string applyWindowedMode(const std::string& command,
                                  const std::string& systemName,
                                  const SystemData* system);

    // Clears the windowed and hidden entries of the platform. Returns true if something was
    // written.
    bool reset(const std::string& systemName);
} // namespace PlatformSettings

#endif // ES_APP_GUIS_PLATFORM_SETTINGS_H
