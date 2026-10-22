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
//    Flycast    flatpak mappings/SDL_Keyboard_arcade.cfg  (SDL scancodes)
//    Play!      flatpak inputprofiles/default.xml         (Qt key codes)
//    Lindbergh  ~/roms/emus/lindbergh/<game>/controls.ini (evdev names)
//
//  The values are stored in ES-DE space as SDL key names and converted per emulator
//  during the write. Writes are idempotent: files whose content does not change are
//  left untouched, changed files get a .bak-<timestamp> backup first.
//
//  This is an Exia (EndeavourOS game station) local addition.
//

#include "guis/GuiArcadeKeybinds.h"

#include "Log.h"
#include "MenuFontUtil.h"
#include "Window.h"
#include "guis/GuiMsgBox.h"
#include "utils/FileSystemUtil.h"

#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

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
                 {"p2.coin", "6"},             {"p2.start", "2"},            {"p2.up", ""},
                 {"p2.down", ""},              {"p2.left", ""},              {"p2.right", ""},
                 {"p2.btn1", ""},              {"p2.btn2", ""},              {"p2.btn3", ""},
                 {"p2.btn4", ""},              {"p2.btn5", ""},              {"p2.btn6", ""},
                 {"p2.throttle", ""},          {"p2.brake", ""},             {"p2.boost", ""},
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
// Key code conversion (one SDL scancode to every emulator notation)
///////////////////////////////////////////////////////////////////////////////

namespace {
struct KeyEntry {
    int scan_;         // SDL scancode
    int evdev_;        // Linux key code used by Demul
    const char* name_; // evdev mnemonic, Lindbergh uses "KEY_" + name_
    const char* dolphin_;
    int qtKey_;        // Qt::Key value used by Play!
};

// Letters are SDL_SCANCODE_A=4..Z=29 against evdev KEY_A=30..KEY_Z=50, digits are
// SDL_SCANCODE_1=30..0=39 against evdev KEY_1=2..KEY_0=11. Both directions verified
// against the current padDemul.ini contents.
const std::vector<KeyEntry> sKeyEntries {
    {4, 30, "A", "A", 65},     {5, 31, "B", "B", 66},     {6, 32, "C", "C", 67},
    {7, 33, "D", "D", 68},     {8, 34, "E", "E", 69},     {9, 35, "F", "F", 70},
    {10, 36, "G", "G", 71},    {11, 37, "H", "H", 72},    {12, 38, "I", "I", 73},
    {13, 39, "J", "J", 74},    {14, 40, "K", "K", 75},    {15, 41, "L", "L", 76},
    {16, 42, "M", "M", 77},    {17, 43, "N", "N", 78},    {18, 44, "O", "O", 79},
    {19, 45, "P", "P", 80},    {20, 46, "Q", "Q", 81},    {21, 47, "R", "R", 82},
    {22, 48, "S", "S", 83},    {23, 49, "T", "T", 84},    {24, 50, "U", "U", 85},
    {25, 51, "V", "V", 86},    {26, 52, "W", "W", 87},    {27, 53, "X", "X", 88},
    {28, 54, "Y", "Y", 89},    {29, 55, "Z", "Z", 90},
    {30, 2, "1", "1", 49},     {31, 3, "2", "2", 50},     {32, 4, "3", "3", 51},
    {33, 5, "4", "4", 52},     {34, 6, "5", "5", 53},     {35, 7, "6", "6", 54},
    {36, 8, "7", "7", 55},     {37, 9, "8", "8", 56},     {38, 10, "9", "9", 57},
    {39, 11, "0", "0", 48},
    {41, 1, "ESCAPE", "Escape", 16777216},
    {42, 14, "BACKSPACE", "BackSpace", 16777219},
    {43, 15, "TAB", "Tab", 9},
    {44, 57, "SPACE", "space", 32},
    {45, 12, "MINUS", "minus", 45},
    {46, 13, "EQUAL", "equal", 61},
    {47, 26, "LEFTBRACKET", "bracketleft", 91},
    {48, 27, "RIGHTBRACKET", "bracketright", 93},
    {49, 43, "BACKSLASH", "backslash", 92},
    {51, 39, "SEMICOLON", "semicolon", 59},
    {52, 40, "APOSTROPHE", "apostrophe", 39},
    {54, 51, "COMMA", "comma", 44},
    {55, 52, "PERIOD", "period", 46},
    {56, 53, "SLASH", "slash", 47},
    {58, 59, "F1", "F1", 16777264},
    {59, 60, "F2", "F2", 16777265},
    {60, 61, "F3", "F3", 16777266},
    {61, 62, "F4", "F4", 16777267},
    {62, 63, "F5", "F5", 16777268},
    {63, 64, "F6", "F6", 16777269},
    {64, 65, "F7", "F7", 16777270},
    {65, 66, "F8", "F8", 16777271},
    {66, 67, "F9", "F9", 16777272},
    {67, 68, "F10", "F10", 16777273},
    {68, 87, "F11", "F11", 16777274},
    {69, 88, "F12", "F12", 16777275},
    {74, 102, "HOME", "Home", 16777232},
    {75, 104, "PAGEUP", "Page_Up", 16777238},
    {76, 111, "DELETE", "Delete", 16777223},
    {77, 107, "END", "End", 16777233},
    {78, 109, "PAGEDOWN", "Page_Down", 16777239},
    {79, 106, "RIGHT", "Right", 16777236},
    {80, 105, "LEFT", "Left", 16777234},
    {81, 108, "DOWN", "Down", 16777237},
    {82, 103, "UP", "Up", 16777235},
    {40, 28, "RETURN", "Return", 16777220},
    {224, 29, "LEFTCTRL", "Ctrl", 16777249},
    {225, 42, "LEFTSHIFT", "Shift", 16777248},
    {226, 56, "LEFTALT", "Alt", 16777251},
    {228, 97, "RIGHTCTRL", "Ctrl", 16777249},
    {229, 54, "RIGHTSHIFT", "Shift", 16777248},
    {230, 100, "RIGHTALT", "Alt", 16777251}};

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
    const SDL_Keycode keycode {SDL_GetKeyFromName(keyName.c_str())};
    if (keycode == SDLK_UNKNOWN)
        return -1;
    return static_cast<int>(SDL_GetScancodeFromKey(keycode));
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
};

