//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiNetworkSettings.cpp
//
//  Exia specific network settings page, reachable from the main menu as NETWORK SETTINGS.
//  See GuiNetworkSettings.h for the overview and the feature scope.
//
//  Everything is driven by the nmcli client of NetworkManager. Commands are executed through
//  execvp with an argument vector and never through a shell, so SSIDs and passphrases need no
//  escaping at all. nmcli translates its output, so every command runs with LC_ALL=C and the
//  parsers below depend on that.
//
//  Two operations are slow enough to freeze the render loop, the Wi-Fi scan (roughly five to
//  ten seconds) and the activation of a network (up to the activation timeout). Both run on a
//  worker thread behind NetworkBusyOverlay, every other operation is a fast query.
//
//  The passphrase is handed to nmcli on standard input, nmcli --ask registers as a secret agent
//  so the passphrase never shows up in the process list. NetworkManager stores it in the
//  connection profile, which keeps autoconnect working after a reboot.
//

#include "guis/GuiNetworkSettings.h"

#include "Log.h"
#include "MenuFontUtil.h"
#include "Settings.h"
#include "Window.h"
#include "components/BusyComponent.h"
#include "components/SwitchComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextEditKeyboardPopup.h"
#include "guis/GuiTextEditPopup.h"
#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"
#include "views/ViewController.h"

#if defined(__linux__)

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Timeout for one reachability probe, the overall budget for a full round and how long a round
// stays valid before the page probes again.
constexpr int probeTimeout {1200};
constexpr int probeBudget {1500};
constexpr int probeCacheSeconds {60};

// Maximum time to wait for a Wi-Fi activation, passed to nmcli as --wait.
constexpr int activationTimeout {45};

// One reachability target, a plain TCP connect is used so that no HTTP round trip is needed.
// The domestic sources are literal addresses which are answered without a DNS lookup, the
// overseas ones are websites that are blocked in mainland China, a successful connection to
// any of them is therefore a real indication that the overseas internet is reachable.
struct ProbeTarget {
    const char* host;
    int port;
    bool domestic;
};

const std::vector<ProbeTarget>& probeTargets()
{
    static const std::vector<ProbeTarget> targets {
        // Overseas sources.
        {"www.google.com", 443, false},
        {"www.wikipedia.org", 443, false},
        {"www.facebook.com", 443, false},
        // Domestic sources.
        {"223.5.5.5", 53, true},
        {"119.29.29.29", 53, true},
        {"114.114.114.114", 53, true},
        {"www.baidu.com", 443, true},
        {"www.qq.com", 443, true}};
    return targets;
}

std::string trimString(const std::string& str)
{
    const std::string whitespace {" \t\r\n"};
    const size_t start {str.find_first_not_of(whitespace)};
    if (start == std::string::npos)
        return "";
    return str.substr(start, str.find_last_not_of(whitespace) - start + 1);
}

