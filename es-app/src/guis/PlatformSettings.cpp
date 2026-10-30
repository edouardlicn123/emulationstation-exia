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
#include <sstream>

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
        // NOTE: %EMULATOR_FLYCAST-DOJO% must be matched before %EMULATOR_FLYCAST%, otherwise
        // the former would be swallowed by the latter.
        if (launchCommand.find("%EMULATOR_RETROARCH%") != std::string::npos)
            return WindowBackend::RETROARCH;
        if (launchCommand.find("%EMULATOR_MAME%") != std::string::npos ||
            launchCommand.find("%EMULATOR_GROOVYMAME%") != std::string::npos)
            return WindowBackend::MAME;
        if (launchCommand.find("%EMULATOR_DOLPHIN%") != std::string::npos)
            return WindowBackend::DOLPHIN;
        if (launchCommand.find("%EMULATOR_PLAY!%") != std::string::npos)
            return WindowBackend::PLAY;
        if (launchCommand.find("%EMULATOR_FLYCAST-DOJO%") != std::string::npos)
            return WindowBackend::FLYCAST_DOJO;
        if (launchCommand.find("%EMULATOR_FLYCAST%") != std::string::npos)
            return WindowBackend::FLYCAST;
        if (launchCommand.find("%EMULATOR_FINALBURN-NEO%") != std::string::npos)
            return WindowBackend::FINALBURN_NEO;
        if (launchCommand.find("%EMULATOR_DEMUL-WINDOWS%") != std::string::npos)
            return WindowBackend::DEMUL;
        if (launchCommand.find("%EMULATOR_SM2-EMU%") != std::string::npos)
            return WindowBackend::SM2_EMU;
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

    bool isFixedFullscreen(const SystemData* system)
    {
        // Demul platforms (gaelco, hikaru) are always fullscreen, the display mode is not
        // configurable and the platform page does not show the row.
        return windowBackendOfSystem(system) == WindowBackend::DEMUL;
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
            case WindowBackend::DOLPHIN:
                // Dolphin supports transient config overrides via -C; Video.Fullscreen toggles
                // fullscreen vs windowed mode.
                return "-C Video.Fullscreen=False";
            case WindowBackend::FLYCAST:
            case WindowBackend::FLYCAST_DOJO:
                // Flycast supports transient config overrides via -config section:key=value.
                return "-config video:fullscreen=0";
            case WindowBackend::PLAY:
            case WindowBackend::FINALBURN_NEO:
            case WindowBackend::DEMUL:
                // PLAY and FINALBURN_NEO need to remove an existing fullscreen flag from the
                // command instead of appending. DEMUL reads UseFullscreen from its plugin ini
                // file instead of taking a command-line argument. Neither appends arguments.
                return "";
            case WindowBackend::SM2_EMU:
                // sm2-emu defaults to windowed; --fullscreen forces fullscreen.
                return "--fullscreen";
            case WindowBackend::UNSUPPORTED:
                break;
        }
        return "";
    }

    // Forward declaration: defined below applyWindowedMode().
    std::string removeLaunchArgument(const std::string& command, const std::string& flag);

    std::string applyWindowedMode(const std::string& command,
                                  const std::string& systemName,
                                  const SystemData* system)
    {
        const WindowBackend backend {windowBackendOfSystem(system)};

        // ------------------------------------------------------------------
        // DEMUL: windowed/fullscreen is controlled by the UseFullscreen key in
        // its gpuDX11*.ini plugin file, not by a command-line argument. Mutate
        // the ini and return the command unchanged.
        // ------------------------------------------------------------------
        if (backend == WindowBackend::DEMUL) {
            const std::vector<std::string> iniFiles {
                "/home/emu/Applications/demul/gpuDX11old.ini",
                "/home/emu/Applications/demul/gpuDX11.ini"
            };
            // Fixed fullscreen platforms ignore the per platform windowed setting, so a stale
            // "system.<name>.windowed" entry can never bring the emulator up in a window.
            const std::string value {"1"};
            for (const auto& path : iniFiles) {
                if (!Utils::FileSystem::exists(path))
                    continue;
                std::ifstream in(path);
                if (!in.is_open()) {
                    LOG(LogWarning) << "PlatformSettings: cannot read " << path;
                    continue;
                }
                std::stringstream buffer;
                buffer << in.rdbuf();
                in.close();
                std::string content {buffer.str()};
                std::string replaced;
                bool found = false;
                // Demul writes CRLF line endings, keep the file in the same style so the
                // Windows profile parser sees it unchanged in every other respect.
                const std::string eol {
                    content.find("\r\n") == std::string::npos ? "\n" : "\r\n"};
                std::istringstream iss(content);
                std::string line;
                while (std::getline(iss, line)) {
                    const size_t eq {line.find('=')};
                    // The key is compared without the padding around the '=': Demul writes
                    // "UseFullscreen = 1" while older versions of this code only matched the
                    // unpadded form, so the key was never rewritten and an extra line was
                    // appended on every fullscreen launch instead (devdoc/58 T1).
                    std::string key {eq == std::string::npos ? std::string {} : line.substr(0, eq)};
                    while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                        key.pop_back();
                    if (key == "UseFullscreen") {
                        replaced += "UseFullscreen = " + value + eol;
                        found = true;
                    }
                    else {
                        replaced += line + "\n";
                    }
                }
                if (!found)
                    replaced += "UseFullscreen = " + value + eol;
                std::ofstream file(path);
                if (file.is_open())
                    file << replaced;
                else
                    LOG(LogWarning) << "PlatformSettings: cannot write " << path;
            }
            return command;
        }

        if (!isWindowed(systemName)) {
            // Fullscreen mode: some emulators need an explicit flag to force
            // fullscreen even though that is their default.
            if (backend == WindowBackend::DOLPHIN)
                return command + " -C Video.Fullscreen=True";
            if (backend == WindowBackend::FLYCAST || backend == WindowBackend::FLYCAST_DOJO)
                return command + " -config video:fullscreen=1";
            if (backend == WindowBackend::SM2_EMU)
                return command + " --fullscreen";
            // PLAY and FINALBURN_NEO already carry their fullscreen flag in
            // es_systems.xml; nothing to add.
            return command;
        }

        // Windowed mode: append the backend-specific arguments.
        const std::string arguments {windowedArguments(backend)};

        if (arguments.empty()) {
            // PLAY and FINALBURN_NEO: remove their fullscreen flag from the
            // command instead of appending anything.
            if (backend == WindowBackend::PLAY)
                return removeLaunchArgument(command, "--fullscreen");
            if (backend == WindowBackend::FINALBURN_NEO)
                return removeLaunchArgument(command, "-fullscreen");
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

    // ------------------------------------------------------------------
    // Remove a standalone launch argument (e.g. "--fullscreen") from a
    // command line, collapsing the surrounding whitespace.
    // ------------------------------------------------------------------
    std::string removeLaunchArgument(const std::string& command, const std::string& flag)
    {
        std::string result {command};
        size_t pos {0};
        while ((pos = result.find(flag, pos)) != std::string::npos) {
            const bool beforeOk {pos == 0 || result[pos - 1] == ' '};
            const size_t endPos {pos + flag.size()};
            const bool afterOk {endPos == result.size() || result[endPos] == ' '};
            if (beforeOk && afterOk) {
                size_t start {pos};
                if (pos > 0 && result[pos - 1] == ' ')
                    --start;
                size_t end {endPos};
                if (endPos < result.size() && result[endPos] == ' ')
                    ++end;
                result.erase(start, end - start);
                pos = start;
            }
            else {
                pos = endPos;
            }
        }
        return result;
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