std::string backupSuffix()
{
    const std::time_t now {std::time(nullptr)};
    const std::tm* local {std::localtime(&now)};
    std::stringstream out;
    out << std::put_time(local, "%Y%m%d-%H%M%S");
    return out.str();
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
    const bool moved {Utils::FileSystem::renameFile(staging, path, true)};
    if (!moved)
        LOG(LogError) << "GuiArcadeKeybinds: rename failed for " << path;
    return moved;
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

        if (replaced) {
            result.touched = true;
            continue;
        }

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
        result.touched = true;
    }

    std::string newContent {joinLines(lines, ending, trailingEnding)};
    if (newContent == content) {
        LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
        return true;
    }
    return backupAndWrite(path, newContent);
}

// Play! profile patcher: replaces the values of the integer preferences of an action
// block like input.pad1.cross.bindingtarget1.keyId. Missing lines count as skipped.
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

    bool changed {false};
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

    changed = true; // Values were replaced unconditionally, treat as a content change
                    // candidate; the equality check below decides the final outcome.
    (void)changed;
    const std::string newContent {joinLines(lines, ending, trailingEnding)};
    if (newContent == content) {
        LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
        return true;
    }
    result.touched = true;
    return backupAndWrite(path, newContent);
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
    // The game keys from the unified bindings. Keys that cannot be resolved land in
    // the skipped count and keep their default behaviour in Flycast.
    for (const auto& [option, keyName] : binds) {
        const int scan {scancodeOf(keyName)};
        if (scan < 0) {
            result.skippedCount++;
            LOG(LogInfo) << "GuiArcadeKeybinds: Flycast " << option << ": unknown key " << keyName
                         << ", skipped";
            continue;
        }
        out << "bind" << bindIndex++ << " = " << scan << ":" << option << "\n";
    }
    // The retained emulation hotkeys match the built-in keyboard defaults (Space =
    // fast forward, Tab = menu) so a fresh mapping file does not lose them.
    if (binds.find("btn_fforward") == binds.end())
        out << "bind" << bindIndex++ << " = 44:btn_fforward\n";
    if (binds.find("btn_menu") == binds.end())
        out << "bind" << bindIndex++ << " = 43:btn_menu\n";

    std::string content;
    if (readFileBytes(path, content)) {
        const std::string newContent {out.str()};
        if (newContent == content) {
            LOG(LogInfo) << "GuiArcadeKeybinds: " << path << " unchanged";
            return true;
        }
    }
    const bool written {backupAndWrite(path, out.str())};
    if (written)
        result.touched = true;
    return written;
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
EmuResult writeDemul(const ArcadeKeybindStore& store)
{
    const std::string path {Utils::FileSystem::getSystemHomeDirectory() +
                            "/Applications/demul/padDemul.ini"};
    EmuResult result;

    auto evdevOf {[&store, &result](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr) {
            result.skippedCount++;
            LOG(LogInfo) << "GuiArcadeKeybinds: Demul " << prefix << unifiedKey
                         << ": unsupported key " << value;
            return std::string("");
        }
        return std::to_string(entry->evdev_);
    }};

    const std::vector<std::pair<const char*, const char*>> jamm {
        {"JAMMA0_0", "p1."}, {"JAMMA0_1", "p2."}};
    std::vector<KeyPatch> patches;
    static const std::vector<std::pair<const char*, const char*>> digitals {
        {"DIGITALUP", "up"}, {"DIGITALDOWN", "down"}, {"DIGITALLEFT", "left"},
        {"DIGITALRIGHT", "right"}};
    for (const auto& [section, prefix] : jamm) {
        for (const auto& [demulKey, unifiedKey] : digitals) {
            const std::string value {evdevOf(prefix, unifiedKey)};
            if (!value.empty())
                patches.push_back({section, demulKey, value});
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
        // Boost and reset have no Demul bucket, they are skipped on purpose.
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

    if (patches.empty())
        return result;
    patchIniFile(path, patches, false, result);
    return result;
}

// Dolphin: GCPadNew.ini uses X11 keysym names inside [GCPad1]/[GCPad2].
EmuResult writeDolphin(const ArcadeKeybindStore& store)
{
    const std::string path {
        Utils::FileSystem::getSystemHomeDirectory() +
        "/.var/app/org.DolphinEmu.dolphin-emu/config/dolphin-emu/GCPadNew.ini"};
    EmuResult result;

    auto dolphinOf {[&store](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr)
            return std::string("");
        return std::string(entry->dolphin_);
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
    if (applied == 0)
        return result;
    patchIniFile(path, patches, true, result);
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

    auto qtOf {[&store](const std::string& prefix, const char* unifiedKey) {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return -1;
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr)
            return -1;
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
        patchPlayProfile(path, pad, actions, result);
    }
    if (applied == 0)
        result.missing = true;
    return result;
}

// Lindbergh: every game folder gets its own controls.ini. The loader loads it from
// the game directory, files not present yet stay untouched.
EmuResult writeLindbergh(const ArcadeKeybindStore& store)
{
    const std::string base {
        Utils::FileSystem::getSystemHomeDirectory() + "/roms/emus/lindbergh"};
    EmuResult result;

    // Top level game ids: lines without indentation ending in a colon.
    std::vector<std::string> gameIds;
    std::string ymlContent;
    if (readFileBytes(base + "/games_config.yml", ymlContent)) {
        std::istringstream stream {ymlContent};
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty() || line.front() == '#' || line.front() == ' ' ||
                line.front() == '\t')
                continue;
            const size_t colon {line.find(':')};
            if (colon == std::string::npos)
                continue;
            const std::string id {line.substr(0, colon)};
            if (!id.empty() && id.find(' ') == std::string::npos)
                gameIds.push_back(id);
        }
    }
    else {
        result.missing = true;
    }

    int appliedFiles {0};
    for (const std::string& gameId : gameIds) {
        const std::string path {base + "/" + gameId + "/controls.ini"};
        if (!Utils::FileSystem::exists(path)) {
            result.skippedCount++;
            continue;
        }
        std::vector<KeyPatch> patches;
        auto evdevName {[&store](const std::string& prefix,
                             const char* unifiedKey) -> std::string {
        const std::string value {store.get(prefix + unifiedKey)};
        if (value.empty())
            return std::string("");
        const int scan {scancodeOf(value)};
        const KeyEntry* entry {(scan >= 0) ? findEntry(scan) : nullptr};
        if (entry == nullptr)
            return std::string("");
        return std::string(entry->name_);
    }};
        if (const std::string value {evdevName("p1.", "coin")}; !value.empty())
            patches.push_back({"Common", "P1_Coin", value, true});
        if (const std::string value {evdevName("p2.", "coin")}; !value.empty())
            patches.push_back({"Common", "P2_Coin", value, true});
        if (const std::string value {evdevName("p1.", "start")}; !value.empty())
            patches.push_back({"Common", "P1_Start", value, true});
        if (const std::string value {evdevName("p2.", "start")}; !value.empty())
            patches.push_back({"Common", "P2_Start", value, true});
        if (const std::string value {evdevName("global.", "service")}; !value.empty())
            patches.push_back({"Common", "P1_Service", value, true});
        if (const std::string value {evdevName("global.", "test")}; !value.empty())
            patches.push_back({"Common", "Test", value, true});

        const std::vector<std::pair<const char*, const char*>> digital {
            {"P1_Up", "up"}, {"P1_Down", "down"}, {"P1_Left", "left"}, {"P1_Right", "right"},
            {"P1_Button1", "btn1"}, {"P1_Button2", "btn2"}, {"P1_Button3", "btn3"}};
        for (const auto& [key, unifiedKey] : digital) {
            const std::string value {evdevName("p1.", unifiedKey)};
            if (!value.empty())
                patches.push_back({"Digital", key, value, true});
        }
        const std::vector<std::pair<const char*, const char*>> driving {
            {"P1_Gas_Digital", "throttle"}, {"P1_Brake_Digital", "brake"},
            {"P1_Steer_Left", "left"}, {"P1_Steer_Right", "right"}};
        for (const auto& [key, unifiedKey] : driving) {
            const std::string value {evdevName("p1.", unifiedKey)};
            if (!value.empty())
                patches.push_back({"Driving", key, value, true});
        }
        if (const std::string value {evdevName("p1.", "boost")}; !value.empty())
            patches.push_back({"Driving", "Boost", value, true});
        // P2 directional and button entries are not part of the loader template.

        EmuResult fileResult;
        if (patches.empty())
            continue;
        if (patchIniFile(path, patches, true, fileResult))
            ++appliedFiles;
        result.touched |= fileResult.touched;
        result.skippedCount += fileResult.skippedCount;
    }
    if (appliedFiles == 0 && gameIds.size() >= 1) {
        // Nothing changed; keep the summary honest without counting it as a failure.
    }
    return result;
}
} // namespace

