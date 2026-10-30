//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiArcadeKeybinds.cpp
//
//  Arcade simulator key setup (devdoc/43), reachable from OPTIONS -> INPUT DEVICE
//  SETTINGS. Captures keyboard keys row by row and writes them into the packaged
//  simulator configurations installed on this station:
//
//    Demul      ~/Applications/demul/padDemul.ini         (evdev key codes)
//    Dolphin    flatpak GCPadNew.ini                      (X11 keysym names)
//    Flycast    flatpak mappings/SDL_Keyboard.cfg         (SDL scancodes)
//    Play!      flatpak inputprofiles/default.xml         (Qt key codes)
//    Lindbergh  ~/roms/emus/lindbergh/controls.ini        (SDL scancode names)
//    RetroArch  flatpak config/remaps/<core>/<core>.rmp   (RetroArch key names)
//
//  The values are stored in ES-DE space as SDL key names and converted per emulator
//  during the write. Writes are idempotent: files whose content does not change are
//  left untouched, changed files get a .bak-<timestamp> backup first.
//
//  This is an Exia (EndeavourOS game station) local addition.
//

#include "guis/GuiArcadeKeybinds.h"

#include "FileData.h"
#include "Log.h"
#include "MenuFontUtil.h"
#include "SystemData.h"
#include "Window.h"
#include "guis/GuiMsgBox.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
// ArcadeKeybindStore
///////////////////////////////////////////////////////////////////////////////

ArcadeKeybindStore& ArcadeKeybindStore::getInstance()
{
    static ArcadeKeybindStore instance;
    return instance;
}

std::string ArcadeKeybindStore::getPath()
{
    return Utils::FileSystem::getAppDataDirectory() + "/arcade_keybinds.xml";
}

void ArcadeKeybindStore::load()
{
    // MAME aligned defaults (devdoc/43 section 1). P1 follows the MAME baseline, P2
    // starts with the two unambiguous MAME entries and empty rows for the rest, the
    // globals mirror the Demul/MAME service and test conventions plus reset=F3.
    mBindings = {{"p1.coin", "5"},             {"p1.start", "1"},            {"p1.up", "Up"},
                 {"p1.down", "Down"},          {"p1.left", "Left"},          {"p1.right", "Right"},
                 {"p1.btn1", "Left Ctrl"},     {"p1.btn2", "Left Alt"},      {"p1.btn3", "Space"},
                 {"p1.btn4", "Left Shift"},    {"p1.btn5", "Z"},             {"p1.btn6", "X"},
                 {"p1.throttle", "Up"},        {"p1.brake", "Down"},         {"p1.boost", ""},
                 {"p1.s1up", ""},              {"p1.s1down", ""},            {"p1.s1left", ""},
                 {"p1.s1right", ""},           {"p1.s2up", ""},              {"p1.s2down", ""},
                 {"p1.s2left", ""},            {"p1.s2right", ""},
                 {"p2.coin", "6"},             {"p2.start", "2"},            {"p2.up", ""},
                 {"p2.down", ""},              {"p2.left", ""},              {"p2.right", ""},
                 {"p2.btn1", ""},              {"p2.btn2", ""},              {"p2.btn3", ""},
                 {"p2.btn4", ""},              {"p2.btn5", ""},              {"p2.btn6", ""},
                 {"p2.throttle", ""},          {"p2.brake", ""},             {"p2.boost", ""},
                 {"p2.s1up", ""},              {"p2.s1down", ""},            {"p2.s1left", ""},
                 {"p2.s1right", ""},           {"p2.s2up", ""},              {"p2.s2down", ""},
                 {"p2.s2left", ""},            {"p2.s2right", ""},
                 {"global.service", "F2"},     {"global.test", "F4"},        {"global.reset", "F3"}};

    if (!Utils::FileSystem::exists(getPath()))
        return;
    std::ifstream file(getPath(), std::ios::binary);
    if (!file.is_open()) {
        LOG(LogWarning) << "GuiArcadeKeybinds: cannot read " << getPath();
        return;
    }
    std::stringstream stream;
    stream << file.rdbuf();
    const std::string xml {stream.str()};

    // Minimal attribute scan, 33 entries do not justify a parser dependency:
    // <binding key="p1.coin" value="5" />
    size_t searchPos {0};
    while (true) {
        const size_t at {xml.find("<binding ", searchPos)};
        if (at == std::string::npos)
            break;
        const size_t end {xml.find('>', at)};
        const size_t keyAt {xml.find("key=\"", at)};
        const size_t valAt {xml.find("value=\"", at)};
        if (end == std::string::npos || keyAt == std::string::npos || valAt == std::string::npos ||
            keyAt > end || valAt > end) {
            searchPos = at + 1;
            continue;
        }
        const size_t keyEnd {xml.find('"', keyAt + 5)};
        const size_t valEnd {xml.find('"', valAt + 7)};
        if (keyEnd == std::string::npos || valEnd == std::string::npos)
            break;
        const std::string key {xml.substr(keyAt + 5, keyEnd - keyAt - 5)};
        const std::string value {xml.substr(valAt + 7, valEnd - valAt - 7)};
        if (!key.empty() && mBindings.find(key) != mBindings.end())
            mBindings[key] = value;
        searchPos = (valEnd == std::string::npos) ? at + 1 : valEnd;
    }
}

