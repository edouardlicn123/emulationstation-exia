//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  PlatformSettings.cpp
//
//  See PlatformSettings.h for the layout of the configuration keys and for the reason why the
//  windowed mode is implemented by appending launch arguments.
//

#include "guis/PlatformSettings.h"

#include "Log.h"
#include "SystemData.h"
#include "guis/GuiExiaSettings.h"
#include "utils/FileSystemUtil.h"

#include <fstream>

namespace
{
    std::string confKey(const std::string& systemName, const char* key)
    {
        return "system." + systemName + "." + key;
    }

    bool confBool(const std::string& systemName, const char* key)
    {
        // Absent means off, and so does anything that is not exactly "1", so a hand edited
        // file with a stray value can never put a platform into an unexpected state.
        return ExiaConfig::getInstance().get(confKey(systemName, key), "0") == "1";
    }

    void setConfBool(const std::string& systemName, const char* key, bool value)
    {
        ExiaConfig::getInstance().set(confKey(systemName, key), value ? "1" : "0");
    }
} // namespace

namespace PlatformSettings
{
    bool isWindowed(const std::string& systemName)
    {
        return confBool(systemName, KEY_WINDOWED);
    }

    void setWindowed(const std::string& systemName, bool windowed)
    {
        setConfBool(systemName, KEY_WINDOWED, windowed);
    }

    bool isHidden(const std::string& systemName)
    {
        return confBool(systemName, KEY_HIDDEN);
    }

    void setHidden(const std::string& systemName, bool hidden)
    {
        setConfBool(systemName, KEY_HIDDEN, hidden);
    }

    bool isSystemVisible(const SystemData* system)
    {
        if (system == nullptr || system->isCollection())
            return false;
        return !isHidden(system->getName());
    }

    int visibleSystemCount()
    {
        int count {0};
        for (SystemData* system : SystemData::sSystemVector) {
            if (isSystemVisible(system))
                ++count;
        }
        return count;
    }

    SystemData* firstVisibleSystem()
    {
        for (SystemData* system : SystemData::sSystemVector) {
            if (isSystemVisible(system))
                return system;
        }
        // Everything is hidden, the caller still needs a usable system. SystemView shows all
        // systems in this case as well so the two stay consistent.
        return SystemData::sSystemVector.empty() ? nullptr : SystemData::sSystemVector.front();
    }

    WindowBackend windowBackendOf(const std::string& launchCommand)
    {
        // The tokens are the ones used in es_systems.xml, see devdoc/36 section 4.2. They are
        // matched on the raw command, i.e. before the find rules replace them by a path.
        if (launchCommand.find("%EMULATOR_RETROARCH%") != std::string::npos)
            return WindowBackend::RETROARCH;
        if (launchCommand.find("%EMULATOR_MAME%") != std::string::npos ||
            launchCommand.find("%EMULATOR_GROOVYMAME%") != std::string::npos)
            return WindowBackend::MAME;
        return WindowBackend::UNSUPPORTED;
    }

    WindowBackend windowBackendOfSystem(const SystemData* system)
    {
        if (system == nullptr)
            return WindowBackend::UNSUPPORTED;

        // Detect the backend of the command that will actually be used, so switching the
        // default emulator of a platform also switches whether the display mode is available.
        const std::string configured {system->getAlternativeEmulator()};
        const std::vector<std::pair<std::string, std::string>>& commands {
            system->getSystemEnvData()->mLaunchCommands};

        if (configured != "" && configured.substr(0, 9) != "<INVALID>") {
            for (const auto& command : commands) {
                if (command.second == configured)
                    return windowBackendOf(command.first);
            }
        }

        if (commands.empty())
            return WindowBackend::UNSUPPORTED;

        return windowBackendOf(commands.front().first);
    }

    std::string retroarchWindowedConfigPath()
    {
        // The per platform settings live next to exia.conf, and the flatpak sandbox of
        // RetroArch can read that directory (verified, devdoc/36 section 4.3).
        const std::string path {ExiaConfig::getPath()};
        const size_t separator {path.find_last_of('/')};
        const std::string directory {
            separator == std::string::npos ? path : path.substr(0, separator)};
        return directory + "/retroarch-windowed.cfg";
    }

    bool ensureRetroarchWindowedConfig()
    {
        const std::string path {retroarchWindowedConfigPath()};

        if (Utils::FileSystem::exists(path))
            return true;

        std::ofstream file {path};
        if (!file.is_open()) {
            LOG(LogError) << "PlatformSettings: cannot write " << path;
            return false;
        }

        // video_windowed_fullscreen has to be disabled as well, otherwise a borderless
        // fullscreen configuration would make the windowed mode look like fullscreen.
        file << "# Written by ES-DE for the per platform windowed display mode.\n";
        file << "# This file is appended to the RetroArch configuration on launch.\n";
        file << "video_fullscreen = \"false\"\n";
        file << "video_windowed_fullscreen = \"false\"\n";
        file.close();

        return true;
    }

    std::string windowedArguments(WindowBackend backend)
    {
        switch (backend) {
            case WindowBackend::RETROARCH:
                return "--appendconfig=" + retroarchWindowedConfigPath();
            case WindowBackend::MAME:
                return "-window";
            case WindowBackend::UNSUPPORTED:
                break;
        }
        return "";
    }

    std::string applyWindowedMode(const std::string& command,
                                  const std::string& systemName,
                                  const SystemData* system)
    {
        if (!isWindowed(systemName))
            return command;

        const WindowBackend backend {windowBackendOfSystem(system)};
        const std::string arguments {windowedArguments(backend)};

        if (arguments.empty()) {
            LOG(LogWarning) << "PlatformSettings: windowed display mode is configured for system \""
                            << systemName << "\" but the current emulator does not support it, "
                            << "launching fullscreen instead";
            return command;
        }

        if (backend == WindowBackend::RETROARCH)
            ensureRetroarchWindowedConfig();

        LOG(LogDebug) << "PlatformSettings: appending windowed mode arguments \"" << arguments
                      << "\" for system \"" << systemName << "\"";

        return command + " " + arguments;
    }

    bool reset(const std::string& systemName)
    {
        // Remove the entries instead of writing the default values back, so a reset platform
        // leaves no trace in the configuration file.
        ExiaConfig& config {ExiaConfig::getInstance()};
        bool changed {false};

        for (const char* key : {KEY_WINDOWED, KEY_HIDDEN}) {
            const std::string fullKey {confKey(systemName, key)};
            if (config.get(fullKey, "") != "") {
                config.remove(fullKey);
                changed = true;
            }
        }

        return changed;
    }
} // namespace PlatformSettings
