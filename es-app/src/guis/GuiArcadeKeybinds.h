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
#include <vector>

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

// Main entry page: explanation plus the three groups and an immediate write-all.
class GuiArcadeKeybinds : public GuiSettings
{
public:
    GuiArcadeKeybinds();

private:
    ComponentListRow makeNavRow(const std::string& label, const std::string& value,
                                const std::function<void()>& func);
    ComponentListRow makeNoteRow(const std::string& text);

    void saveAll();
};

// Per group capture page. Rows are captured with the GuiInputConfig press/release
// pattern, one key at a time, advancing down the list automatically.
class GuiArcadeKeybindPage : public GuiSettings
{
public:
    GuiArcadeKeybindPage(const std::string& title, const std::string& prefix);

    void update(int deltaTime) override;
    bool input(InputConfig* config, Input input) override;

private:
    // Applies the captured key to the row, stores it and advances with the caller.
    void captureAssign(const Input& input, int index);
    void rowDone();
    void setPressText(int index);
    void setNotDefined(int index);
    void saveAll();
    ComponentListRow makeNoteRow(const std::string& text);

    struct KeyRow {
        std::string settingsKey;
        const char* labelMsgid;
    };

    std::shared_ptr<ComponentList> mList;
    std::vector<std::shared_ptr<TextComponent>> mMappings;
    std::vector<KeyRow> mTargetKeys;

    bool mConfiguringAll {false};
    bool mConfiguringRow {false};
    bool mHoldingInput {false};
    Input mHeldInput;
    int mHeldInputId {0};
    int mHeldTime {0};
};

#endif // ES_APP_GUIS_GUI_ARCADE_KEYBINDS_H