bool ArcadeKeybindStore::save()
{
    std::stringstream out;
    out << "<arcadeKeybinds version=\"1\">\n";
    for (const auto& entry : mBindings)
        out << "  <binding key=\"" << entry.first << "\" value=\"" << entry.second << "\" />\n";
    out << "</arcadeKeybinds>\n";

    std::ofstream file(getPath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG(LogError) << "GuiArcadeKeybinds: cannot write " << getPath();
        return false;
    }
    file << out.str();
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// ArcadePerGameStore
///////////////////////////////////////////////////////////////////////////////

ArcadePerGameStore& ArcadePerGameStore::getInstance()
{
    static ArcadePerGameStore instance;
    return instance;
}

std::string ArcadePerGameStore::getPath()
{
    return Utils::FileSystem::getAppDataDirectory() + "/arcade_keybinds_games.xml";
}

std::string ArcadePerGameStore::getSnapshotRoot()
{
    return Utils::FileSystem::getAppDataDirectory() + "/arcade_keybinds/demul";
}

void ArcadePerGameStore::load()
{
    mGames.clear();
    if (!Utils::FileSystem::exists(getPath()))
        return;
    std::ifstream file(getPath(), std::ios::binary);
    if (!file.is_open()) {
        LOG(LogWarning) << "GuiArcadeKeybinds: cannot read " << getPath();
        return;
    }
    std::stringstream stream;
    stream << file.rdbuf();
    const std::string xml {stream.str()};

    // Flat records keep the parser as small as the global store one:
    // <override system="hikaru" rom="sgnascar" key="p1.throttle" value="V" />
    size_t searchPos {0};
    while (true) {
        const size_t at {xml.find("<override ", searchPos)};
        if (at == std::string::npos)
            break;
        const size_t tagEnd {xml.find('>', at)};
        if (tagEnd == std::string::npos)
            break;
        const auto attribute {[&xml, at, tagEnd](const char* name) {
            const std::string needle {std::string(name) + "=\""};
            const size_t nameAt {xml.find(needle, at)};
            if (nameAt == std::string::npos || nameAt + needle.size() > tagEnd)
                return std::string("");
            const size_t valueStart {nameAt + needle.size()};
            const size_t valueEnd {xml.find('"', valueStart)};
            if (valueEnd == std::string::npos || valueEnd > tagEnd)
                return std::string("");
            return xml.substr(valueStart, valueEnd - valueStart);
        }};
        const std::string system {attribute("system")};
        const std::string rom {attribute("rom")};
        const std::string key {attribute("key")};
        const std::string value {attribute("value")};
        if (!system.empty() && !rom.empty() && !key.empty() && !value.empty())
            mGames[{system, rom}][key] = value;
        searchPos = tagEnd;
    }
}

bool ArcadePerGameStore::save()
{
    std::stringstream out;
    out << "<arcadeKeybindGames version=\"1\">\n";
    for (const auto& [game, overrides] : mGames) {
        for (const auto& [key, value] : overrides) {
            if (value.empty())
                continue;
            out << "  <override system=\"" << game.first << "\" rom=\"" << game.second
                << "\" key=\"" << key << "\" value=\"" << value << "\" />\n";
        }
    }
    out << "</arcadeKeybindGames>\n";

    std::ofstream file(getPath(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        LOG(LogError) << "GuiArcadeKeybinds: cannot write " << getPath();
        return false;
    }
    file << out.str();
    return true;
}

std::string ArcadePerGameStore::get(const std::string& system, const std::string& rom,
                                    const std::string& key) const
{
    const auto game {mGames.find({system, rom})};
    if (game == mGames.end())
        return "";
    const auto entry {game->second.find(key)};
    return (entry != game->second.end()) ? entry->second : "";
}

void ArcadePerGameStore::set(const std::string& system, const std::string& rom,
                             const std::string& key, const std::string& value)
{
    if (value.empty()) {
        const auto game {mGames.find({system, rom})};
        if (game == mGames.end())
            return;
        game->second.erase(key);
        if (game->second.empty())
            mGames.erase(game);
        return;
    }
    mGames[{system, rom}][key] = value;
}

size_t ArcadePerGameStore::overrideCount(const std::string& system,
                                         const std::string& rom) const
{
    const auto game {mGames.find({system, rom})};
    return (game != mGames.end()) ? game->second.size() : 0;
}

const ArcadePerGameStore::Overrides& ArcadePerGameStore::overridesFor(
    const std::string& system, const std::string& rom) const
{
    static const Overrides empty;
    const auto game {mGames.find({system, rom})};
    return (game != mGames.end()) ? game->second : empty;
}

std::vector<std::pair<std::string, std::string>> ArcadePerGameStore::gameList() const
{
    std::vector<std::pair<std::string, std::string>> games;
    for (const auto& entry : mGames)
        games.push_back(entry.first);
    return games;
}

///////////////////////////////////////////////////////////////////////////////
// Key code conversion (one SDL scancode to every emulator notation)
///////////////////////////////////////////////////////////////////////////////

namespace {
struct KeyEntry {
    int scan_;         // SDL scancode
    int evdev_;        // Linux key code used by Demul
    const char* dolphin_;
    int qtKey_;        // Qt::Key value used by Play!
};

// The evdev column follows the physical QWERTY layout of <linux/input-event-codes.h>,
// not the alphabetical order: KEY_Q=16, KEY_A=30, KEY_Z=44 and so on. Digits are
// SDL_SCANCODE_1=30..0=39 against evdev KEY_1=2..KEY_0=11.
const std::vector<KeyEntry> sKeyEntries {
    {4, 30, "A", 65},     {5, 48, "B", 66},     {6, 46, "C", 67},
    {7, 32, "D", 68},     {8, 18, "E", 69},     {9, 33, "F", 70},
    {10, 34, "G", 71},    {11, 35, "H", 72},    {12, 23, "I", 73},
    {13, 36, "J", 74},    {14, 37, "K", 75},    {15, 38, "L", 76},
    {16, 50, "M", 77},    {17, 49, "N", 78},    {18, 24, "O", 79},
    {19, 25, "P", 80},    {20, 16, "Q", 81},    {21, 19, "R", 82},
    {22, 31, "S", 83},    {23, 20, "T", 84},    {24, 22, "U", 85},
    {25, 47, "V", 86},    {26, 17, "W", 87},    {27, 45, "X", 88},
    {28, 21, "Y", 89},    {29, 44, "Z", 90},
    {30, 2, "1", 49},     {31, 3, "2", 50},     {32, 4, "3", 51},
    {33, 5, "4", 52},     {34, 6, "5", 53},     {35, 7, "6", 54},
    {36, 8, "7", 55},     {37, 9, "8", 56},     {38, 10, "9", 57},
    {39, 11, "0", 48},
    {41, 1, "Escape", 16777216},
    {42, 14, "BackSpace", 16777219},
    {43, 15, "Tab", 9},
    {44, 57, "space", 32},
    {45, 12, "minus", 45},
    {46, 13, "equal", 61},
    {47, 26, "bracketleft", 91},
    {48, 27, "bracketright", 93},
    {49, 43, "backslash", 92},
    {51, 39, "semicolon", 59},
    {52, 40, "apostrophe", 39},
    {54, 51, "comma", 44},
    {55, 52, "period", 46},
    {56, 53, "slash", 47},
    {58, 59, "F1", 16777264},
    {59, 60, "F2", 16777265},
    {60, 61, "F3", 16777266},
    {61, 62, "F4", 16777267},
    {62, 63, "F5", 16777268},
    {63, 64, "F6", 16777269},
    {64, 65, "F7", 16777270},
    {65, 66, "F8", 16777271},
    {66, 67, "F9", 16777272},
    {67, 68, "F10", 16777273},
    {68, 87, "F11", 16777274},
    {69, 88, "F12", 16777275},
    {74, 102, "Home", 16777232},
    {75, 104, "Page_Up", 16777238},
    {76, 111, "Delete", 16777223},
    {77, 107, "End", 16777233},
    {78, 109, "Page_Down", 16777239},
    {79, 106, "Right", 16777236},
    {80, 105, "Left", 16777234},
    {81, 108, "Down", 16777237},
    {82, 103, "Up", 16777235},
    {40, 28, "Return", 16777220},
    {224, 29, "Ctrl", 16777249},
    {225, 42, "Shift", 16777248},
    {226, 56, "Alt", 16777251},
    {228, 97, "Ctrl", 16777249},
    {229, 54, "Shift", 16777248},
    {230, 100, "Alt", 16777251}};

const KeyEntry* findEntry(int scan)
{
    for (const KeyEntry& entry : sKeyEntries) {
        if (entry.scan_ == scan)
            return &entry;
    }
    return nullptr;
}

// Unified SDL key name -> SDL scancode, -1 when the name is unknown.
int scancodeOf(const std::string& keyName)
{
    // Do not go through SDL_GetKeyFromName() + SDL_GetScancodeFromKey(): with
    // sdl2-compat on X11 the latter returns SDL_SCANCODE_UNKNOWN for function keys
    // (F1-F12), so the name lookup would silently skip every F-key binding.
    const SDL_Scancode scan {SDL_GetScancodeFromName(keyName.c_str())};
    if (scan == SDL_SCANCODE_UNKNOWN)
        return -1;
    return static_cast<int>(scan);
}

// RetroArch key names (input_config_key_map in input/input_keymaps.c). The SDL
// scancode name is translated into the spelling RetroArch writes into its
// configuration files: letters are lower case, main-keyboard digits keep their
// raw name ("5", "1"), numpad digits get a "num" prefix ("num5", "num1"), and the
// named keys use short forms such as "ctrl", "enter" or "pageup".
std::string retroArchKeyName(int scan)
{
    const char* raw {SDL_GetScancodeName(static_cast<SDL_Scancode>(scan))};
    if (raw == nullptr || *raw == '\0')
        return std::string();
    const std::string name {raw};
    // Numpad digits: SDL returns "KP_0"..."KP_9", RetroArch expects "num0"..."num9".
    if (name.size() == 3 && name[0] == 'K' && name[1] == 'P' && name[2] >= '0' && name[2] <= '9')
        return "num" + name.substr(2);
    if (name.size() == 1) {
        const char character {name.front()};
        if (character >= 'A' && character <= 'Z')
            return std::string(1, static_cast<char>(character - 'A' + 'a'));
        if (character >= '0' && character <= '9')
            return "num" + name;  // main-keyboard digit: RetroArch expects "num0".."num9"
        switch (character) {
            case '-': return "minus";
            case '=': return "equals";
            case '[': return "leftbracket";
            case ']': return "rightbracket";
            case '\\': return "backslash";
            case ';': return "semicolon";
            case '\'': return "quote";
            case ',': return "comma";
            case '.': return "period";
            case '/': return "slash";
            case '`': return "tilde";
            default: break;
        }
    }
    static const std::map<std::string, std::string> named {
        {"Left Ctrl", "ctrl"},      {"Left Shift", "shift"}, {"Left Alt", "alt"},
        {"Right Ctrl", "rctrl"},    {"Right Shift", "rshift"}, {"Right Alt", "ralt"},
        {"Return", "enter"},        {"Escape", "escape"},     {"Backspace", "backspace"},
        {"Tab", "tab"},             {"Space", "space"},       {"Up", "up"},
        {"Down", "down"},           {"Left", "left"},         {"Right", "right"},
        {"Page Up", "pageup"},      {"Page Down", "pagedown"}, {"Delete", "del"},
        {"Insert", "insert"},       {"Home", "home"},         {"End", "end"}};
    const auto found {named.find(name)};
    if (found != named.end())
        return found->second;
    std::string lower {name};
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return lower;
}
} // namespace

///////////////////////////////////////////////////////////////////////////////
// File writing helpers
///////////////////////////////////////////////////////////////////////////////

namespace {
struct EmuResult {
    bool touched {false}; // File written (content changed).
    bool missing {false}; // Configuration file not found.
    int skippedCount {0}; // Entries skipped because the emulator has no bucket.
    int verifiedCount {0}; // Entries read back from the written file and matched.
    int failedCount {0}; // Entries that could not be written or did not match.
};

std::string backupSuffix()
{
    const std::time_t now {std::time(nullptr)};
    const std::tm* local {std::localtime(&now)};
    std::stringstream out;
    out << std::put_time(local, "%Y%m%d-%H%M%S");
    return out.str();
}

// The SAVE audit is written directly to disk instead of being collected in memory, so
// a crash halfway through still leaves the entries handled up to that point on disk.
std::ofstream sReportFile;
std::string sReportPath;

std::string reportField(const std::string& value)
{
    std::string result {value};
    for (char& character : result) {
        if (character == '\t' || character == '\n' || character == '\r')
            character = ' ';
    }
    return result;
}

void reportRow(const std::string& emulator,
               const std::string& action,
               const std::string& keyName,
               int scan,
               const std::string& target,
               const std::string& value,
               const std::string& status,
               const std::string& detail = "")
{
    if (!sReportFile.is_open())
        return;
    sReportFile << reportField(emulator) << '\t' << reportField(action) << '\t'
                << reportField(keyName) << '\t' << (scan >= 0 ? std::to_string(scan) : "-")
                << '\t' << reportField(target) << '\t' << reportField(value) << '\t'
                << reportField(status) << '\t' << reportField(detail) << '\n';
    sReportFile.flush();
}

void pruneReports(const std::string& directory, size_t keep)
{
    std::vector<std::string> reports;
    for (const std::string& path : Utils::FileSystem::getDirContent(directory)) {
        if (Utils::FileSystem::getExtension(path) == ".tsv")
            reports.push_back(path);
    }
    std::sort(reports.begin(), reports.end());
    while (reports.size() >= keep) {
        Utils::FileSystem::removeFile(reports.front());
        reports.erase(reports.begin());
    }
}

bool beginReport(const std::string& storePath)
{
    const std::string directory {
        Utils::FileSystem::getAppDataDirectory() + "/logs/arcade-keybinds"};
    if (!Utils::FileSystem::exists(directory) &&
        !Utils::FileSystem::createDirectory(directory)) {
        LOG(LogError) << "GuiArcadeKeybinds: cannot create " << directory;
        return false;
    }
    pruneReports(directory, 20);

    const std::string base {directory + "/" + backupSuffix()};
    std::string path {base + ".tsv"};
    int suffix {1};
    while (Utils::FileSystem::exists(path))
        path = base + "-" + std::to_string(suffix++) + ".tsv";

    sReportFile.open(path, std::ios::binary | std::ios::trunc);
    if (!sReportFile.is_open()) {
        LOG(LogError) << "GuiArcadeKeybinds: cannot write audit report " << path;
        return false;
    }
    sReportPath = path;
    sReportFile << "# ES-DE arcade keybind audit\n";
    sReportFile << "# time\t" << backupSuffix() << "\n";
    sReportFile << "# store\t" << reportField(storePath) << "\n";
    sReportFile << "emulator\taction\tkey\tscancode\ttarget\tvalue\tstatus\tdetail\n";
    return true;
}

void endReport(const EmuResult& demul,
               const EmuResult& dolphin,
               const EmuResult& flycast,
               const EmuResult& play,
               const EmuResult& lindbergh,
               const EmuResult& retroarch,
               bool demulOnly = false)
{
    if (!sReportFile.is_open())
        return;

    const auto summary {[](const char* name, const EmuResult& result) {
        std::stringstream row;
        row << name << ": verified=" << result.verifiedCount << " skipped=" << result.skippedCount
            << " failed=" << result.failedCount << " written=" << (result.touched ? 1 : 0)
            << " missing=" << (result.missing ? 1 : 0);
        return row.str();
    }};

    sReportFile << "\n# summary\n";
    sReportFile << "# " << summary("Demul", demul) << "\n";
    if (demulOnly) {
        sReportFile << "# scope: Demul only (per game profile save)\n";
        sReportFile.close();
        LOG(LogInfo) << "GuiArcadeKeybinds: audit report written to " << sReportPath;
        return;
    }
    sReportFile << "# " << summary("Dolphin", dolphin) << "\n";
    sReportFile << "# " << summary("Flycast", flycast) << "\n";
    sReportFile << "# " << summary("Play!", play) << "\n";
    sReportFile << "# " << summary("Lindbergh", lindbergh) << "\n";
    sReportFile << "# " << summary("RetroArch", retroarch) << "\n";
    sReportFile.close();
    LOG(LogInfo) << "GuiArcadeKeybinds: audit report written to " << sReportPath;
}

bool readFileBytes(const std::string& path, std::string& content)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    std::stringstream stream;
    stream << file.rdbuf();
    content = stream.str();
    return true;
}

void determineLineEnding(const std::string& content, std::string& ending)
{
    ending = "\n";
    const size_t lf {content.find('\n')};
    if (lf != std::string::npos && lf > 0 && content[lf - 1] == '\r')
        ending = "\r\n";
}

std::vector<std::string> splitLines(const std::string& content)
{
    std::vector<std::string> lines;
    std::stringstream stream {content};
    std::string line;
    while (std::getline(stream, line, '\n')) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// Backs up path after the staging write succeeded and replaces it atomically.
bool backupAndWrite(const std::string& path, const std::string& newContent)
{
    const std::string staging {path + ".keybinds_tmp"};
    {
        std::ofstream file(staging, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            LOG(LogError) << "GuiArcadeKeybinds: cannot write staging file " << staging;
            return false;
        }
        file << newContent;
    }
    const std::string backup {path + ".bak-" + backupSuffix()};
    std::string original;
    if (readFileBytes(path, original)) {
        std::ofstream backupFile(backup, std::ios::binary | std::ios::trunc);
        if (!backupFile.is_open())
            LOG(LogWarning) << "GuiArcadeKeybinds: cannot create backup " << backup;
        else
            backupFile << original;
    }
    Utils::FileSystem::renameFile(staging, path, true);
    // renameFile() returns the raw std::rename() result on POSIX (0 means success),
    // so the staging file disappearing is the reliable success check.
    if (Utils::FileSystem::exists(staging)) {
        LOG(LogError) << "GuiArcadeKeybinds: rename failed for " << path;
        return false;
    }
    return true;
}

// Joins lines back to bytes using the original line ending.
std::string joinLines(const std::vector<std::string>& lines, const std::string& ending,
                      bool trailingEnding)
{
    std::string out;
    for (size_t i {0}; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size())
            out += ending;
    }
    if (trailingEnding)
        out += ending;
    return out;
}

// Replaces a top level "key = value" line. retroarch.cfg has no sections, so the
// generic INI patcher cannot be used here. Returns true when the content changed.
bool setTopLevelValue(std::string& content, const std::string& key, const std::string& value)
{
    std::string ending {"\n"};
    determineLineEnding(content, ending);
    const bool trailingEnding {content.size() >= ending.size() &&
                               content.compare(content.size() - ending.size(), ending.size(),
                                               ending) == 0};
    const std::string prefix {key + " ="};
    const std::string replacement {key + " = \"" + value + "\""};
    std::vector<std::string> lines {splitLines(content)};
    for (std::string& line : lines) {
        if (line.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (line == replacement)
            return false;
        line = replacement;
        content = joinLines(lines, ending, trailingEnding);
        return true;
    }
    lines.push_back(replacement);
    content = joinLines(lines, ending, trailingEnding);
    return true;
}

struct KeyPatch {
    std::string section; // Ini section name without brackets.
    std::string key;     // Field copies keep the value alive for any caller.
    std::string value;
    // Lindbergh style: the value is a comma list whose first KEY_xxx token gets
    // replaced and the rest of the list keeps its other inputs untouched.
    bool replaceFirstKeyToken {false};
    bool optional {false}; // Missing in place: count as skipped instead of a failure.

    KeyPatch(std::string sectionInit, std::string keyInit, std::string valueInit) :
        section {std::move(sectionInit)},
        key {std::move(keyInit)},
        value {std::move(valueInit)} {}
    KeyPatch(std::string sectionInit, std::string keyInit, std::string valueInit, bool tokenInit) :
        section {std::move(sectionInit)},
        key {std::move(keyInit)},
        value {std::move(valueInit)},
        replaceFirstKeyToken {tokenInit} {}
};

// Generic ini patcher: replaces a key inside a section, appends it inside an existing
// section when new, or creates the section at the end when requested.
bool patchIniFile(const std::string& path, const std::vector<KeyPatch>& patches,
                  bool createMissingSection, EmuResult& result)
{
    std::string content;
    if (!readFileBytes(path, content)) {
        result.missing = true;
        LOG(LogWarning) << "GuiArcadeKeybinds: " << path << " not found";
        return false;
    }
    std::string ending;
    determineLineEnding(content, ending);
    const bool trailingEnding {!content.empty() && content.back() == '\n'};
    std::vector<std::string> lines {splitLines(content)};

    const auto trimLeading {[](std::string& text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.erase(text.begin());
    }};

    for (KeyPatch patch : patches) {
        const std::string section {patch.section};
        const std::string key {patch.key};
        bool replaced {false};
        std::string currentSection;

        for (size_t i {0}; i < lines.size(); ++i) {
            std::string trimmed {lines[i]};
            trimLeading(trimmed);
            if (trimmed.size() > 1 && trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = trimmed.substr(1, trimmed.size() - 2);
                continue;
            }
            if (currentSection != section)
                continue;
            const size_t equals {trimmed.find('=')};
            if (equals == std::string::npos)
                continue;
            std::string lineKey {trimmed.substr(0, equals)};
            while (!lineKey.empty() && (lineKey.back() == ' ' || lineKey.back() == '\t'))
                lineKey.pop_back();
            if (lineKey != key)
                continue;
            std::string prefix {lines[i].substr(0, lines[i].find(key))};
            if (patch.replaceFirstKeyToken) {
                std::string valuePart {trimmed.substr(equals + 1)};
                std::vector<std::string> tokens;
                std::stringstream tokenStream {valuePart};
                std::string token;
                while (std::getline(tokenStream, token, ',')) {
                    // Normalise the surrounding spaces so the joined list does not keep
                    // the original alignment quirks of the source file.
                    while (!token.empty() && token.front() == ' ')
                        token.erase(token.begin());
                    while (!token.empty() && token.back() == ' ')
                        token.pop_back();
                    tokens.push_back(token);
                }
                bool found {false};
                for (std::string& entry : tokens) {
                    std::string trimmedToken {entry};
                    trimLeading(trimmedToken);
                    if (trimmedToken.rfind("KEY_", 0) == 0) {
                        entry = "KEY_" + patch.value;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // No keyboard token in this row, leave the file untouched for it.
                    result.skippedCount++;
                    replaced = true;
                    break;
                }
                std::stringstream joined;
                for (size_t t {0}; t < tokens.size(); ++t) {
                    if (t > 0)
                        joined << ", ";
                    joined << tokens[t];
                }
                lines[i] = prefix + key + " = " + joined.str();
                replaced = true;
                break;
            }
            lines[i] = prefix + key + " = " + patch.value;
            replaced = true;
            break;
        }

        if (replaced)
            continue;

        if (patch.optional) {
            result.skippedCount++;
            LOG(LogInfo) << "GuiArcadeKeybinds: " << path << ": no " << section << "." << key
                         << " entry, skipped";
            continue;
        }

        // Append inside the existing section or create the section at the end.
        bool sectionFound {false};
        for (size_t i {0}; i < lines.size(); ++i) {
            std::string trimmed {lines[i]};
            trimLeading(trimmed);
            if (trimmed.size() > 1 && trimmed.front() == '[' && trimmed.back() == ']') {
                const std::string name {trimmed.substr(1, trimmed.size() - 2)};
                if (name == section)
                    sectionFound = true;
                else if (sectionFound)
                    break;
                continue;
            }
            if (sectionFound) {
                const auto iterator {lines.begin()};
                lines.insert(iterator + static_cast<long>(i), key + " = " + patch.value);
                break;
            }
        }
        if (!sectionFound && createMissingSection) {
            lines.push_back("");
            lines.push_back("[" + section + "]");
            lines.push_back(key + " = " + patch.value);
        }
        else if (!sectionFound) {
            result.skippedCount++;
            LOG(LogInfo) << "GuiArcadeKeybinds: " << path << ": section [" << section
                         << "] not found, skipped";
            continue;
        }
    }

    std::string newContent {joinLines(lines, ending, trailingEnding)};
    if (newContent == content) {
        LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
        return true;
    }
    if (!backupAndWrite(path, newContent))
        return false;
    result.touched = true;
    return true;
}

// Reads the file back and checks every patched value. This catches writers that
// silently skipped a row or files that were changed again between write and read.
void verifyIniPatches(const std::string& emulator,
                      const std::string& path,
                      const std::vector<KeyPatch>& patches,
                      EmuResult& result,
                      bool createMissingSection)
{
    std::string content;
    if (!readFileBytes(path, content)) {
        ++result.failedCount;
        reportRow(emulator, "verify", "", -1, path, "", "ERROR", "cannot read back");
        LOG(LogError) << "GuiArcadeKeybinds: cannot read back " << path;
        return;
    }

    const auto trim {[](std::string text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.erase(text.begin());
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                                 text.back() == '\r'))
            text.pop_back();
        return text;
    }};
    const std::vector<std::string> lines {splitLines(content)};

    for (const KeyPatch& patch : patches) {
        bool found {false};
        bool sectionFound {false};
        std::string actual;
        std::string currentSection;
        for (const std::string& line : lines) {
            std::string trimmed {trim(line)};
            if (trimmed.size() > 1 && trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = trimmed.substr(1, trimmed.size() - 2);
                if (currentSection == patch.section)
                    sectionFound = true;
                continue;
            }
            if (currentSection != patch.section)
                continue;
            const size_t equals {trimmed.find('=')};
            if (equals == std::string::npos)
                continue;
            if (trim(trimmed.substr(0, equals)) != patch.key)
                continue;
            actual = trim(trimmed.substr(equals + 1));
            found = true;
            break;
        }

        const std::string target {"[" + patch.section + "] " + patch.key};
        if (!found) {
            if (!createMissingSection && !sectionFound) {
                // patchIniFile() deliberately leaves missing sections alone for
                // emulators such as Demul; mirror that decision instead of calling
                // the intentional skip a failed write.
                reportRow(emulator, "verify", patch.key, -1, target, "",
                          "SKIP", "section missing, writer does not create it");
            }
            else if (!patch.optional) {
                ++result.failedCount;
                reportRow(emulator, "verify", patch.key, -1, target, "",
                          "MISSING", "expected=" + patch.value);
                LOG(LogError) << "GuiArcadeKeybinds: " << emulator << " " << target
                              << " missing after write";
            }
            continue;
        }

        if (patch.replaceFirstKeyToken) {
            bool keyTokenFound {false};
            std::string firstToken;
            std::stringstream stream {actual};
            std::string token;
            while (std::getline(stream, token, ',')) {
                token = trim(token);
                if (token.rfind("KEY_", 0) == 0) {
                    firstToken = token;
                    keyTokenFound = true;
                    break;
                }
            }
            if (!keyTokenFound) {
                reportRow(emulator, "verify", patch.key, -1, target, actual,
                          "SKIP", "no KEY_ token in row");
                continue;
            }
            const std::string expected {"KEY_" + patch.value};
            if (firstToken == expected) {
                ++result.verifiedCount;
                reportRow(emulator, "verify", patch.key, -1, target, firstToken,
                          "OK", "expected=" + expected);
            }
            else {
                ++result.failedCount;
                reportRow(emulator, "verify", patch.key, -1, target, firstToken,
                          "MISMATCH", "expected=" + expected);
                LOG(LogError) << "GuiArcadeKeybinds: " << emulator << " " << target
                              << " is " << firstToken << ", expected " << expected;
            }
            continue;
        }

        if (actual == patch.value) {
            ++result.verifiedCount;
            reportRow(emulator, "verify", patch.key, -1, target, actual, "OK",
                      "expected=" + patch.value);
        }
        else {
            ++result.failedCount;
            reportRow(emulator, "verify", patch.key, -1, target, actual,
                      "MISMATCH", "expected=" + patch.value);
            LOG(LogError) << "GuiArcadeKeybinds: " << emulator << " " << target << " is "
                          << actual << ", expected " << patch.value;
        }
    }
}

bool patchPlayProfile(const std::string& path, const std::string& pad,
                      const std::map<std::string, std::string>& actions, EmuResult& result)
{
    std::string content;
    if (!readFileBytes(path, content)) {
        result.missing = true;
        LOG(LogWarning) << "GuiArcadeKeybinds: " << path << " not found";
        return false;
    }
    std::string ending;
    determineLineEnding(content, ending);
    const bool trailingEnding {!content.empty() && content.back() == '\n'};
    std::vector<std::string> lines {splitLines(content)};

    for (const auto& [action, prefValues] : actions) {
        const size_t c1 {prefValues.find(':')};
        const size_t c2 {prefValues.find(':', c1 + 1)};
        const size_t c3 {prefValues.find(':', c2 + 1)};
        if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos)
            continue;
        const std::string keyId {prefValues.substr(0, c1)};
        const std::string providerId {prefValues.substr(c1 + 1, c2 - c1 - 1)};
        const std::string keyType {prefValues.substr(c2 + 1, c3 - c2 - 1)};
        const std::string bindingType {prefValues.substr(c3 + 1)};
        const std::vector<std::pair<const char*, std::string>> prefs {
            {"bindingtarget1.keyId", keyId},
            {"bindingtarget1.keyType", keyType},
            {"bindingtarget1.providerId", providerId},
            {"bindingtype", bindingType}};
        for (const auto& [pref, value] : prefs) {
            const std::string needle {"Name=\"input." + pad + "." + action + "." + pref + "\""};
            bool replaced {false};
            for (std::string& line : lines) {
                if (line.find(needle) == std::string::npos)
                    continue;
                const size_t valueAt {line.find("Value=\"")};
                if (valueAt == std::string::npos)
                    continue;
                const size_t valueEnd {line.find('"', valueAt + 7)};
                if (valueEnd == std::string::npos)
                    continue;
                line = line.substr(0, valueAt + 7) + value + line.substr(valueEnd);
                replaced = true;
                break;
            }
            if (!replaced) {
                result.skippedCount++;
                LOG(LogInfo) << "GuiArcadeKeybinds: Play! " << pad << "." << action << "." << pref
                             << " not found, skipped";
            }
        }
    }

    const std::string newContent {joinLines(lines, ending, trailingEnding)};
    if (newContent == content) {
        LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
        return true;
    }
    if (!backupAndWrite(path, newContent))
        return false;
    result.touched = true;
    return true;
}

// Play! has no section/INI syntax, so its verification is a separate scan of the
// Name="..." / Value="..." rows that patchPlayProfile() just edited.
void verifyPlayProfile(const std::string& path,
                       const std::string& pad,
                       const std::map<std::string, std::string>& actions,
                       EmuResult& result)
{
    std::string content;
    if (!readFileBytes(path, content)) {
        ++result.failedCount;
        reportRow("Play!", "verify", pad, -1, path, "", "ERROR", "cannot read back");
        LOG(LogError) << "GuiArcadeKeybinds: cannot read back " << path;
        return;
    }
    const std::vector<std::string> lines {splitLines(content)};

    for (const auto& [action, prefValues] : actions) {
        const size_t c1 {prefValues.find(':')};
        const size_t c2 {prefValues.find(':', c1 + 1)};
        const size_t c3 {prefValues.find(':', c2 + 1)};
        if (c1 == std::string::npos || c2 == std::string::npos || c3 == std::string::npos)
            continue;
        const std::string keyId {prefValues.substr(0, c1)};
        const std::string providerId {prefValues.substr(c1 + 1, c2 - c1 - 1)};
        const std::string keyType {prefValues.substr(c2 + 1, c3 - c2 - 1)};
        const std::string bindingType {prefValues.substr(c3 + 1)};
        const std::vector<std::pair<const char*, std::string>> prefs {
            {"bindingtarget1.keyId", keyId},
            {"bindingtarget1.keyType", keyType},
            {"bindingtarget1.providerId", providerId},
            {"bindingtype", bindingType}};
        for (const auto& [pref, expected] : prefs) {
            const std::string needle {"Name=\"input." + pad + "." + action + "." + pref + "\""};
            const std::string target {pad + "." + action + "." + pref};
            bool found {false};
            std::string actual;
            for (const std::string& line : lines) {
                if (line.find(needle) == std::string::npos)
                    continue;
                const size_t valueAt {line.find("Value=\"")};
                if (valueAt == std::string::npos)
                    continue;
                const size_t valueEnd {line.find('"', valueAt + 7)};
                if (valueEnd == std::string::npos)
                    continue;
                actual = line.substr(valueAt + 7, valueEnd - valueAt - 7);
                found = true;
                break;
            }
            if (!found) {
                reportRow("Play!", "verify", action, -1, target, "", "SKIP",
                          "row not in profile");
                continue;
            }
            if (actual == expected) {
                ++result.verifiedCount;
                reportRow("Play!", "verify", action, -1, target, actual, "OK",
                          "expected=" + expected);
            }
            else {
                ++result.failedCount;
                reportRow("Play!", "verify", action, -1, target, actual, "MISMATCH",
                          "expected=" + expected);
                LOG(LogError) << "GuiArcadeKeybinds: Play! " << target << " is " << actual
                              << ", expected " << expected;
            }
        }
    }
}

// Flycast keyboard mapping: full rewrite because a user mapping file completely
// replaces the built-in default, so the emulation hotkeys are always written together
// with the game keys.
bool writeFlycastMapping(const std::string& path, const std::map<std::string, std::string>& binds,
                         EmuResult& result)
{
    const std::string dir {path.substr(0, path.rfind('/'))};
    if (!Utils::FileSystem::exists(dir) && !Utils::FileSystem::createDirectory(dir)) {
        LOG(LogError) << "GuiArcadeKeybinds: cannot create " << dir;
        ++result.failedCount;
        reportRow("Flycast", "file", "", -1, path, "", "ERROR", "cannot create directory");
        return false;
    }

    std::stringstream out;
    out << "[emulator]\n";
    out << "mapping_name = Keyboard\n";
    out << "dead_zone = 10\n";
    out << "saturation = 100\n";
    out << "rumble_power = 0\n";
    out << "version = 4\n";
    out << "\n";
    out << "[digital]\n";
    int bindIndex {0};
    std::vector<std::string> expectedLines;
    // The game keys from the unified bindings. Keys that cannot be resolved land in
    // the skipped count and keep their default behaviour in Flycast.
    for (const auto& [option, keyName] : binds) {
        const int scan {scancodeOf(keyName)};
        if (scan < 0) {
            result.skippedCount++;
            reportRow("Flycast", option, keyName, -1, "SDL scancode", "", "SKIP",
                      "unknown key");
            LOG(LogInfo) << "GuiArcadeKeybinds: Flycast " << option << ": unknown key " << keyName
                         << ", skipped";
            continue;
        }
        const std::string line {"bind" + std::to_string(bindIndex++) + " = " +
                                std::to_string(scan) + ":" + option};
        out << line << "\n";
        expectedLines.push_back(line);
        reportRow("Flycast", option, keyName, scan, "SDL scancode", std::to_string(scan),
                  "MAPPED");
    }
    // The retained emulation hotkeys match the built-in keyboard defaults (Space =
    // fast forward, Tab = menu, F12 = screenshot) so a fresh mapping file does not
    // lose them. A loaded mapping replaces the defaults entirely.
    const auto addDefault {[&](const char* option, int scan) {
        const std::string line {"bind" + std::to_string(bindIndex++) + " = " +
                                std::to_string(scan) + ":" + option};
        out << line << "\n";
        expectedLines.push_back(line);
        reportRow("Flycast", option, "default", scan, "SDL scancode", std::to_string(scan),
                  "MAPPED", "retained default hotkey");
    }};
    if (binds.find("btn_fforward") == binds.end())
        addDefault("btn_fforward", 44);
    if (binds.find("btn_menu") == binds.end())
        addDefault("btn_menu", 43);
    if (binds.find("btn_screenshot") == binds.end())
        addDefault("btn_screenshot", 69);

    std::string content;
    const bool existed {readFileBytes(path, content)};
    if (existed && content == out.str()) {
        LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
    }
    else if (!backupAndWrite(path, out.str())) {
        ++result.failedCount;
        reportRow("Flycast", "file", "", -1, path, "", "ERROR", "write failed");
        return false;
    }
    else {
        result.touched = true;
    }

    std::string written;
    if (!readFileBytes(path, written)) {
        ++result.failedCount;
        reportRow("Flycast", "file", "", -1, path, "", "ERROR", "cannot read back");
        return false;
    }
    for (const std::string& expected : expectedLines) {
        if (written.find(expected) != std::string::npos) {
            ++result.verifiedCount;
            reportRow("Flycast", "verify", "", -1, path, expected, "OK",
                      "expected=" + expected);
        }
        else {
            ++result.failedCount;
            reportRow("Flycast", "verify", "", -1, path, "", "MISSING",
                      "expected=" + expected);
            LOG(LogError) << "GuiArcadeKeybinds: Flycast " << path
                          << " is missing after write: " << expected;
        }
    }
    return result.failedCount == 0;
}
} // namespace