std::vector<std::string> splitLines(const std::string& str)
{
    std::vector<std::string> lines;
    std::string line;
    for (const char character : str) {
        if (character == '\n') {
            lines.push_back(line);
            line.clear();
        }
        else if (character != '\r') {
            line.push_back(character);
        }
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

// nmcli prints "--" instead of an empty value.
std::string cleanValue(const std::string& value)
{
    return value == "--" ? "" : value;
}

// The last non empty line of a command output, which is where nmcli puts its error message.
std::string lastLine(const std::string& text)
{
    const std::vector<std::string> lines {splitLines(text)};
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const std::string value {trimString(*it)};
        if (!value.empty())
            return value;
    }
    return "";
}

// Runs a command through execvp with an argument vector, never through a shell. stdIn is written
// to the child's standard input, stdOut receives the merged standard output and standard error.
// LC_ALL is forced to C inside the child because nmcli translates its output.
int runProcess(const std::vector<std::string>& args,
               const std::string* stdIn,
               std::string* stdOut)
{
    if (args.empty())
        return -1;

    int inPipe[2] {-1, -1};
    int outPipe[2] {-1, -1};
    if (pipe(inPipe) != 0)
        return -1;
    if (pipe(outPipe) != 0) {
        close(inPipe[0]);
        close(inPipe[1]);
        return -1;
    }

    const pid_t pid {fork()};
    if (pid < 0) {
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(inPipe[1]);
        close(outPipe[0]);
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(outPipe[1], STDERR_FILENO);
        close(inPipe[0]);
        close(outPipe[1]);

        setenv("LC_ALL", "C", 1);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(127);
    }

    close(inPipe[0]);
    close(outPipe[1]);

    if (stdIn != nullptr) {
        const char* data {stdIn->data()};
        size_t remaining {stdIn->size()};
        while (remaining > 0) {
            const ssize_t written {write(inPipe[1], data, remaining)};
            if (written <= 0)
                break;
            data += written;
            remaining -= static_cast<size_t>(written);
        }
    }
    close(inPipe[1]);

    std::string output;
    std::array<char, 4096> buffer {};
    while (true) {
        const ssize_t count {read(outPipe[0], buffer.data(), buffer.size())};
        if (count <= 0)
            break;
        output.append(buffer.data(), static_cast<size_t>(count));
    }
    close(outPipe[0]);

    int status {0};
    waitpid(pid, &status, 0);
    if (stdOut != nullptr)
        *stdOut = output;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

// Absolute or PATH resolved location of nmcli, empty when NetworkManager is not installed.
// getPathToBinary() returns the directory holding the executable, the file name has to be
// appended (this is the same handling as in FileData::findEmulator()).
const std::string& nmcliBinary()
{
    static const std::string directory {Utils::FileSystem::getPathToBinary("nmcli")};
    static const std::string path {directory.empty() ? std::string {} : directory + "/nmcli"};
    return path;
}

int runNmcli(const std::vector<std::string>& args, const std::string* stdIn, std::string* stdOut)
{
    if (nmcliBinary().empty()) {
        if (stdOut != nullptr)
            *stdOut = "nmcli is not available";
        return 127;
    }

    std::vector<std::string> command;
    command.reserve(args.size() + 1);
    command.push_back(nmcliBinary());
    command.insert(command.end(), args.begin(), args.end());
    return runProcess(command, stdIn, stdOut);
}

// Runs a query that returns a single value, the first non empty line is used. This is the form
// to use with "-g FIELD" as the output is then free of field names and table padding.
std::string nmcliValue(const std::vector<std::string>& args)
{
    std::string output;
    if (runNmcli(args, nullptr, &output) != 0)
        return "";

    for (const std::string& line : splitLines(output)) {
        const std::string value {trimString(line)};
        if (!value.empty())
            return value;
    }
    return "";
}

// Runs a query in the "nmcli -m multiline" format, which avoids the escaping and column layout
// problems of the terse mode. Every record is a list of field/value pairs in the order nmcli
// printed them, a record ends when the first field reappears.
std::vector<std::vector<std::pair<std::string, std::string>>>
nmcliRecords(const std::vector<std::string>& args)
{
    std::vector<std::string> command {"-m", "multiline"};
    command.insert(command.end(), args.begin(), args.end());

    std::string output;
    if (runNmcli(command, nullptr, &output) != 0)
        return {};

    std::vector<std::vector<std::pair<std::string, std::string>>> records;
    std::vector<std::pair<std::string, std::string>> record;

    for (const std::string& line : splitLines(output)) {
        const size_t separator {line.find(':')};
        if (separator == std::string::npos)
            continue;

        const std::string key {trimString(line.substr(0, separator))};
        if (key.empty())
            continue;

        bool startsNewRecord {false};
        for (const std::pair<std::string, std::string>& field : record) {
            if (field.first == key) {
                startsNewRecord = true;
                break;
            }
        }
        if (startsNewRecord) {
            records.push_back(record);
            record.clear();
        }

        record.emplace_back(key, trimString(line.substr(separator + 1)));
    }

    if (!record.empty())
        records.push_back(record);
    return records;
}

std::string recordValue(const std::vector<std::pair<std::string, std::string>>& record,
                        const std::string& key)
{
    for (const std::pair<std::string, std::string>& field : record) {
        if (field.first == key)
            return field.second;
    }
    return "";
}

// ---------------------------------------------------------------------------------------------
// NetworkManager queries
// ---------------------------------------------------------------------------------------------

struct NetworkDevice {
    std::string name;
    std::string type;
    std::string state;
    std::string connection;
};

std::vector<NetworkDevice> networkDevices()
{
    std::vector<NetworkDevice> devices;

    for (const std::vector<std::pair<std::string, std::string>>& record :
         nmcliRecords({"-f", "DEVICE,TYPE,STATE,CONNECTION", "dev", "status"})) {
        NetworkDevice device;
        device.name = recordValue(record, "DEVICE");
        device.type = recordValue(record, "TYPE");
        device.state = recordValue(record, "STATE");
        device.connection = cleanValue(recordValue(record, "CONNECTION"));
        if (!device.name.empty())
            devices.push_back(device);
    }
    return devices;
}

// The first device NetworkManager currently has a connection on, loopback and the internal
// wifi-p2p interface are skipped.
const NetworkDevice* connectedDevice(const std::vector<NetworkDevice>& devices)
{
    for (const NetworkDevice& device : devices) {
        if (device.type == "loopback" || device.type == "wifi-p2p")
            continue;
        if (device.state == "connected" && !device.connection.empty())
            return &device;
    }
    return nullptr;
}

std::string wifiDeviceName()
{
    for (const NetworkDevice& device : networkDevices()) {
        if (device.type == "wifi")
            return device.name;
    }
    return "";
}

bool wifiRadioEnabled()
{
    return nmcliValue({"-g", "WIFI", "radio"}) == "enabled";
}

// IP4.ADDRESS is reported with a prefix length, only the address itself is of interest here.
std::string deviceIpv4Address(const std::string& device)
{
    const std::string value {nmcliValue({"-g", "IP4.ADDRESS", "dev", "show", device})};
    const size_t separator {value.find('/')};
    if (separator == std::string::npos)
        return value;
    return value.substr(0, separator);
}

std::string deviceIpv4Gateway(const std::string& device)
{
    return nmcliValue({"-g", "IP4.GATEWAY", "dev", "show", device});
}

struct SavedConnection {
    std::string name;
    bool autoconnect {false};
    bool active {false};
};

std::vector<SavedConnection> savedWifiConnections()
{
    std::vector<SavedConnection> connections;

    for (const std::vector<std::pair<std::string, std::string>>& record :
         nmcliRecords({"-f", "NAME,TYPE,AUTOCONNECT,DEVICE", "con", "show"})) {
        if (recordValue(record, "TYPE") != "wifi")
            continue;

        SavedConnection connection;
        connection.name = cleanValue(recordValue(record, "NAME"));
        connection.autoconnect = recordValue(record, "AUTOCONNECT") == "yes";
        connection.active = !cleanValue(recordValue(record, "DEVICE")).empty();
        if (!connection.name.empty())
            connections.push_back(connection);
    }
    return connections;
}

// Scans for Wi-Fi networks. This takes several seconds, so it has to run on a worker thread.
// Hidden access points are reported by nmcli with an empty SSID and are dropped, they can be
// reached through the manual input instead.
std::vector<NetworkWifiEntry> scanWifiNetworks()
{
    std::vector<NetworkWifiEntry> networks;
    const std::vector<SavedConnection> saved {savedWifiConnections()};

    for (const std::vector<std::pair<std::string, std::string>>& record :
         nmcliRecords({"-f", "IN-USE,SSID,SIGNAL,SECURITY", "dev", "wifi", "list", "--rescan",
                       "yes"})) {
        const std::string ssid {cleanValue(recordValue(record, "SSID"))};
        if (ssid.empty())
            continue;

        const int signal {
            std::atoi(cleanValue(recordValue(record, "SIGNAL")).c_str())};
        const std::string security {cleanValue(recordValue(record, "SECURITY"))};
        const bool inUse {cleanValue(recordValue(record, "IN-USE")) == "*"};

        bool merged {false};
        for (NetworkWifiEntry& entry : networks) {
            if (entry.ssid != ssid)
                continue;
            merged = true;
            if (signal > entry.signal) {
                entry.signal = signal;
                entry.security = security;
            }
            entry.inUse = entry.inUse || inUse;
            break;
        }
        if (merged)
            continue;

        NetworkWifiEntry entry;
        entry.ssid = ssid;
        entry.signal = signal;
        entry.security = security;
        entry.inUse = inUse;
        for (const SavedConnection& connection : saved) {
            if (connection.name == ssid) {
                entry.saved = true;
                break;
            }
        }
        networks.push_back(entry);
    }

    // The strongest networks first, networks in use or already saved on top of them.
    std::sort(networks.begin(), networks.end(),
              [](const NetworkWifiEntry& first, const NetworkWifiEntry& second) {
                  if (first.inUse != second.inUse)
                      return first.inUse;
                  if (first.saved != second.saved)
                      return first.saved;
                  if (first.signal != second.signal)
                      return first.signal > second.signal;
                  return first.ssid < second.ssid;
              });
    return networks;
}

// Maps the nmcli SECURITY string to the 802-11-wireless-security.key-mgmt value. A network that
// offers both WPA2 and WPA3 is created as wpa-psk which every access point accepts, only a WPA3
// exclusive network needs sae.
std::string keyManagement(const std::string& security)
{
    const std::string value {Utils::String::toLower(security)};
    if (value.find("wpa3") != std::string::npos && value.find("wpa2") == std::string::npos)
        return "sae";
    return "wpa-psk";
}

// Activates a Wi-Fi network. The passphrase is passed on standard input, nmcli --ask registers
// as a secret agent for the duration of the activation and NetworkManager stores the secret in
// the connection profile afterwards.
bool connectToNetwork(const std::string& ssid,
                      const std::string& security,
                      const std::string& password,
                      std::string& errorText)
{
    if (wifiDeviceName().empty()) {
        errorText = _("NO WI-FI DEVICE FOUND");
        return false;
    }

    // Reuse an existing profile if there is one, otherwise create it first.
    std::string profile;
    for (const SavedConnection& connection : savedWifiConnections()) {
        if (connection.name == ssid) {
            profile = connection.name;
            break;
        }
    }

    std::string output;
    if (profile.empty()) {
        std::vector<std::string> command {"con", "add",     "type", "wifi", "con-name", ssid,
                                          "ssid", ssid};
        if (!password.empty()) {
            command.push_back("802-11-wireless-security.key-mgmt");
            command.push_back(keyManagement(security));
        }
        if (runNmcli(command, nullptr, &output) != 0) {
            errorText = lastLine(output);
            return false;
        }
        profile = ssid;
    }

    std::vector<std::string> command {"--ask", "--wait", std::to_string(activationTimeout),
                                      "con",   "up",   profile};
    std::string standardInput;
    const std::string* standardInputPtr {nullptr};
    if (!password.empty()) {
        standardInput = password + "\n";
        standardInputPtr = &standardInput;
    }
    if (runNmcli(command, standardInputPtr, &output) != 0) {
        errorText = lastLine(output);
        return false;
    }

    // Match what the NetworkManager applet does, a network the user connected to is supposed to
    // be picked up automatically from now on.
    runNmcli({"con", "mod", profile, "connection.autoconnect", "yes"}, nullptr, nullptr);
    return true;
}

bool disconnectDevice(const std::string& device, std::string& errorText)
{
    std::string output;
    if (runNmcli({"dev", "disconnect", device}, nullptr, &output) != 0) {
        errorText = lastLine(output);
        return false;
    }
    return true;
}

bool forgetConnection(const std::string& name, std::string& errorText)
{
    std::string output;
    if (runNmcli({"con", "delete", name}, nullptr, &output) != 0) {
        errorText = lastLine(output);
        return false;
    }
    return true;
}

bool setAutoconnect(const std::string& name, const bool state)
{
    return runNmcli({"con", "mod", name, "connection.autoconnect", state ? "yes" : "no"}, nullptr,
                    nullptr) == 0;
}

// ---------------------------------------------------------------------------------------------
// Reachability probes
// ---------------------------------------------------------------------------------------------

// Non blocking TCP connect probe, returns true when the connection could be established within
// the timeout. Host names are resolved through getaddrinfo, literal addresses are used as they
// are.
bool probeTcp(const std::string& host, const int port, const int timeoutMilliseconds)
{
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* addresses {nullptr};
    const std::string portString {std::to_string(port)};
    if (getaddrinfo(host.c_str(), portString.c_str(), &hints, &addresses) != 0)
        return false;

    bool reachable {false};
    for (struct addrinfo* address = addresses; address != nullptr && !reachable;
         address = address->ai_next) {
        const int descriptor {socket(address->ai_family,
                                     address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                     address->ai_protocol)};
        if (descriptor < 0)
            continue;

        const int connectResult {connect(descriptor, address->ai_addr, address->ai_addrlen)};
        if (connectResult == 0) {
            reachable = true;
        }
        else if (errno == EINPROGRESS) {
            struct pollfd descriptorPoll {};
            descriptorPoll.fd = descriptor;
            descriptorPoll.events = POLLOUT;

            int pollResult {0};
            do {
                pollResult = poll(&descriptorPoll, 1, timeoutMilliseconds);
            } while (pollResult < 0 && errno == EINTR);

            if (pollResult > 0) {
                int socketError {0};
                socklen_t errorLength {sizeof(socketError)};
                if (getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) == 0 &&
                    socketError == 0)
                    reachable = true;
            }
        }
        close(descriptor);
    }

    freeaddrinfo(addresses);
    return reachable;
}

// State shared with the detached probe threads, keeping it in a shared pointer is what makes it
// safe for the straggler threads to write their result after a round has been given up on.
struct ProbeState {
    std::atomic<int> overseasRemaining {0};
    std::atomic<int> domesticRemaining {0};
    std::atomic<bool> overseasReachable {false};
    std::atomic<bool> domesticReachable {false};
};

struct ConnectivityResult {
    bool overseas {false};
    bool domestic {false};
};

// Probes all targets concurrently and returns once both groups have settled or the overall
// budget has expired. NetworkManager's own connectivity check counts as one more overseas
// source, it only reports "full" when its check server returned the expected response.
ConnectivityResult checkConnectivity()
{
    auto state {std::make_shared<ProbeState>()};
    state->overseasReachable =
        nmcliValue({"-g", "CONNECTIVITY", "general", "status"}) == "full";

    const std::vector<ProbeTarget>& targets {probeTargets()};
    for (const ProbeTarget& target : targets) {
        if (target.domestic)
            ++state->domesticRemaining;
        else
            ++state->overseasRemaining;
    }

    for (const ProbeTarget& target : targets) {
        std::thread([state, target] {
            if (probeTcp(target.host, target.port, probeTimeout)) {
                if (target.domestic)
                    state->domesticReachable = true;
                else
                    state->overseasReachable = true;
            }

            if (target.domestic)
                --state->domesticRemaining;
            else
                --state->overseasRemaining;
        }).detach();
    }

    const auto deadline {std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(probeBudget)};
    while (std::chrono::steady_clock::now() < deadline) {
        const bool overseasSettled {state->overseasReachable || state->overseasRemaining == 0};
        const bool domesticSettled {state->domesticReachable || state->domesticRemaining == 0};
        if (overseasSettled && domesticSettled)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ConnectivityResult result;
    result.overseas = state->overseasReachable;
    result.domestic = state->domesticReachable;
    return result;
}

// A round takes a moment, so the result is reused for a short while when the page is reopened.
ConnectivityResult connectivityStatus()
{
    static bool valid {false};
    static std::chrono::steady_clock::time_point timestamp;
    static ConnectivityResult cached;

    const auto now {std::chrono::steady_clock::now()};
    if (valid && std::chrono::duration_cast<std::chrono::seconds>(now - timestamp).count() <
                     probeCacheSeconds)
        return cached;

    cached = checkConnectivity();
    timestamp = now;
    valid = true;
    return cached;
}

} // namespace

// Full screen overlay which shows the busy animation while a slow command runs on a worker
// thread. It removes itself once the task has finished and only then calls the completion
// function, so a page can be replaced with an up to date one without leaving a stale page
// behind in the GUI stack.
class NetworkBusyOverlay : public GuiComponent
{
public:
    NetworkBusyOverlay(const std::string& text,
                       const std::function<void()>& task,
                       const std::function<void()>& onFinished);
    ~NetworkBusyOverlay() override;

    bool input(InputConfig* config, Input input) override { return true; }
    void update(int deltaTime) override;

private:
    BusyComponent mBusyAnim;
    std::unique_ptr<std::thread> mThread;
    std::atomic<bool> mFinished {false};
    std::function<void()> mOnFinished;
};

NetworkBusyOverlay::NetworkBusyOverlay(const std::string& text,
                                       const std::function<void()>& task,
                                       const std::function<void()>& onFinished)
    : GuiComponent {}
    , mOnFinished {onFinished}
{
    setSize(Renderer::getScreenWidth(), Renderer::getScreenHeight());
    mBusyAnim.setText(text);
    mBusyAnim.setSize(mSize);
    mBusyAnim.onSizeChanged();
    addChild(&mBusyAnim);

    mThread = std::make_unique<std::thread>([this, task] {
        task();
        mFinished = true;
    });
}

NetworkBusyOverlay::~NetworkBusyOverlay()
{
    if (mThread && mThread->joinable())
        mThread->join();
}

void NetworkBusyOverlay::update(int deltaTime)
{
    if (!mFinished) {
        GuiComponent::update(deltaTime);
        return;
    }

    mFinished = false;
    if (mThread && mThread->joinable())
        mThread->join();

    const std::function<void()> onFinished {mOnFinished};
    mOnFinished = nullptr;
    delete this;

    if (onFinished)
        onFinished();
}

// ---------------------------------------------------------------------------------------------
// Row builders
// ---------------------------------------------------------------------------------------------

ComponentListRow GuiNetworkSettings::makeSectionRow(const std::string& label)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorSecondary),
                   true);
    return row;
}