///////////////////////////////////////////////////////////////////////////////
// Save orchestration
///////////////////////////////////////////////////////////////////////////////

namespace {
void applyAllConfigs(Window* window, bool report)
{
    const ArcadeKeybindStore& store {ArcadeKeybindStore::getInstance()};

    const EmuResult demul {writeDemul(store)};
    const EmuResult dolphin {writeDolphin(store)};
    const EmuResult play {writePlay(store)};

    // Flycast: the standalone SDL keyboard hangs off port 0 only, so P1 is written
    // while P2 keeps its defaults (devdoc/43 section 2.6).
    EmuResult flycast;
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
            const std::string path {
                Utils::FileSystem::getSystemHomeDirectory() +
                "/.var/app/org.flycast.Flycast/config/flycast/mappings/"
                "SDL_Keyboard_arcade.cfg"};
            writeFlycastMapping(path, binds, flycast);
        }
    }

    const EmuResult lindbergh {writeLindbergh(store)};

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
        return row.str();
    }};

    std::stringstream message;
    message << line("Demul", demul) << "\n" << line("Dolphin", dolphin) << "\n"
            << line("Flycast", flycast) << " (" << _("PLAYER 1 ONLY") << ")"
            << "\n"
            << line("Play!", play) << "\n"
            << line("Lindbergh", lindbergh) << "\n"
            << "\n"
            << _("SOME ENTRIES ARE SKIPPED WHEN THE EMULATOR HAS NO MATCHING OPTION; "
                 "DETAILS ARE IN THE LOG");

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

    addRow(makeNoteRow(
        _("THESE KEYS ARE WRITTEN TO THE ARCADE EMULATOR CONFIGURATIONS ON SAVE. THIS "
          "ONLY AFFECTS THE EMULATORS, NOT THIS FRONTEND. KEYBOARD ONLY, GAMEPADS KEEP "
          "THEIR OWN BINDINGS.")));

    addRow(makeNavRow(_("PLAYER 1 KEYBOARD"), "",
                      [this] { mWindow->pushGui(new GuiArcadeKeybindPage(_("PLAYER 1"), "p1.")); }));
    addRow(makeNavRow(_("PLAYER 2 KEYBOARD"), "",
                      [this] { mWindow->pushGui(new GuiArcadeKeybindPage(_("PLAYER 2"), "p2.")); }));
    addRow(makeNavRow(_("COMMON MACHINE KEYS"), "",
                      [this] {
                          mWindow->pushGui(new GuiArcadeKeybindPage(
                              _("COMMON MACHINE KEYS"), "global."));
                      }));

    ComponentListRow saveRow;
    saveRow.addElement(std::make_shared<TextComponent>(_("SAVE AND WRITE TO THE EMULATORS"),
                                                       getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                       mMenuColorPrimary, ALIGN_CENTER),
                       true);
    saveRow.makeAcceptInputHandler([this] { saveAll(); });
    addRow(saveRow);
}