///////////////////////////////////////////////////////////////////////////////
// Per emulator writers
///////////////////////////////////////////////////////////////////////////////

namespace {
struct SubBind {
    const char* demulKey;
    const char* unifiedKey;
    bool optional {false};
};

// Demul: [JAMMA0_0]/[JAMMA0_1] use evdev codes, [GLOBAL0] holds the machine keys.
std::string demulIniPath()
{
    return Utils::FileSystem::getSystemHomeDirectory() + "/Applications/demul/padDemul.ini";
}

// Builds the patch list for one binding set. Shared by the global writer and the per
// game snapshots so both always translate the keys the exact same way.
std::vector<KeyPatch> demulPatches(const ArcadeKeybindStore& store,
                                   const std::string& emulator,
                                   EmuResult& result)
{
    auto evdevOf {[&store, &result, &emulator](const std::string& prefix,
                                               const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr) {
            result.skippedCount++;
            reportRow(emulator, prefix + unifiedKey, value, scan, "evdev", "", "SKIP",
                      "unsupported key (no Demul evdev mapping)");
            LOG(LogInfo) << "GuiArcadeKeybinds: " << emulator << " " << prefix << unifiedKey
                         << ": unsupported key " << value;
            return std::string("");
        }
        const std::string evdev {std::to_string(entry->evdev_)};
        reportRow(emulator, prefix + unifiedKey, value, scan, "evdev", evdev, "MAPPED");
        return evdev;
    }};