// Not a data row, the secondary color and the missing arrow keep it apart from the rows below.
ComponentListRow GuiNetworkSettings::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_LEFT, ALIGN_CENTER, glm::ivec2 {0, 0}),
                   true);
    return row;
}

// Read only row without an arrow and without an action.
ComponentListRow GuiNetworkSettings::makeInfoRow(const std::string& label, const std::string& value)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    row.addElement(std::make_shared<TextComponent>(value, menuValueFont(), mMenuColorSecondary,
                                                   ALIGN_RIGHT),
                   false);
    return row;
}

ComponentListRow GuiNetworkSettings::makeNavRow(GuiSettings* page,
                                                const std::string& label,
                                                const std::string& value,
                                                const std::function<void()>& func)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorPrimary),
                   true);
    if (!value.empty()) {
        row.addElement(std::make_shared<TextComponent>(value, menuValueFont(),
                                                       mMenuColorSecondary, ALIGN_RIGHT),
                       false);
    }
    row.addElement(page->getMenu().makeArrow(), false);
    if (func)
        row.makeAcceptInputHandler(func);
    return row;
}

// ---------------------------------------------------------------------------------------------
// Current status page
// ---------------------------------------------------------------------------------------------

// The read only information, it used to sit directly below the Wi-Fi settings which made the
// main page a mix of information and configuration.
GuiSettings* GuiNetworkSettings::buildCurrentStatusPage()
{
    GuiSettings* page {new GuiSettings(_("CURRENT STATUS"))};

    addInformationRows(page);
    addInternetStatusRows(page);
    addEthernetRows(page);

    return page;
}