ComponentListRow GuiArcadeKeybinds::makeNavRow(const std::string& label, const std::string& value,
                                               const std::function<void()>& func)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    if (!value.empty()) {
        row.addElement(std::make_shared<TextComponent>(value, menuValueFont(),
                                                       mMenuColorPrimary, ALIGN_RIGHT),
                       false);
    }
    row.addElement(getMenu().makeArrow(), false);
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}

ComponentListRow GuiArcadeKeybinds::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0}),
                   true);
    return row;
}

void GuiArcadeKeybinds::saveAll()
{
    ArcadeKeybindStore::getInstance().save();
    applyAllConfigs(mWindow, true);
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

const std::vector<std::pair<const char*, const char*>> sGlobalRows {
    {"service", "SERVICE"}, {"test", "TEST"}, {"reset", "RESET"}};

constexpr int sHoldToSkipMs {1000};
} // namespace

GuiArcadeKeybindPage::GuiArcadeKeybindPage(const std::string& title, const std::string& prefix)
    : GuiSettings(title)
{
    ArcadeKeybindStore::getInstance().load();

    const bool global {prefix == "global."};
    const auto& rows {global ? sGlobalRows : sP1P2Rows};
    const ArcadeKeybindStore& store {ArcadeKeybindStore::getInstance()};

    for (const auto& [sub, labelMsgid] : rows) {
        ComponentListRow row;
        auto text {std::make_shared<TextComponent>(_(labelMsgid),
                                                   getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary)};
        row.addElement(text, true);

        auto mapping {std::make_shared<TextComponent>("", menuValueFont(), mMenuColorPrimary,
                                                      ALIGN_RIGHT)};
        row.addElement(mapping, false);
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
        if (const std::string value {store.get(prefix + sub)}; !value.empty())
            mapping->setText(value);
        else
            setNotDefined(static_cast<int>(mTargetKeys.size() - 1));
        addRow(row);
    }

    ComponentListRow saveRow;
    saveRow.addElement(
        std::make_shared<TextComponent>(_("SAVE AND WRITE TO THE EMULATORS"),
                                        getMenuThemeFont(FONT_SIZE_MEDIUM), mMenuColorPrimary,
                                        ALIGN_CENTER),
        true);
    saveRow.makeAcceptInputHandler([this] { saveAll(); });
    addRow(saveRow);

    addRow(makeNoteRow(
        _("PRESS ENTER ON A ROW TO START CAPTURING, THEN PRESS THE PHYSICAL KEY. HOLD FOR "
          "ONE SECOND TO SKIP THE ROW, PRESS B TO STOP.")));

    // The scroll list starts on the first row to make the capture flow obvious.
    getMenu().setCursorToFirstListEntry();
}