    const std::vector<std::pair<const char*, const char*>> jamm {
        {"JAMMA0_0", "p1."}, {"JAMMA0_1", "p2."}};
    std::vector<KeyPatch> patches;
    // The analog steering axis follows the same keys as the digital dpad; racing
    // titles (Gaelco GA3D, ...) read ANALOGLEFT/ANALOGRIGHT instead of the
    // DIGITAL* pair, so both must be kept in sync.
    struct DigitalKey {
        const char* demulKey;
        const char* unifiedKey;
        const char* analogKey;
    };
    static const std::vector<DigitalKey> digitals {
        {"DIGITALUP", "up", nullptr}, {"DIGITALDOWN", "down", nullptr},
        {"DIGITALLEFT", "left", "ANALOGLEFT"},
        {"DIGITALRIGHT", "right", "ANALOGRIGHT"}};
    for (const auto& [section, prefix] : jamm) {
        for (const auto& key : digitals) {
            const std::string value {evdevOf(prefix, key.unifiedKey)};
            if (value.empty())
                continue;
            patches.push_back({section, key.demulKey, value});
            if (key.analogKey != nullptr)
                patches.push_back({section, key.analogKey, value});
        }
        for (int button {1}; button <= 6; ++button) {
            const std::string sub {"btn" + std::to_string(button)};
            const std::string value {evdevOf(prefix, sub.c_str())};
            if (!value.empty())
                patches.push_back({section, std::string("PUSH") + std::to_string(button), value});
        }
        const std::string coin {evdevOf(prefix, "coin")};
        if (!coin.empty())
            patches.push_back({section, "COIN", coin});
        const std::string start {evdevOf(prefix, "start")};
        if (!start.empty())
            patches.push_back({section, "START", start});
        // The Demul racing axis shares the arrow keys: throttle is analog up, brake
        // analog down, with the plain dpad keys as the fallback.
        std::string throttle {evdevOf(prefix, "throttle")};
        std::string brake {evdevOf(prefix, "brake")};
        if (throttle.empty())
            throttle = evdevOf(prefix, "up");
        if (brake.empty())
            brake = evdevOf(prefix, "down");
        if (!throttle.empty())
            patches.push_back({section, "ANALOGUP", throttle});
        if (!brake.empty())
            patches.push_back({section, "ANALOGDOWN", brake});
    }

    // Twin stick cabinets (Hikaru Planet Harriers) read their two analog sticks from the JOY
    // sections instead of the JAMMA axes: player 1 is JOY0_0, player 2 is JOY0_1, and S1/S2 are
    // the two sticks inside each section.
    static const std::vector<std::pair<const char*, const char*>> joySections {
        {"JOY0_0", "p1."}, {"JOY0_1", "p2."}};
    static const std::vector<std::pair<const char*, const char*>> sticks {
        {"s1up", "S1UP"},       {"s1down", "S1DOWN"},   {"s1left", "S1LEFT"},
        {"s1right", "S1RIGHT"}, {"s2up", "S2UP"},       {"s2down", "S2DOWN"},
        {"s2left", "S2LEFT"},   {"s2right", "S2RIGHT"}};
    for (const auto& [section, prefix] : joySections) {
        for (const auto& [unifiedKey, demulKey] : sticks) {
            const std::string value {evdevOf(prefix, unifiedKey)};
            if (!value.empty())
                patches.push_back({section, demulKey, value});
        }
    }

    // Boost and reset have no Demul bucket, they are skipped on purpose.
    for (const auto& [prefix, key] :
         std::initializer_list<std::pair<const char*, const char*>> {
             {"p1.", "boost"}, {"p2.", "boost"}, {"global.", "reset"}}) {
        const std::string value {store.get(std::string(prefix) + key)};
        if (!value.empty()) {
            ++result.skippedCount;
            reportRow(emulator, std::string(prefix) + key, value, scancodeOf(value),
                      "no Demul slot", "", "SKIP", "no matching Demul option");
        }
    }

    // Global machine keys live in [GLOBAL0]; per player service keys are kept aligned.
    const std::string service {evdevOf("global.", "service")};
    const std::string test {evdevOf("global.", "test")};
    for (const auto& [section, prefix] : jamm) {
        (void)prefix;
        if (!service.empty())
            patches.push_back({section, "SERVICE", service});
    }
    if (!test.empty())
        patches.push_back({"GLOBAL0", "TEST", test});
    if (!service.empty())
        patches.push_back({"GLOBAL0", "SERVICE", service});

    return patches;
}

EmuResult writeDemul(const ArcadeKeybindStore& store)
{
    const std::string path {demulIniPath()};
    EmuResult result;

    const std::vector<KeyPatch> patches {demulPatches(store, "Demul", result)};
    if (patches.empty())
        return result;
    if (!patchIniFile(path, patches, false, result)) {
        if (!result.missing) {
            ++result.failedCount;
            reportRow("Demul", "file", "", -1, path, "", "ERROR", "write failed");
        }
        return result;
    }
    verifyIniPatches("Demul", path, patches, result, false);
    return result;
}

// Demul has a single padDemul.ini and no per game input configuration, so one complete
// copy of it is generated per game and swapped in by demul-wine.sh before the launch.
bool isDemulSystem(SystemData* system)
{
    if (system == nullptr || system->isCollection())
        return false;
    for (const auto& command : system->getSystemEnvData()->mLaunchCommands) {
        const std::string text {Utils::String::toLower(command.second)};
        if (text.find("demul") != std::string::npos)
            return true;
    }
    return false;
}

std::vector<SystemData*> demulSystems()
{
    std::vector<SystemData*> systems;
    for (SystemData* system : SystemData::sSystemVector) {
        if (isDemulSystem(system))
            systems.push_back(system);
    }
    return systems;
}

// The -rom= argument is the file name without directory and extension, lower case.
std::string romNameOf(FileData* file)
{
    std::string name {Utils::FileSystem::getFileName(file->getPath())};
    const size_t dot {name.find_last_of('.')};
    if (dot != std::string::npos)
        name.erase(dot);
    return Utils::String::toLower(name);
}

ArcadeKeybindStore effectiveStore(const ArcadeKeybindStore& global, const std::string& system,
                                  const std::string& rom)
{
    ArcadeKeybindStore effective {global};
    for (const auto& [key, value] : ArcadePerGameStore::getInstance().overridesFor(system, rom)) {
        if (!value.empty())
            effective.set(key, value);
    }
    return effective;
}

int writeDemulSnapshots(const ArcadeKeybindStore& globalStore, bool report)
{
    const std::string basePath {demulIniPath()};
    std::string base;
    if (!readFileBytes(basePath, base)) {
        LOG(LogWarning) << "GuiArcadeKeybinds: cannot read " << basePath
                        << ", skipping the per game Demul profiles";
        return 0;
    }

    const std::string root {ArcadePerGameStore::getSnapshotRoot()};
    int written {0};
    std::set<std::string> demulSystemNames;
    for (SystemData* system : demulSystems()) {
        const std::string systemName {system->getName()};
        demulSystemNames.insert(systemName);
        const std::string directory {root + "/" + systemName};
        std::set<std::string> validRoms;

        for (FileData* file : system->getRootFolder()->getFilesRecursive(GAME)) {
            if (file->getType() != GAME || Utils::FileSystem::isDirectory(file->getPath()))
                continue;
            const std::string rom {romNameOf(file)};
            if (rom.empty())
                continue;
            validRoms.insert(rom);
            if (!Utils::FileSystem::createDirectory(directory))
                continue;

            const std::string target {directory + "/" + rom + ".ini"};
            // Start from the freshly written global ini so every snapshot is complete,
            // then apply this game's overrides on top of it.
            std::string current;
            const bool upToDate {readFileBytes(target, current) && current == base};
            if (!upToDate) {
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    LOG(LogError) << "GuiArcadeKeybinds: cannot write " << target;
                    continue;
                }
                out << base;
            }

            const std::string emulator {"Demul[" + systemName + "/" + rom + "]"};
            const ArcadeKeybindStore effective {effectiveStore(globalStore, systemName, rom)};
            EmuResult result;
            std::vector<KeyPatch> patches {demulPatches(effective, emulator, result)};
            // ATV Track has no action buttons, and Demul's PUSH1-6 defaults are U/I/O/J/K/L,
            // the same keys the throttle and brake use. Without this the throttle key also
            // fires the game's emergency stop, so the six PUSH slots are blanked here.
            if (rom == "atvtrack" || rom == "atvtracka") {
                for (const char* section : {"JAMMA0_0", "JAMMA0_1"}) {
                    for (int slot = 1; slot <= 6; ++slot)
                        patches.push_back({section, "PUSH" + std::to_string(slot), "0"});
                }
            }
            // Gaelco Football reads the stick as ANALOGUP/ANALOGDOWN, and its buttons are the
            // same U/I keys, so pushing the stick up also shot the ball. Blank only the slots
            // that really collide; a button the player reassigns keeps its own key. Games with
            // real action buttons on those keys (Air Trix, ...) are left alone.
            const bool axisWins {rom == "gfootbal"};
            static const std::vector<std::pair<const char*, const char*>> jammaSections {
                {"JAMMA0_0", "p1."}, {"JAMMA0_1", "p2."}};
            static const std::vector<const char*> axisKeys {
                "up", "down", "left", "right", "throttle", "brake", "coin", "start"};
            if (axisWins) {
                for (const auto& [section, prefix] : jammaSections) {
                    std::set<int> axisCodes;
                    for (const char* axisKey : axisKeys) {
                        const std::string name {effective.get(std::string(prefix) + axisKey)};
                        if (name.empty())
                            continue;
                        const int scan {scancodeOf(name)};
                        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
                        if (entry != nullptr)
                            axisCodes.insert(entry->evdev_);
                    }
                    for (int slot = 1; slot <= 6; ++slot) {
                        const std::string name {
                            effective.get(std::string(prefix) + "btn" +
                                           std::to_string(slot))};
                        if (name.empty())
                            continue;
                        const int scan {scancodeOf(name)};
                        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
                        if (entry == nullptr ||
                            axisCodes.find(entry->evdev_) == axisCodes.end())
                            continue;
                        patches.push_back({section, "PUSH" + std::to_string(slot), "0"});
                        if (report)
                            reportRow(emulator,
                                      std::string(prefix) + "btn" + std::to_string(slot), name,
                                      scan, section, "0", "SKIP",
                                      "same key as a movement axis, button slot blanked");
                    }
                }
            }
            patchIniFile(target, patches, false, result);
            if (report)
                reportRow(emulator, "snapshot", rom, -1, target, "",
                          upToDate ? "UNCHANGED" : "WRITTEN", "per game Demul profile");
            ++written;
        }

        // Drop the profiles of games which no longer exist.
        for (const std::string& path : Utils::FileSystem::getDirContent(directory)) {
            if (Utils::FileSystem::getExtension(path) != ".ini")
                continue;
            const size_t dot {path.find_last_of('.')};
            const std::string rom {Utils::String::toLower(
                Utils::FileSystem::getFileName(path.substr(0, dot)))};
            if (validRoms.find(rom) == validRoms.end())
                Utils::FileSystem::removeFile(path);
        }
    }

    // Systems which no longer launch through Demul keep no profiles either.
    for (const std::string& path : Utils::FileSystem::getDirContent(root)) {
        if (!Utils::FileSystem::isDirectory(path))
            continue;
        if (demulSystemNames.find(Utils::FileSystem::getFileName(path)) !=
            demulSystemNames.end())
            continue;
        for (const std::string& profile : Utils::FileSystem::getDirContent(path)) {
            if (Utils::FileSystem::getExtension(profile) == ".ini")
                Utils::FileSystem::removeFile(profile);
        }
    }

    LOG(LogInfo) << "GuiArcadeKeybinds: " << written << " Demul per game profiles in " << root;
    return written;
}

// Dolphin: GCPadNew.ini uses X11 keysym names inside [GCPad1]/[GCPad2].
EmuResult writeDolphin(const ArcadeKeybindStore& store)
{
    const std::string path {
        Utils::FileSystem::getSystemHomeDirectory() +
        "/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu/GCPadNew.ini"};
    EmuResult result;

    auto dolphinOf {[&store, &result](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr) {
            ++result.skippedCount;
            reportRow("Dolphin", prefix + unifiedKey, value, scan, "X11 keysym", "",
                      "SKIP", "unsupported key (no Dolphin keysym)");
            return std::string("");
        }
        // Dolphin stores keyboard bindings as backtick quoted keysyms; a bare name
        // would be parsed as a button of the currently selected input device.
        const std::string keysym {"`" + std::string(entry->dolphin_) + "`"};
        reportRow("Dolphin", prefix + unifiedKey, value, scan, "X11 keysym", keysym,
                  "MAPPED");
        return keysym;
    }};

    std::vector<KeyPatch> patches;
    int applied {0};
    for (const auto& [section, prefix] :
         std::initializer_list<std::pair<const char*, const char*>> {{"GCPad1", "p1."},
                                                                     {"GCPad2", "p2."}}) {
        const std::vector<std::pair<const char*, const char*>> dpad {
            {"Main Stick/Up", "up"}, {"Main Stick/Down", "down"}, {"Main Stick/Left", "left"},
            {"Main Stick/Right", "right"}};
        for (const auto& [key, unifiedKey] : dpad) {
            const std::string value {dolphinOf(prefix, unifiedKey)};
            if (!value.empty()) {
                patches.push_back({section, key, value});
                ++applied;
            }
        }
        // Six pad buttons are five here, button 6 has no Dolphin bucket.
        const std::vector<std::pair<const char*, const char*>> buttons {
            {"Buttons/A", "btn1"}, {"Buttons/B", "btn2"}, {"Buttons/X", "btn3"},
            {"Buttons/Y", "btn4"}, {"Buttons/Z", "btn5"}};
        for (const auto& [key, unifiedKey] : buttons) {
            const std::string value {dolphinOf(prefix, unifiedKey)};
            if (!value.empty()) {
                patches.push_back({section, key, value});
                ++applied;
            }
        }
        const std::vector<std::pair<const char*, const char*>> triforce {
            {"Triforce/Coin", "coin"}, {"Buttons/Start", "start"}, {"Triforce/Test", "test"},
            {"Triforce/Service", "service"}};
        for (const auto& [key, unifiedKey] : triforce) {
            // Test and service are machine level keys shared by both pads, the rest
            // follow the per player bindings.
            const std::string value {dolphinOf(std::string(unifiedKey) == "test" ||
                                                       std::string(unifiedKey) == "service" ?
                                                   "global." :
                                                   prefix,
                                               unifiedKey)};
            if (!value.empty()) {
                patches.push_back({section, key, value});
                ++applied;
            }
        }
        // Throttle, brake, boost and reset have no Dolphin pad bucket.
    }

    for (const auto& [prefix, key] :
         std::initializer_list<std::pair<const char*, const char*>> {
             {"p1.", "throttle"}, {"p1.", "brake"}, {"p1.", "boost"},
             {"p1.", "btn6"}, {"p2.", "throttle"}, {"p2.", "brake"},
             {"p2.", "boost"}, {"p2.", "btn6"}, {"global.", "reset"}}) {
        const std::string value {store.get(std::string(prefix) + key)};
        if (!value.empty()) {
            ++result.skippedCount;
            reportRow("Dolphin", std::string(prefix) + key, value, scancodeOf(value),
                      "no Dolphin slot", "", "SKIP", "no matching Dolphin option");
        }
    }

    if (applied == 0)
        return result;
    if (!patchIniFile(path, patches, true, result)) {
        if (!result.missing) {
            ++result.failedCount;
            reportRow("Dolphin", "file", "", -1, path, "", "ERROR", "write failed");
        }
        return result;
    }
    verifyIniPatches("Dolphin", path, patches, result, true);
    return result;
}

