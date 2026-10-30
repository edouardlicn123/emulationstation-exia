//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiArcadeKeybinds.h
//
//  Arcade simulator key setup, reachable from OPTIONS -> INPUT DEVICE SETTINGS as
//  ARCADE SIMULATOR KEY SETUP (devdoc/43).
//
//  The page lets the keyboard keys used by the arcade emulators be captured row by row
//  in the Batocera style, and SAVE writes all of them into the installed simulator
//  configurations (Demul, Dolphin-Triforce, Flycast, Play! and the Lindbergh loader).
//  The bindings live in ~/.config/ES-DE/arcade_keybinds.xml (ArcadeKeybindStore) so
//  that es_settings.xml rewriting is not involved. The values are stored as SDL key
//  names ("5", "Up", "Left Ctrl", "F2") which the writer converts per emulator.
//
//  Only keyboard input is captured, gamepads keep their normal navigation behaviour
//  inside these pages. The frontend's own input configuration is never touched.
//
//  This is an Exia (EndeavourOS game station) local addition.
//

#ifndef ES_APP_GUIS_GUI_ARCADE_KEYBINDS_H
#define ES_APP_GUIS_GUI_ARCADE_KEYBINDS_H

#include "components/ComponentList.h"
#include "guis/GuiSettings.h"
#include "InputConfig.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class SystemData;
class TextComponent;

// Persistent bindings store for the 33 setup keys (P1/P2: coin, start, dpad, buttons
// 1-6, throttle, brake, boost; global: service, test, reset).
class ArcadeKeybindStore
{
public:
    static ArcadeKeybindStore& getInstance();

    static std::string getPath();
    // Fills the map with the MAME aligned defaults and then overlays the saved file.
    void load();
    // Writes the current bindings to arcade_keybinds.xml. Returns false on error.
    bool save();

    std::string get(const std::string& key) const
    {
        const auto entry {mBindings.find(key)};
        return (entry != mBindings.end()) ? entry->second : "";
    }
    void set(const std::string& key, const std::string& value) { mBindings[key] = value; }

private:
    std::map<std::string, std::string> mBindings;
};

// Per game overrides on top of ArcadeKeybindStore (devdoc/43 extension). Only the keys
// that differ from the global setup are stored, everything else is inherited, so the
// main setup page keeps driving the defaults for every Demul game.
class ArcadePerGameStore
{
public:
    using Overrides = std::map<std::string, std::string>;

    static ArcadePerGameStore& getInstance();

    static std::string getPath();
    // <appdata>/arcade_keybinds/demul, one complete padDemul.ini copy per game below it.
    static std::string getSnapshotRoot();

    void load();
    bool save();

    // Empty when the game inherits the global binding.
    std::string get(const std::string& system, const std::string& rom,
                    const std::string& key) const;
    // An empty value removes the override and restores the inherited binding.
    void set(const std::string& system, const std::string& rom, const std::string& key,
             const std::string& value);

    size_t overrideCount(const std::string& system, const std::string& rom) const;
    const Overrides& overridesFor(const std::string& system, const std::string& rom) const;
    std::vector<std::pair<std::string, std::string>> gameList() const;

private:
    std::map<std::pair<std::string, std::string>, Overrides> mGames;
};

// Main entry page: explanation plus the navigation to the key groups. Each key page
// persists its changes when it is closed.
class GuiArcadeKeybinds : public GuiSettings
{
public:
    GuiArcadeKeybinds();

private:
    ComponentListRow makeNavRow(const std::string& label, const std::string& value,
                                const std::function<void()>& func,
                                const std::string& help = "");
};

// Per group capture page. Rows are captured with the GuiInputConfig press/release
// pattern, one key at a time, advancing down the list automatically.
class GuiArcadeKeybindPage : public GuiSettings
{
public:
    // system/rom empty means the global setup, otherwise the page edits the overrides
    // of that one Demul game and shows the inherited values for the untouched rows.
    GuiArcadeKeybindPage(const std::string& title, const std::string& prefix,
                         const std::string& system = "", const std::string& rom = "");

    void update(int deltaTime) override;
    bool input(InputConfig* config, Input input) override;

private:
    // Applies the captured key to the row, stores it and advances with the caller.
    void captureAssign(const Input& input, int index);
    void rowDone();
    void setPressText(int index);
    void setNotDefined(int index);
    void refreshRow(int index);
    void saveAll(bool report = true);

    struct KeyRow {
        std::string settingsKey;
        const char* labelMsgid;
    };

    std::shared_ptr<ComponentList> mList;
    std::vector<std::shared_ptr<TextComponent>> mMappings;
    std::vector<KeyRow> mTargetKeys;

    std::string mSystem;
    std::string mRom;
    bool mPerGame {false};

    bool mConfiguringAll {false};
    bool mConfiguringRow {false};
    bool mHoldingInput {false};
    Input mHeldInput;
    int mHeldInputId {0};
    int mHeldTime {0};
};

// Platform picker for the per game setup: lists every system launched through Demul.
class GuiArcadePerGameSystems : public GuiSettings
{
public:
    GuiArcadePerGameSystems();

private:
    ComponentListRow makeNavRow(const std::string& label, const std::string& value,
                                const std::function<void()>& func,
                                const std::string& help = "");
    ComponentListRow makeNoteRow(const std::string& text);
};

// Game picker inside one Demul platform, showing how many keys are overridden.
class GuiArcadePerGameGames : public GuiSettings
{
public:
    explicit GuiArcadePerGameGames(SystemData* system);

    bool input(InputConfig* config, Input input) override;

private:
    ComponentListRow makeNavRow(const std::string& label, const std::string& value,
                                const std::function<void()>& func,
                                std::shared_ptr<TextComponent>* valueText = nullptr,
                                const std::string& help = "");
    ComponentListRow makeNoteRow(const std::string& text);

    std::string mSystemName;
    std::vector<std::pair<std::string, std::shared_ptr<TextComponent>>> mValues;
    bool mRefreshPending {false};
};

// Player picker for one game, mirroring the P1/P2 split of the global setup.
class GuiArcadePerGamePlayers : public GuiSettings
{
public:
    GuiArcadePerGamePlayers(const std::string& title, const std::string& system,
                            const std::string& rom);

private:
    ComponentListRow makeNavRow(const std::string& label, const std::string& value,
                                const std::function<void()>& func,
                                const std::string& help = "");
};

#endif // ES_APP_GUIS_GUI_ARCADE_KEYBINDS_H
