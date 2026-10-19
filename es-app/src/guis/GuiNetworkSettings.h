//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiNetworkSettings.h
//
//  Exia specific network settings page, reachable from the main menu as NETWORK SETTINGS.
//
//  The page is backed by NetworkManager through its nmcli command line client. Everything is
//  executed through execvp with an argument vector and never through a shell, so SSIDs and
//  passphrases do not need any escaping. nmcli translates its output, so all commands run with
//  LC_ALL=C and the output parsers rely on that.
//
//  Contents:
//    CURRENT STATUS   read only information, a submenu holding the connection name, the IPv4
//                     address, the gateway, the overseas and domestic reachability and the
//                     state of the wired interfaces
//    WI-FI            radio switch, available networks and saved networks
//
//  This is an Exia (EndeavourOS game station) local addition and it is Linux only, the main
//  menu entry is therefore not added on the other platforms.
//

#ifndef ES_APP_GUIS_GUI_NETWORK_SETTINGS_H
#define ES_APP_GUIS_GUI_NETWORK_SETTINGS_H

#include "guis/GuiSettings.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// One Wi-Fi network as reported by the NetworkManager scan. Access points that share an SSID
// (2.4 GHz plus 5 GHz for example) are merged by the scan, only the strongest one is kept.
struct NetworkWifiEntry {
    std::string ssid;
    std::string security;
    int signal {0};
    bool inUse {false};
    bool saved {false};
};

class GuiNetworkSettings : public GuiSettings
{
public:
    explicit GuiNetworkSettings(const std::string& title);

private:
    // Row builders shared by all of the network pages.
    static ComponentListRow makeSectionRow(const std::string& label);
    static ComponentListRow makeNoteRow(const std::string& text);
    static ComponentListRow makeInfoRow(const std::string& label, const std::string& value);
    static ComponentListRow makeNavRow(GuiSettings* page,
                                       const std::string& label,
                                       const std::string& value,
                                       const std::function<void()>& func);

    // Page builders, used both for the initial open and to refresh a page after an action has
    // changed the NetworkManager state.
    static GuiSettings* buildCurrentStatusPage();
    static GuiSettings* buildNetworksPage(Window* window,
                                          const std::vector<NetworkWifiEntry>& networks);
    static GuiSettings* buildSavedPage(Window* window);
    static GuiSettings* buildSavedConnectionPage(Window* window,
                                                 GuiSettings* savedPage,
                                                 const std::string& name);

    // Actions shared by the pages.
    static void rescanNetworks(Window* window, GuiSettings* page);
    static void selectNetwork(Window* window,
                              GuiSettings* page,
                              const std::shared_ptr<std::vector<NetworkWifiEntry>>& networks,
                              const std::size_t index);
    static void activateNetwork(Window* window,
                                GuiSettings* page,
                                const std::shared_ptr<std::vector<NetworkWifiEntry>>& networks,
                                const std::size_t index,
                                const std::string& password);
    static void inputSsidManually(Window* window);
    static void showTextInput(Window* window,
                              const std::string& title,
                              const std::string& initial,
                              const std::function<void(const std::string&)>& callback);

    // The read only rows all belong to the CURRENT STATUS page, they are filled from there.
    static void addInformationRows(GuiSettings* page);
    static void addInternetStatusRows(GuiSettings* page);
    static void addEthernetRows(GuiSettings* page);

    void addWifiRows();

    void openAvailableNetworks();
    void openSavedNetworks();
};

#endif // ES_APP_GUIS_GUI_NETWORK_SETTINGS_H