// Play! uses integer Qt keys inside inputprofiles/default.xml, providerId 'QtKy',
// keyType BUTTON (0), bindingtype SIMPLE (1).
EmuResult writePlay(const ArcadeKeybindStore& store)
{
    const std::string path {
        Utils::FileSystem::getSystemHomeDirectory() +
        "/.var/app/org.purei.Play/config/Play Data Files/inputprofiles/default.xml"};
    EmuResult result;
    constexpr int kQtKyProvider {1367943289}; // 'QtKy' packed as uint32.

    auto qtOf {[&store, &result](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return -1;
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr) {
            ++result.skippedCount;
            reportRow("Play!", prefix + unifiedKey, value, scan, "Qt key", "", "SKIP",
                      "unsupported key (no Qt keysym)");
            return -1;
        }
        reportRow("Play!", prefix + unifiedKey, value, scan, "Qt key",
                  std::to_string(entry->qtKey_), "MAPPED");
        return entry->qtKey_;
    }};

    int applied {0};
    for (const auto& [pad, prefix] :
         std::initializer_list<std::pair<const char*, const char*>> {{"pad1", "p1."},
                                                                     {"pad2", "p2."}}) {
        std::map<std::string, std::string> actions;
        const std::vector<std::pair<const char*, const char*>> dpad {
            {"dpad_up", "up"}, {"dpad_down", "down"}, {"dpad_left", "left"},
            {"dpad_right", "right"}};
        for (const auto& [action, unifiedKey] : dpad) {
            const int qt {qtOf(prefix, unifiedKey)};
            if (qt > 0) {
                actions[action] = std::to_string(qt) + ":" + std::to_string(kQtKyProvider) +
                                  ":0:1";
                ++applied;
            }
        }
        // Six buttons across the cross/circle/square/triangle/l1/r1 pad buckets.
        const std::vector<std::pair<const char*, const char*>> buttons {
            {"cross", "btn1"}, {"circle", "btn2"}, {"square", "btn3"},
            {"triangle", "btn4"}, {"l1", "btn5"}, {"r1", "btn6"}};
        for (const auto& [action, unifiedKey] : buttons) {
            const int qt {qtOf(prefix, unifiedKey)};
            if (qt > 0) {
                actions[action] = std::to_string(qt) + ":" + std::to_string(kQtKyProvider) +
                                  ":0:1";
                ++applied;
            }
        }
        const int startKey {qtOf(prefix, "start")};
        if (startKey > 0) {
            actions["start"] =
                std::to_string(startKey) + ":" + std::to_string(kQtKyProvider) + ":0:1";
            ++applied;
        }
        // Coin, throttle, brake, boost, service, test and reset have no console pad
        // bucket and stay with whatever the Play! defaults provide.
        if (actions.empty())
            continue;
        if (!patchPlayProfile(path, pad, actions, result)) {
            if (!result.missing) {
                ++result.failedCount;
                reportRow("Play!", "file", "", -1, path, "", "ERROR", "write failed");
            }
            continue;
        }
        verifyPlayProfile(path, pad, actions, result);
    }

    for (const auto& [prefix, key] :
         std::initializer_list<std::pair<const char*, const char*>> {
             {"p1.", "coin"}, {"p1.", "throttle"}, {"p1.", "brake"}, {"p1.", "boost"},
             {"p1.", "service"}, {"p1.", "test"}, {"p1.", "reset"}, {"p2.", "coin"},
             {"p2.", "throttle"}, {"p2.", "brake"}, {"p2.", "boost"}, {"p2.", "service"},
             {"p2.", "test"}, {"p2.", "reset"}, {"global.", "service"}, {"global.", "test"},
             {"global.", "reset"}}) {
        const std::string value {store.get(std::string(prefix) + key)};
        if (!value.empty()) {
            ++result.skippedCount;
            reportRow("Play!", std::string(prefix) + key, value, scancodeOf(value),
                      "no Play! slot", "", "SKIP", "no matching Play! option");
        }
    }
    if (applied == 0)
        result.missing = true;
    return result;
}

// Default controls.ini generated by `lindbergh-loader --create controls`. Used when
// the shared file does not exist yet, so the loader gets a complete binding set
// instead of a partial file with missing actions.
const char* kLindberghTemplate {R"LBT([Config]
Steer_DeadZone = 8000
Gas_DeadZone = 500
Brake_DeadZone = 500
ABC_X_DeadZone = 8000
ABC_Y_DeadZone = 8000
Throttle_DeadZone = 8000
ShakeIncreaseRate = 10.0
ShakeDecayRate = 0.95

[Common]
Test = KEY_T
P1_Coin = KEY_5
P2_Coin = KEY_6
P1_Start = KEY_1, JOY0_BUTTON_11, GC0_BUTTON_START
P2_Start = KEY_2
P1_Service = KEY_S, JOY0_BUTTON_10, GC0_BUTTON_BACK

[Digital]
P1_Up = KEY_Up, GC0_BUTTON_DPUP, GC0_AXIS_LEFTY_NEGATIVE, JOY0_HAT0_UP, JOY0_AXIS_1_NEGATIVE
P1_Down = KEY_Down, GC0_BUTTON_DPDOWN, GC0_AXIS_LEFTY_POSITIVE, JOY0_HAT0_DOWN, JOY0_AXIS_1_POSITIVE
P1_Left = KEY_Left, GC0_BUTTON_DPLEFT, GC0_AXIS_LEFTX_NEGATIVE, JOY0_HAT0_LEFT, JOY0_AXIS_0_NEGATIVE
P1_Right = KEY_Right, GC0_BUTTON_DPRIGHT, GC0_AXIS_LEFTX_POSITIVE, JOY0_HAT0_RIGHT, JOY0_AXIS_0_POSITIVE
P1_Button1 = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0
P1_Button2 = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1
P1_Button3 = KEY_E, GC0_BUTTON_X, JOY0_BUTTON_2
P1_Card1Insert = KEY_F7, GC0_BUTTON_GUIDE, JOY0_BUTTON_11
P2_Card2Insert = KEY_F8, GC1_BUTTON_GUIDE, JOY1_BUTTON_11

[Driving]
P1_Steer_Left = KEY_Left
P1_Steer_Right = KEY_Right
P1_Gas_Digital = KEY_Up
P1_Brake_Digital = KEY_Down
ViewChange = KEY_V, GC0_BUTTON_Y, JOY0_BUTTON_2
Boost = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0
BoostRight = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1
GearUp = KEY_A, GC0_BUTTON_RIGHTSHOULDER, JOY0_BUTTON_5
GearDown = KEY_Z, GC0_BUTTON_LEFTSHOULDER, JOY0_BUTTON_4
MusicChange = KEY_M, GC0_BUTTON_X, JOY0_BUTTON_3
Up = KEY_I, GC0_BUTTON_DPUP, JOY0_HAT0_UP
Down = KEY_K, GC0_BUTTON_DPDOWN, JOY0_HAT0_DOWN
Left = KEY_J, GC0_BUTTON_DPLEFT, JOY0_HAT0_LEFT
Right = KEY_L, GC0_BUTTON_DPRIGHT, JOY0_HAT0_RIGHT
CardInsert = KEY_F7, GC0_BUTTON_GUIDE, JOY0_BUTTON_12
P1_Steer = GC0_AXIS_LEFTX, JOY0_AXIS_0
P1_Gas = GC0_AXIS_RIGHTTRIGGER, JOY0_AXIS_5
P1_Brake = GC0_AXIS_LEFTTRIGGER, JOY0_AXIS_2

[ABC]
ABC_Left = KEY_Left
ABC_Right = KEY_Right
ABC_Up = KEY_Up
ABC_Down = KEY_Down
Throttle_Accelerate = KEY_A
Throttle_Slowdown = KEY_Z
GunTrigger = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0
MissileTrigger = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1
ClimaxSwitch = KEY_E, GC0_BUTTON_Y, JOY0_BUTTON_4
ABC_X = GC0_AXIS_LEFTX, JOY0_AXIS_0
ABC_Y = GC0_AXIS_LEFTY, JOY0_AXIS_1
Throttle = GC0_AXIS_RIGHTY_INVERTED, GC0_AXIS_RIGHTTRIGGER_POSITIVE_HALF, GC0_AXIS_LEFTTRIGGER_NEGATIVE_HALF, JOY0_AXIS_4_INVERTED, JOY0_AXIS_5_POSITIVE_HALF, JOY0_AXIS_4_NEGATIVE_HALF

[Shooting]
P1_GunX = MOUSE_AXIS_X
P1_GunY = MOUSE_AXIS_Y
P1_Trigger = MOUSE_LEFT_BUTTON, KEY_Q
P1_Reload = MOUSE_RIGHT_BUTTON, KEY_W
P1_GunButton = MOUSE_MIDDLE_BUTTON, KEY_E
P1_ActionButton = KEY_R
P1_PedalLeft = KEY_Left
P1_PedalRight = KEY_Right

[Mahjong]
ButtonA = KEY_Y
ButtonB = KEY_U
ButtonC = KEY_I
ButtonD = KEY_O
ButtonE = KEY_G
ButtonF = KEY_H
ButtonG = KEY_J
ButtonH = KEY_K
ButtonI = KEY_L
ButtonJ = KEY_V
ButtonK = KEY_B
ButtonL = KEY_N
ButtonM = KEY_M
ButtonN = KEY_,
ButtonChi = KEY_F1
ButtonPon = KEY_F2
ButtonKan = KEY_F3
ButtonReach = KEY_F4
ButtonAgari = KEY_F5
ButtonCancel = KEY_F6
CardInsert = KEY_F7
)LBT"};

// Lindbergh: controls.ini is read from the working directory of the loader, which
// ~/tools/lindbergh-launch.sh sets to the per game root. One shared file in the
// lindbergh folder is patched and the launcher links it into the other roots.
EmuResult writeLindbergh(const ArcadeKeybindStore& store)
{
    const std::string base {
        Utils::FileSystem::getSystemHomeDirectory() + "/roms/emus/lindbergh"};
    const std::string path {base + "/controls.ini"};
    EmuResult result;

    // A missing file is created from the loader's own default template. Without it
    // the loader falls back to its built-in bindings and ignores our values.
    if (!Utils::FileSystem::exists(path)) {
        if (!Utils::FileSystem::exists(base)) {
            result.missing = true;
            LOG(LogWarning) << "GuiArcadeKeybinds: " << base << " not found";
            return result;
        }
        if (!backupAndWrite(path, kLindberghTemplate))
            return result;
        result.touched = true;
        LOG(LogInfo) << "GuiArcadeKeybinds: created " << path << " from the default template";
    }

    auto loaderKey {[&store, &result](const std::string& prefix,
                                      const char* unifiedKey) -> std::string {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        if (scan < 0 || findEntry(scan) == nullptr) {
            ++result.skippedCount;
            reportRow("Lindbergh", prefix + unifiedKey, value, scan, "SDL scancode name", "",
                      "SKIP", "unsupported key (no SDL scancode name)");
            return std::string("");
        }
        // The loader resolves KEY_<name> through SDL_GetScancodeFromName, so the SDL
        // scancode name ("Left Ctrl", "Up", "5") is the token it expects here.
        const char* name {SDL_GetScancodeName(static_cast<SDL_Scancode>(scan))};
        if (name == nullptr || *name == '\0') {
            ++result.skippedCount;
            reportRow("Lindbergh", prefix + unifiedKey, value, scan, "SDL scancode name", "",
                      "SKIP", "SDL returned an empty scancode name");
            return std::string("");
        }
        reportRow("Lindbergh", prefix + unifiedKey, value, scan, "SDL scancode name", name,
                  "MAPPED");
        return std::string(name);
    }};

    std::vector<KeyPatch> patches;
    if (const std::string value {loaderKey("p1.", "coin")}; !value.empty())
        patches.push_back({"Common", "P1_Coin", value, true});
    if (const std::string value {loaderKey("p2.", "coin")}; !value.empty())
        patches.push_back({"Common", "P2_Coin", value, true});
    if (const std::string value {loaderKey("p1.", "start")}; !value.empty())
        patches.push_back({"Common", "P1_Start", value, true});
    if (const std::string value {loaderKey("p2.", "start")}; !value.empty())
        patches.push_back({"Common", "P2_Start", value, true});
    if (const std::string value {loaderKey("global.", "service")}; !value.empty())
        patches.push_back({"Common", "P1_Service", value, true});
    if (const std::string value {loaderKey("global.", "test")}; !value.empty())
        patches.push_back({"Common", "Test", value, true});

    const std::vector<std::pair<const char*, const char*>> digital {
        {"P1_Up", "up"}, {"P1_Down", "down"}, {"P1_Left", "left"}, {"P1_Right", "right"},
        {"P1_Button1", "btn1"}, {"P1_Button2", "btn2"}, {"P1_Button3", "btn3"}};
    for (const auto& [key, unifiedKey] : digital) {
        const std::string value {loaderKey("p1.", unifiedKey)};
        if (!value.empty())
            patches.push_back({"Digital", key, value, true});
    }
    const std::vector<std::pair<const char*, const char*>> driving {
        {"P1_Gas_Digital", "throttle"}, {"P1_Brake_Digital", "brake"},
        {"P1_Steer_Left", "left"}, {"P1_Steer_Right", "right"}};
    for (const auto& [key, unifiedKey] : driving) {
        const std::string value {loaderKey("p1.", unifiedKey)};
        if (!value.empty())
            patches.push_back({"Driving", key, value, true});
    }
    if (const std::string value {loaderKey("p1.", "boost")}; !value.empty())
        patches.push_back({"Driving", "Boost", value, true});
    // P2 directional and button entries are not part of the loader template.

    for (const auto& [prefix, key] :
         std::initializer_list<std::pair<const char*, const char*>> {
             {"p2.", "up"}, {"p2.", "down"}, {"p2.", "left"}, {"p2.", "right"},
             {"p2.", "btn1"}, {"p2.", "btn2"}, {"p2.", "btn3"}, {"p2.", "throttle"},
             {"p2.", "brake"}, {"p2.", "boost"}, {"global.", "reset"}}) {
        const std::string value {store.get(std::string(prefix) + key)};
        if (!value.empty()) {
            ++result.skippedCount;
            reportRow("Lindbergh", std::string(prefix) + key, value, scancodeOf(value),
                      "no Lindbergh slot", "", "SKIP", "no matching Lindbergh option");
        }
    }

    if (patches.empty())
        return result;
    if (!patchIniFile(path, patches, true, result)) {
        if (!result.missing) {
            ++result.failedCount;
            reportRow("Lindbergh", "file", "", -1, path, "", "ERROR", "write failed");
        }
        return result;
    }
    verifyIniPatches("Lindbergh", path, patches, result, true);
    return result;
}