void GuiNetworkSettings::addInformationRows(GuiSettings* page)
{
    const std::vector<NetworkDevice> devices {networkDevices()};
    const NetworkDevice* active {connectedDevice(devices)};

    page->addRow(makeSectionRow(_("INFORMATION")));
    page->addRow(makeInfoRow(_("CONNECTION"),
                             active != nullptr ? active->connection : _("NOT CONNECTED")));

    std::string address {"-"};
    std::string gateway {"-"};
    if (active != nullptr) {
        const std::string value {deviceIpv4Address(active->name)};
        if (!value.empty())
            address = value;
        const std::string route {deviceIpv4Gateway(active->name)};
        if (!route.empty())
            gateway = route;
    }
    page->addRow(makeInfoRow(_("IP ADDRESS"), address));
    page->addRow(makeInfoRow(_("GATEWAY"), gateway));
}

void GuiNetworkSettings::addInternetStatusRows(GuiSettings* page)
{
    const ConnectivityResult status {connectivityStatus()};

    page->addRow(makeSectionRow(_("INTERNET STATUS")));
    page->addRow(makeInfoRow(_("OVERSEAS NETWORK"), status.overseas ? _("CONNECTED") :
                                                                     _("NOT CONNECTED")));
    page->addRow(makeInfoRow(_("DOMESTIC NETWORK"), status.domestic ? _("CONNECTED") :
                                                                     _("NOT CONNECTED")));
}