bool GuiArcadeKeybindPage::input(InputConfig* config, Input input)
{
    // During capture the B key cancels the capture for any device, otherwise it
    // reaches GuiSettings and closes the page as usual.
    if (mConfiguringRow && config->isMappedTo("b", input) && input.value != 0) {
        mConfiguringRow = false;
        mConfiguringAll = false;
        mHoldingInput = false;
        for (size_t i {0}; i < mMappings.size(); ++i) {
            const std::string value {ArcadeKeybindStore::getInstance().get(
                mTargetKeys.at(i).settingsKey)};
            if (value.empty())
                setNotDefined(static_cast<int>(i));
            else
                mMappings.at(i)->setText(value);
        }
        return true;
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
        // Long press: clear the row and move on without capturing.
        ArcadeKeybindStore::getInstance().set(mTargetKeys.at(mHeldInputId).settingsKey, "");
        mMappings.at(mHeldInputId)->setText("-" + _("NOT DEFINED") + "-");
        mMappings.at(mHeldInputId)->setColor(mMenuColorSecondary);
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
    ArcadeKeybindStore& store {ArcadeKeybindStore::getInstance()};
    const std::string name {SDL_GetKeyName(static_cast<SDL_Keycode>(input.id))};
    store.set(mTargetKeys.at(index).settingsKey, name);
    mMappings.at(index)->setText(name);
    mMappings.at(index)->setColor(mMenuColorPrimary);
    LOG(LogInfo) << "GuiArcadeKeybinds: captured [" << name << "] for ["
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

ComponentListRow GuiArcadeKeybindPage::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0}),
                   true);
    return row;
}

void GuiArcadeKeybindPage::saveAll()
{
    ArcadeKeybindStore::getInstance().save();
    applyAllConfigs(mWindow, true);
}