// RetroArch: per-core remap files keep the arcade layout out of the global
// retroarch.cfg, so non arcade cores keep their own bindings. When a core starts,
// RetroArch loads <input_remapping_directory>/<core>/<core>.rmp.
EmuResult writeRetroArch(const ArcadeKeybindStore& store)
{
    const std::string retroarchDir {
        Utils::FileSystem::getSystemHomeDirectory() +
        "/.var/app/org.libretro.RetroArch/config/retroarch"};
    const std::string configPath {retroarchDir + "/retroarch.cfg"};
    EmuResult result;

    std::string config;
    if (!readFileBytes(configPath, config)) {
        result.missing = true;
        LOG(LogWarning) << "GuiArcadeKeybinds: " << configPath << " not found";
        return result;
    }

    // input_remapping_directory is "~/.var/app/..." on this station; relative values
    // are resolved against the RetroArch configuration directory.
    std::string remapDirectory;
    {
        std::istringstream stream {config};
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("input_remapping_directory", 0) != 0)
                continue;
            const size_t equals {line.find('=')};
            if (equals == std::string::npos)
                break;
            std::string value {line.substr(equals + 1)};
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.erase(value.begin());
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                                      value.back() == '\r'))
                value.pop_back();
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            if (!value.empty() && value.front() == '~')
                value = Utils::FileSystem::getSystemHomeDirectory() + value.substr(1);
            remapDirectory = value;
            break;
        }
    }
    if (remapDirectory.empty())
        remapDirectory = retroarchDir + "/config/remaps";
    else if (remapDirectory.front() != '/')
        remapDirectory = retroarchDir + "/" + remapDirectory;

    // With remap_save_on_exit enabled RetroArch rewrites a loaded .rmp file when the
    // core shuts down and that rewrite only keeps the remap structure, dropping every
    // input_player*_* key binding. The frontend owns these files, so disable it.
    if (setTopLevelValue(config, "remap_save_on_exit", "false")) {
        if (backupAndWrite(configPath, config)) {
            result.touched = true;
            reportRow("RetroArch", "config", "remap_save_on_exit", -1, configPath, "false",
                      "WRITTEN", "frontend owns the remap files");
        }
        else {
            ++result.failedCount;
            reportRow("RetroArch", "config", "remap_save_on_exit", -1, configPath, "",
                      "ERROR", "cannot update retroarch.cfg");
            LOG(LogError) << "GuiArcadeKeybinds: cannot update " << configPath;
        }
    }

    // System RetroArch (used by ES-DE launch command) also needs Port 1 set to
    // keyboard so Flycast receives keyboard input. Flycast ignores .rmp remaps for
    // arcade controls; the port device type drives the mapping.
    {
        const std::string systemConfigPath {
            Utils::FileSystem::getSystemHomeDirectory() + "/.config/retroarch/retroarch.cfg"};
        std::string systemConfig;
        if (readFileBytes(systemConfigPath, systemConfig)) {
            if (setTopLevelValue(systemConfig, "input_libretro_device_port1", "3")) {
                if (backupAndWrite(systemConfigPath, systemConfig)) {
                    result.touched = true;
                    reportRow("RetroArch", "system-config", "input_libretro_device_port1", -1,
                              systemConfigPath, "3", "WRITTEN",
                              "Flycast keyboard input via port device type");
                }
                else {
                    ++result.failedCount;
                    reportRow("RetroArch", "system-config", "input_libretro_device_port1", -1,
                              systemConfigPath, "", "ERROR", "cannot update system retroarch.cfg");
                    LOG(LogError) << "GuiArcadeKeybinds: cannot update " << systemConfigPath;
                }
            }
        }
        else {
            result.missing = true;
            reportRow("RetroArch", "system-config", "input_libretro_device_port1", -1,
                      systemConfigPath, "", "MISSING", "system retroarch.cfg not found");
            LOG(LogWarning) << "GuiArcadeKeybinds: " << systemConfigPath << " not found";
        }
    }

    // Read the setting back: a later write or an unusual quoting style must not leave
    // remap_save_on_exit enabled, because RetroArch then truncates the .rmp files.
    {
        std::string check;
        if (readFileBytes(configPath, check)) {
            bool found {false};
            std::string actual;
            std::istringstream stream {check};
            std::string line;
            while (std::getline(stream, line)) {
                if (line.rfind("remap_save_on_exit", 0) != 0)
                    continue;
                const size_t equals {line.find('=')};
                if (equals == std::string::npos)
                    continue;
                actual = line.substr(equals + 1);
                while (!actual.empty() && (actual.front() == ' ' || actual.front() == '\t'))
                    actual.erase(actual.begin());
                while (!actual.empty() && (actual.back() == ' ' || actual.back() == '\t' ||
                                          actual.back() == '\r'))
                    actual.pop_back();
                if (actual.size() >= 2 && actual.front() == '"' && actual.back() == '"')
                    actual = actual.substr(1, actual.size() - 2);
                found = true;
                break;
            }
            if (!found || actual != "false") {
                ++result.failedCount;
                reportRow("RetroArch", "verify", "remap_save_on_exit", -1, configPath,
                          actual, "MISMATCH", "expected=false");
                LOG(LogError) << "GuiArcadeKeybinds: remap_save_on_exit is " << actual
                              << ", expected false";
            }
            else {
                ++result.verifiedCount;
                reportRow("RetroArch", "verify", "remap_save_on_exit", -1, configPath,
                          actual, "OK", "expected=false");
            }
        }
        else {
            ++result.failedCount;
            reportRow("RetroArch", "verify", "remap_save_on_exit", -1, configPath, "",
                      "ERROR", "cannot read back");
        }
    }

    const auto keyFor {[&store, &result](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string();
        const int scan {scancodeOf(value)};
        if (scan < 0 || findEntry(scan) == nullptr) {
            ++result.skippedCount;
            reportRow("RetroArch", prefix + unifiedKey, value, scan, "RetroArch key", "",
                      "SKIP", "unsupported key (no RetroArch key name)");
            return std::string();
        }
        const std::string key {retroArchKeyName(scan)};
        if (key.empty()) {
            ++result.skippedCount;
            reportRow("RetroArch", prefix + unifiedKey, value, scan, "RetroArch key", "",
                      "SKIP", "empty RetroArch key name");
            return std::string();
        }
        reportRow("RetroArch", prefix + unifiedKey, value, scan, "RetroArch key", key,
                  "MAPPED");
        return key;
    }};

    std::stringstream remap;
    const auto add {[&remap](const char* player, const char* action, const std::string& key) {
        if (!key.empty())
            remap << "input_" << player << "_" << action << " = \"" << key << "\"\n";
    }};
    const auto addPlayer {[&](const char* player, const std::string& prefix) {
        add(player, "select", keyFor(prefix, "coin"));
        add(player, "start", keyFor(prefix, "start"));
        add(player, "up", keyFor(prefix, "up"));
        add(player, "down", keyFor(prefix, "down"));
        add(player, "left", keyFor(prefix, "left"));
        add(player, "right", keyFor(prefix, "right"));
        // Arcade cores map the six buttons as B/A/Y/X/L/R (button 1 first).
        add(player, "b", keyFor(prefix, "btn1"));
        add(player, "a", keyFor(prefix, "btn2"));
        add(player, "y", keyFor(prefix, "btn3"));
        add(player, "x", keyFor(prefix, "btn4"));
        add(player, "l", keyFor(prefix, "btn5"));
        add(player, "r", keyFor(prefix, "btn6"));
        // Racing pedals live on the analog trigger slots.
        add(player, "l2", keyFor(prefix, "throttle"));
        add(player, "r2", keyFor(prefix, "brake"));
    }};
    addPlayer("player1", "p1.");
    addPlayer("player2", "p2.");

    // Boost and the machine keys have no libretro joypad slot; count them once.
    for (const auto& [prefix, key] :
         std::initializer_list<std::pair<const char*, const char*>> {
             {"p1.", "boost"}, {"p2.", "boost"}, {"global.", "service"},
             {"global.", "test"}, {"global.", "reset"}}) {
        const std::string value {store.get(std::string(prefix) + key)};
        if (!value.empty()) {
            ++result.skippedCount;
            reportRow("RetroArch", std::string(prefix) + key, value, scancodeOf(value),
                      "no libretro slot", "", "SKIP", "no matching RetroArch joypad slot");
        }
    }

    // Derive remap file/directory names from the actual installed core .so files,
    // so the file paths match RetroArch's core-name lookup. RetroArch looks up
    // {input_remapping_directory}/<core_display_name>/<core_display_name>.rmp
    // where <core_display_name> is the .so basename without _libretro suffix,
    // first letter capitalised (e.g. flycast_libretro.so -> "Flycast").
    // This matches the core option directory names under config/ (e.g. config/Flycast/).
    std::map<std::string, std::string> coreBasenameToDisplayName;
    std::vector<std::string> installedCores;
    {
        const std::string coresDir {retroarchDir + "/cores"};
        Utils::FileSystem::StringList soFiles = Utils::FileSystem::getDirContent(coresDir);
        for (const std::string& entry : soFiles) {
            if (entry.find(".so") == std::string::npos)
                continue;
            std::string basename {entry};
            const size_t lastSlash {basename.find_last_of('/')};
            if (lastSlash != std::string::npos)
                basename = basename.substr(lastSlash + 1);
            if (basename.rfind(".so", basename.size() - 3) != std::string::npos)
                basename = basename.substr(0, basename.size() - 3);
            // Strip _libretro suffix for display name
            const std::string suffix{"_libretro"};
            if (basename.size() > suffix.size() &&
                basename.compare(basename.size() - suffix.size(), suffix.size(), suffix) == 0)
                basename = basename.substr(0, basename.size() - suffix.size());
            // Capitalise first letter for core display name (matches RetroArch lookup)
            if (!basename.empty())
                basename.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(basename.front())));
            std::string lower {basename};
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            installedCores.push_back(lower);
            coreBasenameToDisplayName[lower] = basename;
        }
    }

    std::string previous;
    int coreCount {0};
    for (const std::string& coreBasename : installedCores) {
        const std::string displayName {coreBasenameToDisplayName.at(coreBasename)};
        // Only process arcade-relevant cores that have option directories.
        const std::string coreOptionDirectory {retroarchDir + "/config/" + displayName};
        const std::string remapDirName {displayName};  // Use core display name for remap path
        const std::string targetDirectory {remapDirectory + "/" + remapDirName};
        const std::string path {targetDirectory + "/" + remapDirName + ".rmp"};
        const std::string rootPath {remapDirectory + "/" + remapDirName + ".rmp"};
        if (!Utils::FileSystem::exists(coreOptionDirectory))
            continue;
        ++coreCount;
        if (!Utils::FileSystem::exists(targetDirectory) &&
            !Utils::FileSystem::createDirectory(targetDirectory)) {
            ++result.failedCount;
            reportRow("RetroArch", "core", displayName.c_str(), -1, targetDirectory, "", "ERROR",
                      "cannot create remap directory");
            LOG(LogError) << "GuiArcadeKeybinds: cannot create " << targetDirectory;
            continue;
        }
        const bool existed {readFileBytes(path, previous)};
        if (existed && previous == remap.str()) {
            LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
        }
        else if (!backupAndWrite(path, remap.str())) {
            ++result.failedCount;
            reportRow("RetroArch", "core", displayName.c_str(), -1, path, "", "ERROR",
                      "write failed");
            continue;
        }
        else {
            result.touched = true;
        }

        // Also write to remaps root so RetroArch's auto-loader (which looks up
        // {input_remapping_directory}/{core_name}.rmp) finds the file.
        const bool rootExisted {readFileBytes(rootPath, previous)};
        if (!rootExisted || previous != remap.str()) {
            if (!backupAndWrite(rootPath, remap.str())) {
                ++result.failedCount;
                reportRow("RetroArch", "core", displayName.c_str(), -1, rootPath, "", "ERROR",
                          "root write failed");
                continue;
            }
            result.touched = true;
        }

        std::string written;
        if (!readFileBytes(path, written) || written != remap.str()) {
            ++result.failedCount;
            reportRow("RetroArch", "core", displayName.c_str(), -1, path, "", "MISMATCH",
                      "read-back content differs");
            LOG(LogError) << "GuiArcadeKeybinds: " << path
                          << " does not match the generated remap after write";
            continue;
        }
        std::string rootWritten;
        if (!readFileBytes(rootPath, rootWritten) || rootWritten != remap.str()) {
            ++result.failedCount;
            reportRow("RetroArch", "core", displayName.c_str(), -1, rootPath, "", "MISMATCH",
                      "root read-back content differs");
            LOG(LogError) << "GuiArcadeKeybinds: " << rootPath
                          << " does not match the generated remap after write";
            continue;
        }
        ++result.verifiedCount;
        reportRow("RetroArch", "core", displayName.c_str(), -1, path, "remap", "OK",
                  "read-back content matches");
        reportRow("RetroArch", "core", displayName.c_str(), -1, rootPath, "remap", "OK",
                  "root read-back content matches");
    }
    if (coreCount == 0) {
        result.missing = true;
        LOG(LogWarning) << "GuiArcadeKeybinds: no RetroArch arcade core found in "
                        << retroarchDir << "/config";
    }
    return result;
}
} // namespace