// ---------------------------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------------------------

GuiNetworkSettings::GuiNetworkSettings(const std::string& title)
    : GuiSettings {title}
{
    if (nmcliBinary().empty()) {
        addRow(makeNoteRow(_("NETWORK MANAGER IS NOT AVAILABLE")));
        return;
    }

    // Everything below is a setting, the read only information has been moved to its own page.
    addRow(makeNavRow(this, _("CURRENT STATUS"), "",
                      [this] { mWindow->pushGui(buildCurrentStatusPage()); }));
    addWifiRows();
}

void GuiNetworkSettings::addWifiRows()
{
    addRow(makeSectionRow(_("WI-FI")));

    const std::string device {wifiDeviceName()};
    if (device.empty()) {
        addRow(makeNoteRow(_("NO WI-FI DEVICE FOUND")));
        return;
    }

    const bool wifiEnabled {wifiRadioEnabled()};
    auto radioSwitch {std::make_shared<SwitchComponent>(wifiEnabled)};
    // The constructor always starts with the off image, the state has to be applied explicitly.
    radioSwitch->setState(wifiEnabled);
    SwitchComponent* radioSwitchPtr {radioSwitch.get()};
    radioSwitch->setCallback([radioSwitchPtr] {
        std::string output;
        runNmcli({"radio", "wifi", radioSwitchPtr->getState() ? "on" : "off"}, nullptr, &output);
    });
    addWithLabel(_("ENABLE WI-FI"), radioSwitch);

    addRow(makeNavRow(this, _("AVAILABLE NETWORKS"), "", [this] { openAvailableNetworks(); }));

    const std::vector<SavedConnection> saved {savedWifiConnections()};
    addRow(makeNavRow(this, _("SAVED NETWORKS"),
                      saved.empty() ? "" : std::to_string(saved.size()),
                      [this] { openSavedNetworks(); }));

    bool active {false};
    for (const SavedConnection& connection : saved) {
        if (connection.active) {
            active = true;
            break;
        }
    }

    if (active) {
        addRow(makeNavRow(this, _("DISCONNECT"), "", [this, device] {
            std::string errorText;
            if (!disconnectDevice(device, errorText)) {
                mWindow->pushGui(new GuiMsgBox(
                    _("FAILED TO DISCONNECT") + "\n" + errorText, _("OK")));
                return;
            }

            // Rebuild the page so the connection information is up to date again.
            Window* window {mWindow};
            window->pushGui(new GuiNetworkSettings(_("NETWORK SETTINGS")));
            delete this;
        }));
    }
}