///////////////////////////////////////////////////////////////////////////////
// Save orchestration
///////////////////////////////////////////////////////////////////////////////

namespace {
// demulOnly is used by the per game pages: those profiles only describe Demul input, so
// no other emulator configuration is written or reported from there.
void applyAllConfigs(Window* window, bool report, bool demulOnly = false)
{
    ArcadePerGameStore::getInstance().load();
    const ArcadeKeybindStore& store {ArcadeKeybindStore::getInstance()};
    if (report)
        beginReport(ArcadeKeybindStore::getPath());

    // The global ini is rewritten first in both modes because the per game copies use it
    // as their base.
    const EmuResult demul {writeDemul(store)};

    EmuResult dolphin;
    EmuResult flycast;
    EmuResult play;
    EmuResult lindbergh;
    EmuResult retroarch;
    int perGameProfiles {0};

    if (!demulOnly) {
        dolphin = writeDolphin(store);
        play = writePlay(store);

        // Flycast: the standalone SDL keyboard hangs off port 0 only, so P1 is written
        // while P2 keeps its defaults (devdoc/43 section 2.6).
        {
            std::map<std::string, std::string> binds;
            const auto addBind {[&store, &binds](const char* option, const char* unifiedKey) {
                const std::string value {store.get(std::string("p1.") + unifiedKey)};
                if (value.empty())
                    return false;
                const int scan {scancodeOf(value)};
                if (scan < 0 || findEntry(scan) == nullptr)
                    return false;
                binds[option] = value;
                // Analog mirrors of the dpad keys give plain arrows for the stick axes too.
                const std::string dpad {"btn_dpad1_"};
                if (std::string(option).rfind("btn_", 0) == 0 &&
                    std::string(option).compare(0, dpad.size(), dpad) == 0) {
                    const std::string direction {std::string(option).substr(dpad.size())};
                    binds["btn_analog_" + direction] = value;
                }
                return true;
            }};
            addBind("btn_d", "coin");
            addBind("btn_start", "start");
            addBind("btn_dpad1_up", "up");
            addBind("btn_dpad1_down", "down");
            addBind("btn_dpad1_left", "left");
            addBind("btn_dpad1_right", "right");
            addBind("btn_a", "btn1");
            addBind("btn_b", "btn2");
            addBind("btn_x", "btn3");
            addBind("btn_y", "btn4");
            addBind("btn_z", "btn5");
            addBind("btn_c", "btn6");
            addBind("btn_trigger_right", "throttle");
            addBind("btn_trigger_left", "brake");
            // Boost, service, test and reset have no Flycast mapping bucket.
            if (!binds.empty()) {
                const std::string home {Utils::FileSystem::getSystemHomeDirectory()};
                // System RetroArch (launched by ES-DE) loads Flycast's SDL keyboard mapping
                // from the system config home, not the flatpak sandbox. Write to both
                // likely locations so the libretro core finds it regardless of which
                // home-folder convention the compiled core uses.
                const std::vector<std::string> flycastPaths {
                    home + "/.config/flycast/mappings/SDL_Keyboard.cfg",
                    home + "/.config/retroarch/config/flycast/mappings/SDL_Keyboard.cfg",
                };
                for (const auto& path : flycastPaths) {
                    writeFlycastMapping(path, binds, flycast);
                }
            }
        }

        lindbergh = writeLindbergh(store);
        retroarch = writeRetroArch(store);
    }

    // Runs after the global ini was rewritten so the per game copies always start from
    // the current global setup, this is what keeps both in sync on a single SAVE.
    perGameProfiles = writeDemulSnapshots(store, report);

    if (report)
        endReport(demul, dolphin, flycast, play, lindbergh, retroarch, demulOnly);

    if (!report || window == nullptr) {
        LOG(LogInfo) << "GuiArcadeKeybinds: applied all configs (report off)";
        return;
    }

    const auto line {[&](const char* name, const EmuResult& result) {
        std::stringstream row;
        row << name << ": ";
        if (result.missing)
            row << _("CONFIGURATION FILE NOT FOUND");
        else if (result.touched)
            row << _("WRITTEN");
        else
            row << _("UNCHANGED");
        if (result.skippedCount > 0)
            row << " - " << result.skippedCount << " " << _("SKIPPED")
                << " (" << _("SEE LOG") << ")";
        if (result.failedCount > 0)
            row << " - " << result.failedCount << " " << _("FAILED") << " (" << _("SEE LOG")
                << ")";
        return row.str();
    }};

    std::stringstream message;
    if (demulOnly) {
        // Per game pages only ever affect Demul, so no other emulator is mentioned here.
        message << line("Demul", demul) << "\n"
                << _("PER-GAME DEMUL PROFILES") << ": " << perGameProfiles << "\n"
                << "\n"
                << _("ONLY THE DEMUL CONFIGURATION AND ITS PER-GAME PROFILES ARE WRITTEN "
                     "FROM THIS PAGE");
    }
    else {
        message << line("Demul", demul) << "\n" << line("Dolphin", dolphin) << "\n"
                << line("Flycast", flycast) << " (" << _("PLAYER 1 ONLY") << ")"
                << "\n"
                << line("Play!", play) << "\n"
                << line("Lindbergh", lindbergh) << "\n"
                << line("RetroArch", retroarch) << "\n"
                << _("PER-GAME DEMUL PROFILES") << ": " << perGameProfiles << "\n"
                << "\n"
                << _("SOME ENTRIES ARE SKIPPED WHEN THE EMULATOR HAS NO MATCHING OPTION; "
                     "DETAILS ARE IN THE LOG");
    }

    LOG(LogInfo) << "GuiArcadeKeybinds: save summary:\n" << message.str();
    window->pushGui(new GuiMsgBox(message.str(), _("OK"), nullptr, "", nullptr, "", nullptr,
                                  "", nullptr, nullptr, true, true));
}
} // namespace

///////////////////////////////////////////////////////////////////////////////
// GuiArcadeKeybinds (main page)
///////////////////////////////////////////////////////////////////////////////

GuiArcadeKeybinds::GuiArcadeKeybinds() : GuiSettings(_("ARCADE SIMULATOR KEY SETUP"))
{
    ArcadeKeybindStore::getInstance().load();

    const std::string help {
        _("THESE KEYS ARE WRITTEN TO THE ARCADE EMULATOR CONFIGURATIONS WHEN YOU LEAVE A "
          "KEY PAGE. THIS ONLY AFFECTS THE EMULATORS, NOT THIS FRONTEND. KEYBOARD ONLY, "
          "GAMEPADS KEEP THEIR OWN BINDINGS.")};

    addRow(makeNavRow(_("PLAYER 1 KEYBOARD"), "",
                      [this] { mWindow->pushGui(new GuiArcadeKeybindPage(_("PLAYER 1"), "p1.")); },
                      help));
    addRow(makeNavRow(_("PLAYER 2 KEYBOARD"), "",
                      [this] { mWindow->pushGui(new GuiArcadeKeybindPage(_("PLAYER 2"), "p2.")); },
                      help));
    addRow(makeNavRow(_("COMMON MACHINE KEYS"), "",
                      [this] {
                          mWindow->pushGui(new GuiArcadeKeybindPage(
                              _("COMMON MACHINE KEYS"), "global."));
                      },
                      help));
    addRow(makeNavRow(_("GAME-SPECIFIC KEYS"), "",
                      [this] { mWindow->pushGui(new GuiArcadePerGameSystems()); }, help));
}

ComponentListRow GuiArcadeKeybinds::makeNavRow(const std::string& label, const std::string& value,
                                               const std::function<void()>& func,
                                               const std::string& help)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    if (!value.empty()) {
        row.addElement(std::make_shared<TextComponent>(value, menuValueFont(),
                                                       mMenuColorPrimary, ALIGN_RIGHT),
                       true);
    }
    row.addElement(getMenu().makeArrow(), false);
    // Explanations belong to the menu's TIPS panel, not to the option list itself.
    row.helpText = help;
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}

///////////////////////////////////////////////////////////////////////////////
// GuiArcadeKeybindPage (per group capture page)
///////////////////////////////////////////////////////////////////////////////

namespace {
const std::vector<std::pair<const char*, const char*>> sP1P2Rows {
    {"coin", "COIN"}, {"start", "START"}, {"up", "UP"}, {"down", "DOWN"},
    {"left", "LEFT"}, {"right", "RIGHT"}, {"btn1", "BUTTON 1"}, {"btn2", "BUTTON 2"},
    {"btn3", "BUTTON 3"}, {"btn4", "BUTTON 4"}, {"btn5", "BUTTON 5"},
    {"btn6", "BUTTON 6"}, {"throttle", "THROTTLE"}, {"brake", "BRAKE"},
    {"boost", "BOOST"}};

// Racing cabinets have a wheel and pedals, not a dpad, so the per game page must not offer
// UP/DOWN/LEFT/RIGHT for them (devdoc/43: left/right also drive ANALOGLEFT/ANALOGRIGHT and
// throttle/brake drive ANALOGUP/ANALOGDOWN). Only the labels and the visible rows differ, the
// stored p1./p2. keys are unchanged so untouched rows still inherit the global setup. Boost is
// left out on purpose because Demul has no bucket for it and the writer skips it.
const std::vector<std::pair<const char*, const char*>> sRacingRows {
    {"coin", "COIN"}, {"start", "START"}, {"left", "STEER LEFT"},
    {"right", "STEER RIGHT"}, {"throttle", "THROTTLE"}, {"brake", "BRAKE"},
    {"btn1", "BUTTON 1"}, {"btn2", "BUTTON 2"}, {"btn3", "BUTTON 3"},
    {"btn4", "BUTTON 4"}, {"btn5", "BUTTON 5"}, {"btn6", "BUTTON 6"}};

// ATV Track has steering and pedals only, its cabinet has no action buttons at all, so the
// page must not offer BUTTON 1-6 for it.
const std::vector<std::pair<const char*, const char*>> sPedalRows {
    {"coin", "COIN"},       {"start", "START"},
    {"left", "STEER LEFT"}, {"right", "STEER RIGHT"},
    {"throttle", "THROTTLE"}, {"brake", "BRAKE"}};

// Twin stick cabinet (Planet Harriers): one stick moves, the other aims, so the page shows
// both sticks instead of a single dpad. These rows write the JOY sections (S1/S2) rather than
// the JAMMA axes, see demulPatches().
const std::vector<std::pair<const char*, const char*>> sTwinStickRows {
    {"coin", "COIN"},
    {"start", "START"},
    {"s1up", "LEFT STICK UP"},
    {"s1down", "LEFT STICK DOWN"},
    {"s1left", "LEFT STICK LEFT"},
    {"s1right", "LEFT STICK RIGHT"},
    {"s2up", "RIGHT STICK UP"},
    {"s2down", "RIGHT STICK DOWN"},
    {"s2left", "RIGHT STICK LEFT"},
    {"s2right", "RIGHT STICK RIGHT"},
    {"btn1", "BUTTON 1"}, {"btn2", "BUTTON 2"}, {"btn3", "BUTTON 3"},
    {"btn4", "BUTTON 4"}, {"btn5", "BUTTON 5"}, {"btn6", "BUTTON 6"}};

// Light gun cabinet (Brave Firefighters): aiming is the mouse in Demul and cannot be captured
// here, only the trigger and the hose pump are keyboard bound.
const std::vector<std::pair<const char*, const char*>> sLightGunRows {
    {"coin", "COIN"}, {"start", "START"}, {"btn1", "TRIGGER"}, {"btn2", "HOSE"}};

// Football Power (gaelco3d footbpow): movement is the AD stick (Demul drives its Y axis
// through ANALOGUP/DOWN, i.e. the throttle/brake slots) plus the two documented action buttons.
const std::vector<std::pair<const char*, const char*>> sSportsRows {
    {"coin", "COIN"},   {"start", "START"},   {"throttle", "STICK UP"},
    {"brake", "STICK DOWN"}, {"left", "STICK LEFT"}, {"right", "STICK RIGHT"},
    {"btn1", "DRIBBLING"}, {"btn2", "DEFENSE"}};

// Per game row sets; titles without special controls keep the full unified list.
std::vector<std::pair<const char*, const char*>> perGameRows(const std::string& rom)
{
    if (rom == "atvtrack" || rom == "atvtracka")
        return sPedalRows;
    static const std::set<std::string> racing {
        "atvtrack", "atvtracka", "smashdrv", "airtrix", "airtrixo",
        "swracer", "sgnascar", "sgnascaro"};
    if (racing.find(rom) != racing.end())
        return sRacingRows;
    if (rom == "pharrier")
        return sTwinStickRows;
    if (rom == "braveff")
        return sLightGunRows;
    if (rom == "gfootbal")
        return sSportsRows;
    return {};
}

const std::vector<std::pair<const char*, const char*>> sGlobalRows {
    {"service", "SERVICE"}, {"test", "TEST"}, {"reset", "RESET"}};

constexpr int sHoldToSkipMs {1000};
} // namespace