void GuiNetworkSettings::addEthernetRows(GuiSettings* page)
{
    const std::vector<NetworkDevice> devices {networkDevices()};

    std::vector<const NetworkDevice*> wired;
    for (const NetworkDevice& device : devices) {
        if (device.type == "ethernet")
            wired.push_back(&device);
    }
    if (wired.empty())
        return;

    page->addRow(makeSectionRow(_("ETHERNET")));
    for (const NetworkDevice* device : wired) {
        page->addRow(makeInfoRow(device->name, device->state == "connected" ? _("CONNECTED") :
                                                                             _("NOT CONNECTED")));
    }
}

void GuiNetworkSettings::openAvailableNetworks()
{
    Window* window {mWindow};
    auto networks {std::make_shared<std::vector<NetworkWifiEntry>>()};
    window->pushGui(new NetworkBusyOverlay(
        _("SCANNING FOR WI-FI NETWORKS..."), [networks] { *networks = scanWifiNetworks(); },
        [window, networks] { window->pushGui(buildNetworksPage(window, *networks)); }));
}

void GuiNetworkSettings::openSavedNetworks()
{
    mWindow->pushGui(buildSavedPage(mWindow));
}

// ---------------------------------------------------------------------------------------------
// Available networks page
// ---------------------------------------------------------------------------------------------

GuiSettings* GuiNetworkSettings::buildNetworksPage(Window* window,
                                                   const std::vector<NetworkWifiEntry>& networks)
{
    GuiSettings* page {new GuiSettings(_("AVAILABLE NETWORKS"))};

    auto sharedNetworks {std::make_shared<std::vector<NetworkWifiEntry>>(networks)};

    if (networks.empty()) {
        page->addRow(makeNoteRow(_("NO WI-FI NETWORKS FOUND")));
    }
    else {
        for (std::size_t index {0}; index < networks.size(); ++index) {
            const NetworkWifiEntry& network {networks.at(index)};

            ComponentListRow row;
            std::string label {network.ssid};
            if (network.inUse)
                label = ViewController::TICKMARK_CHAR + " " + label;
            row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                           mMenuColorPrimary),
                           true);

            std::string value {std::to_string(network.signal) + "%"};
            value += "  " + (network.security.empty() ? _("OPEN") : network.security);
            row.addElement(std::make_shared<TextComponent>(value, menuValueFont(),
                                                           mMenuColorSecondary, ALIGN_RIGHT),
                           false);

            row.makeAcceptInputHandler(
                [window, page, sharedNetworks, index] { selectNetwork(window, page, sharedNetworks,
                                                                      index); });
            page->addRow(row);
        }
    }

    page->getMenu().addButton(_("SCAN"), _("scan"), [window, page] { rescanNetworks(window, page); });
    page->getMenu().addButton(_("INPUT MANUALLY"), _("manual input"),
                              [window] { inputSsidManually(window); });

    return page;
}

void GuiNetworkSettings::rescanNetworks(Window* window, GuiSettings* page)
{
    auto networks {std::make_shared<std::vector<NetworkWifiEntry>>()};
    window->pushGui(new NetworkBusyOverlay(
        _("SCANNING FOR WI-FI NETWORKS..."), [networks] { *networks = scanWifiNetworks(); },
        [window, page, networks] {
            window->pushGui(buildNetworksPage(window, *networks));
            delete page;
        }));
}

// Networks that are already saved are activated straight away, the passphrase is stored in the
// connection profile. Open networks need no passphrase at all, everything else asks for one.
void GuiNetworkSettings::selectNetwork(Window* window,
                                       GuiSettings* page,
                                       const std::shared_ptr<std::vector<NetworkWifiEntry>>& networks,
                                       const std::size_t index)
{
    const NetworkWifiEntry network {networks->at(index)};

    if (network.saved || network.security.empty()) {
        activateNetwork(window, page, networks, index, "");
        return;
    }

    showTextInput(window, _("ENTER WI-FI PASSWORD"), "",
                  [window, page, networks, index](const std::string& password) {
                      activateNetwork(window, page, networks, index, password);
                  });
}

void GuiNetworkSettings::activateNetwork(
    Window* window,
    GuiSettings* page,
    const std::shared_ptr<std::vector<NetworkWifiEntry>>& networks,
    const std::size_t index,
    const std::string& password)
{
    const NetworkWifiEntry network {networks->at(index)};
    const std::string ssid {network.ssid};
    const std::string security {network.security};

    auto success {std::make_shared<bool>(false)};
    auto errorText {std::make_shared<std::string>()};

    window->pushGui(new NetworkBusyOverlay(
        _("CONNECTING") + " " + ssid,
        [ssid, security, password, success, errorText] {
            *success = connectToNetwork(ssid, security, password, *errorText);
        },
        [window, page, networks, index, ssid, success, errorText] {
            if (!*success) {
                window->pushGui(new GuiMsgBox(
                    _("FAILED TO CONNECT TO") + " " + ssid + "\n" + *errorText, _("OK")));
                return;
            }

            window->queueInfoPopup(_("CONNECTED TO") + " " + ssid, 5000);

            // Mark the new connection and rebuild the list so the tick mark is up to date.
            for (NetworkWifiEntry& entry : *networks)
                entry.inUse = false;
            networks->at(index).inUse = true;

            window->pushGui(buildNetworksPage(window, *networks));
            delete page;
        }));
}

void GuiNetworkSettings::inputSsidManually(Window* window)
{
    showTextInput(window, _("ENTER SSID"), "", [window](const std::string& ssid) {
        const std::string trimmedSsid {trimString(ssid)};
        if (trimmedSsid.empty())
            return;

        // Hidden access points are almost always WPA2 protected, so a passphrase is offered and
        // an empty one creates an open connection.
        showTextInput(window, _("ENTER WI-FI PASSWORD"), "",
                      [window, trimmedSsid](const std::string& password) {
                          auto networks {std::make_shared<std::vector<NetworkWifiEntry>>()};
                          NetworkWifiEntry entry;
                          entry.ssid = trimmedSsid;
                          networks->push_back(entry);
                          activateNetwork(window, nullptr, networks, 0, password);
                      });
    });
}

void GuiNetworkSettings::showTextInput(Window* window,
                                       const std::string& title,
                                       const std::string& initial,
                                       const std::function<void(const std::string&)>& callback)
{
    if (Settings::getInstance()->getBool("VirtualKeyboard"))
        window->pushGui(new GuiTextEditKeyboardPopup(0.0f, title, initial, callback, false));
    else
        window->pushGui(new GuiTextEditPopup(title, initial, callback, false));
}