GuiArcadeKeybindPage::GuiArcadeKeybindPage(const std::string& title, const std::string& prefix,
                                           const std::string& system, const std::string& rom)
    : GuiSettings(title), mSystem(system), mRom(rom),
      mPerGame(!system.empty() && !rom.empty())
{
    ArcadeKeybindStore::getInstance().load();
    if (mPerGame)
        ArcadePerGameStore::getInstance().load();

    const bool global {prefix == "global."};
    // Racing titles show their wheel/pedal rows instead of the generic dpad list, everything
    // that is not overridden is still inherited from the global setup.
    std::vector<std::pair<const char*, const char*>> rows;
    if (global) {
        rows = sGlobalRows;
    }
    else if (mPerGame) {
        rows = perGameRows(mRom);
        if (rows.empty())
            rows = sP1P2Rows;
    }
    else {
        rows = sP1P2Rows;
    }

    // The capture instructions live in the TIPS panel of the menu instead of taking a row
    // of the option list.
    const std::string help {
        mPerGame ? _("ROWS THAT ARE NOT OVERRIDDEN FOLLOW THE GLOBAL KEY SETUP. PRESS ENTER "
                     "TO CAPTURE, HOLD FOR ONE SECOND TO INHERIT THE GLOBAL KEY, PRESS B TO "
                     "FINISH AND SAVE.")
                 : _("PRESS ENTER ON A ROW TO START CAPTURING, THEN PRESS THE PHYSICAL KEY. "
                     "HOLD FOR ONE SECOND TO SKIP THE ROW, PRESS B TO FINISH AND SAVE.")};

    for (const auto& [sub, labelMsgid] : rows) {
        ComponentListRow row;
        auto text {std::make_shared<TextComponent>(_(labelMsgid),
                                                   getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary)};
        row.addElement(text, true);
        row.helpText = help;

        auto mapping {std::make_shared<TextComponent>("", menuValueFont(), mMenuColorPrimary,
                                                      ALIGN_RIGHT)};
        // Both label and value must request width, otherwise the list never assigns
        // a size to the value column and the text is clipped away.
        row.addElement(mapping, true);
        mMappings.push_back(mapping);

        row.inputHandler = [this, index = static_cast<int>(mTargetKeys.size())](InputConfig* config,
                                                                               Input input) -> bool {
            if (config->getDeviceId() != DEVICE_KEYBOARD)
                return false; // Gamepads keep navigating the list.
            if (input.type != TYPE_KEY)
                return false;

            if (!mConfiguringRow) {
                if (config->isMappedTo("a", input) && input.value != 0) {
                    // Enter capture mode starting at this row, continuing down until
                    // the end of the list.
                    getMenu().getList()->stopScrolling();
                    mConfiguringAll = true;
                    mConfiguringRow = true;
                    setPressText(index);
                    return true;
                }
                return false;
            }

            if (input.value != 0) {
                // Key down: if already holding something, ignore, otherwise prepare.
                if (mHoldingInput)
                    return true;
                mHoldingInput = true;
                mHeldInput = input;
                mHeldTime = 0;
                mHeldInputId = index;
                return true;
            }

            // Key up: make sure it is the same key that was pressed.
            if (!mHoldingInput || mHeldInput.device != input.device ||
                mHeldInput.id != input.id || mHeldInput.type != input.type)
                return true;

            mHoldingInput = false;
            captureAssign(mHeldInput, mHeldInputId);
            rowDone();
            return true;
        };

        mTargetKeys.push_back({prefix + std::string(sub), labelMsgid});
        refreshRow(static_cast<int>(mTargetKeys.size() - 1));
        addRow(row);
    }

    // The scroll list starts on the first row to make the capture flow obvious.
    getMenu().setCursorToFirstListEntry();
}

bool GuiArcadeKeybindPage::input(InputConfig* config, Input input)
{
    // During capture the B key cancels the capture for any device. Outside capture it
    // leaves the page, and the mappings captured so far are persisted right before the
    // page is removed; this replaces the old explicit SAVE row.
    if (config->isMappedTo("b", input) && input.value != 0) {
        if (mConfiguringRow) {
            mConfiguringRow = false;
            mConfiguringAll = false;
            mHoldingInput = false;
            for (size_t i {0}; i < mMappings.size(); ++i)
                refreshRow(static_cast<int>(i));
            return true;
        }
        saveAll(false);
        return GuiSettings::input(config, input);
    }
    return GuiSettings::input(config, input);
}

void GuiArcadeKeybindPage::update(int deltaTime)
{
    GuiSettings::update(deltaTime);
    if (!mConfiguringRow || !mHoldingInput)
        return;
    const int prevSec {mHeldTime / 1000};
    mHeldTime += deltaTime;
    const int curSec {mHeldTime / 1000};
    if (mHeldTime >= sHoldToSkipMs) {
        // Long press: in the per game page this removes the override so the row falls
        // back to the global setup, in the global pages it clears the key.
        if (mPerGame)
            ArcadePerGameStore::getInstance().set(mSystem, mRom,
                                                  mTargetKeys.at(mHeldInputId).settingsKey, "");
        else
            ArcadeKeybindStore::getInstance().set(
                mTargetKeys.at(mHeldInputId).settingsKey, "");
        setNotDefined(mHeldInputId);
        if (mPerGame)
            refreshRow(mHeldInputId);
        mHoldingInput = false;
        rowDone();
        return;
    }
    if (prevSec != curSec) {
        std::stringstream ss;
        ss << _("HOLD FOR") << " " << sHoldToSkipMs / 1000 - curSec << " " << _("S TO SKIP");
        mMappings.at(mHeldInputId)->setText(ss.str());
        mMappings.at(mHeldInputId)->setColor(mMenuColorPrimary);
    }
}

void GuiArcadeKeybindPage::captureAssign(const Input& input, int index)
{
    const std::string name {SDL_GetKeyName(static_cast<SDL_Keycode>(input.id))};
    if (mPerGame)
        ArcadePerGameStore::getInstance().set(mSystem, mRom,
                                              mTargetKeys.at(index).settingsKey, name);
    else
        ArcadeKeybindStore::getInstance().set(mTargetKeys.at(index).settingsKey, name);
    mMappings.at(index)->setText(name);
    mMappings.at(index)->setColor(mMenuColorPrimary);
    LOG(LogInfo) << "GuiArcadeKeybinds: captured [" << name << "] for ["
                 << (mPerGame ? mSystem + "/" + mRom + " " : "")
                 << mTargetKeys.at(index).settingsKey << "]";
}

void GuiArcadeKeybindPage::rowDone()
{
    if (mConfiguringAll) {
        const std::shared_ptr<ComponentList> list {getMenu().getList()};
        if (list && list->moveCursor(1)) {
            // The SAVE row index sits right after the capture rows.
            const int cursor {list->getCursorId()};
            if (cursor < static_cast<int>(mTargetKeys.size()))
                setPressText(cursor);
            else
                mConfiguringRow = false;
            return;
        }
        mConfiguringAll = false;
    }
    mConfiguringRow = false;
}

void GuiArcadeKeybindPage::setPressText(int index)
{
    if (index < 0 || index >= static_cast<int>(mMappings.size()))
        return;
    mMappings.at(index)->setText(_("PRESS A KEY"));
    mMappings.at(index)->setColor(mMenuColorPrimary);
}

void GuiArcadeKeybindPage::setNotDefined(int index)
{
    if (index < 0 || index >= static_cast<int>(mMappings.size()))
        return;
    mMappings.at(index)->setText("-" + _("NOT DEFINED") + "-");
    mMappings.at(index)->setColor(mMenuColorSecondary);
}

// Shows the override when there is one, otherwise the inherited global key. Overrides
// are in the primary colour, inherited rows are dimmed and labelled.
void GuiArcadeKeybindPage::refreshRow(int index)
{
    if (index < 0 || index >= static_cast<int>(mMappings.size()))
        return;
    const std::string key {mTargetKeys.at(index).settingsKey};
    if (mPerGame) {
        const std::string override {
            ArcadePerGameStore::getInstance().get(mSystem, mRom, key)};
        if (!override.empty()) {
            mMappings.at(index)->setText(override);
            mMappings.at(index)->setColor(mMenuColorPrimary);
            return;
        }
        const std::string inherited {ArcadeKeybindStore::getInstance().get(key)};
        if (inherited.empty()) {
            setNotDefined(index);
            return;
        }
        mMappings.at(index)->setText(inherited + " (" + _("INHERITED") + ")");
        mMappings.at(index)->setColor(mMenuColorSecondary);
        return;
    }
    const std::string value {ArcadeKeybindStore::getInstance().get(key)};
    if (value.empty()) {
        setNotDefined(index);
        return;
    }
    mMappings.at(index)->setText(value);
    mMappings.at(index)->setColor(mMenuColorPrimary);
}

void GuiArcadeKeybindPage::saveAll(bool report)
{
    if (mPerGame) {
        // Only the override file changes, arcade_keybinds.xml keeps the global setup and
        // the other emulators are left untouched.
        ArcadePerGameStore::getInstance().save();
        applyAllConfigs(mWindow, report, true);
        return;
    }
    ArcadeKeybindStore::getInstance().save();
    applyAllConfigs(mWindow, report);
}

///////////////////////////////////////////////////////////////////////////////
// Per game setup pages (Demul)
///////////////////////////////////////////////////////////////////////////////

GuiArcadePerGameSystems::GuiArcadePerGameSystems()
    : GuiSettings(_("GAME-SPECIFIC KEY SETTINGS"))
{
    ArcadePerGameStore::getInstance().load();

    const std::string help {
        _("EACH GAME GETS ITS OWN DEMUL KEYBOARD PROFILE. ROWS THAT ARE NOT OVERRIDDEN "
          "FOLLOW THE GLOBAL KEY SETUP, AND SAVING THERE UPDATES EVERY PROFILE.")};

    std::vector<SystemData*> systems {demulSystems()};
    if (systems.empty()) {
        for (const char* name : {"gaelco", "hikaru"}) {
            SystemData* system {SystemData::getSystemByName(name)};
            if (system != nullptr)
                systems.push_back(system);
        }
    }
    if (systems.empty()) {
        addRow(makeNoteRow(_("NO GAMES FOUND")));
        return;
    }

    for (SystemData* system : systems) {
        const std::string systemName {system->getName()};
        size_t gameCount {0};
        size_t overriddenCount {0};
        for (FileData* file : system->getRootFolder()->getFilesRecursive(GAME)) {
            if (file->getType() != GAME || Utils::FileSystem::isDirectory(file->getPath()))
                continue;
            ++gameCount;
            if (ArcadePerGameStore::getInstance().overrideCount(systemName, romNameOf(file)) > 0)
                ++overriddenCount;
        }
        const std::string value {
            std::to_string(overriddenCount) + "/" + std::to_string(gameCount)};
        addRow(makeNavRow(Utils::String::toUpper(system->getFullNameLocalized()), value,
                          [this, system] {
                              mWindow->pushGui(new GuiArcadePerGameGames(system));
                          },
                          help));
    }
}

ComponentListRow GuiArcadePerGameSystems::makeNavRow(const std::string& label,
                                                     const std::string& value,
                                                     const std::function<void()>& func,
                                                     const std::string& help)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    row.addElement(std::make_shared<TextComponent>(value, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_RIGHT),
                   true);
    row.addElement(getMenu().makeArrow(), false);
    row.helpText = help;
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}

ComponentListRow GuiArcadePerGameSystems::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0}),
                   true);
    return row;
}

GuiArcadePerGameGames::GuiArcadePerGameGames(SystemData* system)
    : GuiSettings(Utils::String::toUpper(system != nullptr ? system->getFullNameLocalized() : ""))
{
    ArcadePerGameStore::getInstance().load();
    ArcadeKeybindStore::getInstance().load();

    if (system == nullptr) {
        addRow(makeNoteRow(_("NO GAMES FOUND")));
        return;
    }

    const std::string systemName {system->getName()};
    std::vector<FileData*> games;
    for (FileData* file : system->getRootFolder()->getFilesRecursive(GAME)) {
        if (file->getType() != GAME || Utils::FileSystem::isDirectory(file->getPath()))
            continue;
        games.push_back(file);
    }
    if (games.empty()) {
        addRow(makeNoteRow(_("NO GAMES FOUND")));
        return;
    }
    std::sort(games.begin(), games.end(), [](FileData* first, FileData* second) {
        return Utils::String::toLower(first->getName()) <
               Utils::String::toLower(second->getName());
    });

    // Some sets ship several revisions of the same game (for example atvtrack and atvtracka,
    // both shown as "Atv Track"). The rom name is appended in that case so the entry the
    // player edits is unambiguous.
    std::map<std::string, int> nameCounts;
    for (FileData* file : games)
        ++nameCounts[Utils::String::toLower(file->getName())];

    const std::string help {
        _("THE NUMBER ON THE RIGHT IS HOW MANY KEYS ARE OVERRIDDEN FOR THAT GAME, "
          "INHERITED MEANS IT USES THE GLOBAL SETUP.")};

    for (FileData* file : games) {
        const std::string rom {romNameOf(file)};
        const size_t count {ArcadePerGameStore::getInstance().overrideCount(systemName, rom)};
        const std::string value {count > 0 ? std::to_string(count) : _("INHERITED")};
        std::string title {Utils::String::toUpper(file->getName())};
        if (nameCounts[Utils::String::toLower(file->getName())] > 1)
            title += " (" + Utils::String::toUpper(rom) + ")";
        std::shared_ptr<TextComponent> valueText;
        addRow(makeNavRow(
            title, value,
            [this, title, systemName, rom] {
                mRefreshPending = true;
                mWindow->pushGui(new GuiArcadePerGamePlayers(title, systemName, rom));
            },
            &valueText, help));
        mValues.emplace_back(rom, valueText);
    }

    mSystemName = systemName;
}

bool GuiArcadePerGameGames::input(InputConfig* config, Input input)
{
    // A child page persists its changes when it closes, so the override counters are
    // refreshed the first time this page takes input again.
    if (mRefreshPending) {
        mRefreshPending = false;
        for (const auto& entry : mValues) {
            const size_t count {
                ArcadePerGameStore::getInstance().overrideCount(mSystemName, entry.first)};
            if (entry.second)
                entry.second->setText(count > 0 ? std::to_string(count) : _("INHERITED"));
        }
    }
    return GuiSettings::input(config, input);
}

ComponentListRow GuiArcadePerGameGames::makeNavRow(const std::string& label,
                                                   const std::string& value,
                                                   const std::function<void()>& func,
                                                   std::shared_ptr<TextComponent>* valueText,
                                                   const std::string& help)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    auto text {std::make_shared<TextComponent>(value, menuValueFont(), mMenuColorSecondary,
                                              ALIGN_RIGHT)};
    row.addElement(text, true);
    if (valueText != nullptr)
        *valueText = text;
    row.addElement(getMenu().makeArrow(), false);
    row.helpText = help;
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}

ComponentListRow GuiArcadePerGameGames::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0}),
                   true);
    return row;
}

GuiArcadePerGamePlayers::GuiArcadePerGamePlayers(const std::string& title,
                                                 const std::string& system,
                                                 const std::string& rom)
    : GuiSettings(title)
{
    const std::string help {_("ROWS THAT ARE NOT OVERRIDDEN FOLLOW THE GLOBAL KEY SETUP.")};
    addRow(makeNavRow(
        _("PLAYER 1 KEYBOARD"), "",
        [this, title, system, rom] {
            mWindow->pushGui(new GuiArcadeKeybindPage(
                title + " " + _("PLAYER 1"), "p1.", system, rom));
        },
        help));
    addRow(makeNavRow(
        _("PLAYER 2 KEYBOARD"), "",
        [this, title, system, rom] {
            mWindow->pushGui(new GuiArcadeKeybindPage(
                title + " " + _("PLAYER 2"), "p2.", system, rom));
        },
        help));
}

ComponentListRow GuiArcadePerGamePlayers::makeNavRow(const std::string& label,
                                                     const std::string& value,
                                                     const std::function<void()>& func,
                                                     const std::string& help)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    row.addElement(std::make_shared<TextComponent>(value, menuValueFont(), mMenuColorPrimary,
                                                   ALIGN_RIGHT),
                   true);
    row.addElement(getMenu().makeArrow(), false);
    row.helpText = help;
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}