// ---------------------------------------------------------------------------------------------
// Saved networks page
// ---------------------------------------------------------------------------------------------

GuiSettings* GuiNetworkSettings::buildSavedPage(Window* window)
{
    GuiSettings* page {new GuiSettings(_("SAVED NETWORKS"))};

    const std::vector<SavedConnection> connections {savedWifiConnections()};
    if (connections.empty()) {
        page->addRow(makeNoteRow(_("NO SAVED WI-FI NETWORKS")));
        return page;
    }

    for (const SavedConnection& connection : connections) {
        ComponentListRow row;
        row.addElement(std::make_shared<TextComponent>(connection.name,
                                                       getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                       mMenuColorPrimary),
                       true);
        std::string value;
        if (connection.active)
            value = _("CONNECTED");
        else if (!connection.autoconnect)
            value = _("AUTOCONNECT") + ": off";
        row.addElement(std::make_shared<TextComponent>(value, menuValueFont(), mMenuColorSecondary,
                                                       ALIGN_RIGHT),
                       false);
        row.addElement(page->getMenu().makeArrow(), false);

        const std::string name {connection.name};
        row.makeAcceptInputHandler(
            [window, page, name] { window->pushGui(buildSavedConnectionPage(window, page, name)); });
        page->addRow(row);
    }

    return page;
}

GuiSettings* GuiNetworkSettings::buildSavedConnectionPage(Window* window,
                                                          GuiSettings* savedPage,
                                                          const std::string& name)
{
    GuiSettings* page {new GuiSettings(name)};

    bool active {false};
    bool autoconnect {false};
    for (const SavedConnection& connection : savedWifiConnections()) {
        if (connection.name == name) {
            active = connection.active;
            autoconnect = connection.autoconnect;
            break;
        }
    }

    const std::string device {wifiDeviceName()};

    // Closes the connection page together with the saved networks page and opens a fresh copy of
    // the latter, the menu component cannot be rebuilt in place.
    auto refresh {[window, savedPage, page] {
        window->pushGui(buildSavedPage(window));
        delete page;
        delete savedPage;
    }};

    if (!active) {
        page->addRow(makeNavRow(page, _("CONNECT"), "", [window, page, name, refresh] {
            auto success {std::make_shared<bool>(false)};
            auto errorText {std::make_shared<std::string>()};

            window->pushGui(new NetworkBusyOverlay(
                _("CONNECTING") + " " + name,
                [name, success, errorText] {
                    *success = connectToNetwork(name, "", "", *errorText);
                },
                [window, name, success, errorText, refresh] {
                    if (!*success) {
                        window->pushGui(new GuiMsgBox(
                            _("FAILED TO CONNECT TO") + " " + name + "\n" + *errorText, _("OK")));
                        return;
                    }
                    window->queueInfoPopup(_("CONNECTED TO") + " " + name, 5000);
                    refresh();
                }));
        }));
    }
    else {
        page->addRow(makeNavRow(page, _("DISCONNECT"), "", [window, device, refresh] {
            auto errorText {std::make_shared<std::string>()};

            window->pushGui(new NetworkBusyOverlay(
                _("DISCONNECT"), [device, errorText] { disconnectDevice(device, *errorText); },
                [window, errorText, refresh] {
                    if (!errorText->empty()) {
                        window->pushGui(new GuiMsgBox(
                            _("FAILED TO DISCONNECT") + "\n" + *errorText, _("OK")));
                        return;
                    }
                    refresh();
                }));
        }));
    }

    auto autoconnectSwitch {std::make_shared<SwitchComponent>(autoconnect)};
    // The constructor always starts with the off image, the state has to be applied explicitly.
    autoconnectSwitch->setState(autoconnect);
    SwitchComponent* autoconnectSwitchPtr {autoconnectSwitch.get()};
    page->addWithLabel(_("AUTOCONNECT"), autoconnectSwitch);
    page->addSaveFunc([name, autoconnectSwitchPtr, autoconnect] {
        if (autoconnectSwitchPtr->getState() == autoconnect)
            return;
        setAutoconnect(name, autoconnectSwitchPtr->getState());
    });

    page->addRow(makeNavRow(page, _("FORGET NETWORK"), "", [window, name, refresh] {
        window->pushGui(new GuiMsgBox(
            _("ARE YOU SURE YOU WANT TO FORGET THE NETWORK") + "\n" + name, _("FORGET NETWORK"),
            [window, name, refresh] {
                std::string errorText;
                if (!forgetConnection(name, errorText)) {
                    window->pushGui(new GuiMsgBox(
                        _("FAILED TO FORGET THE NETWORK") + "\n" + errorText, _("OK")));
                    return;
                }
                refresh();
            },
            _("CANCEL")));
    }));

    return page;
}

#else // !defined(__linux__)

// NetworkManager and nmcli are Linux only, the main menu entry is not added elsewhere.
GuiNetworkSettings::GuiNetworkSettings(const std::string& title)
    : GuiSettings {title}
{
}

#endif // defined(__linux__)
