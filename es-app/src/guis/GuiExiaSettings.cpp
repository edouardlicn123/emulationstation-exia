//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiExiaSettings.cpp
//
//  Exia specific settings page, reachable from the main menu as EXIA SETTINGS.
//
//  Contents:
//    BIOS SWITCH          copy a BIOS variant from ~/biosalt into ~/bios
//    SYSTEM INFO          statistics, exia.conf paths and the boot session status
//    GIT PROXY            toggle and edit the git http proxy
//    LIGHTDM COUNTDOWN    autologin-user-timeout, written with sudo (Linux only)
//    MENU FONT SIZE       menu row text scale, stored as menu.font.scale, needs a restart
//    SUDO PASSWORD        stored in exia.conf, used by the LightDM entry
//
//  No shell commands are constructed from user input, everything runs through execvp with an
//  argument vector, and the sudo password is passed on stdin because it would otherwise be
//  visible in /proc/*/cmdline.
//

#include "guis/GuiExiaSettings.h"

#include "ApplicationVersion.h"
#include "Log.h"
#include "MenuFontUtil.h"
#include "SystemData.h"
#include "Window.h"
#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/TextComponent.h"
#include "guis/GuiTextEditPopup.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const std::string lightdmConfPath {"/etc/lightdm/lightdm.conf"};
const std::string lightdmKey {"autologin-user-timeout"};

std::string trimString(const std::string& str)
{
    const std::string whitespace {" \t\r\n"};
    const size_t start {str.find_first_not_of(whitespace)};
    if (start == std::string::npos)
        return "";
    return str.substr(start, str.find_last_not_of(whitespace) - start + 1);
}

bool readFileLines(const std::string& path, std::vector<std::string>& lines)
{
    std::ifstream file {path, std::ios::binary};
    if (!file.is_open())
        return false;
    lines.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return true;
}

// Writes the lines to a temporary file which then atomically replaces the target file. The
// caller is responsible for setting the final file mode where that matters.
bool writeFileLines(const std::string& path, const std::vector<std::string>& lines)
{
    const std::string tempPath {path + ".new"};
    {
        std::ofstream file {tempPath, std::ios::binary | std::ios::trunc};
        if (!file.is_open())
            return false;
        for (const std::string& line : lines)
            file << line << "\n";
        if (!file.good()) {
            file.close();
            std::remove(tempPath.c_str());
            return false;
        }
    }
    if (std::rename(tempPath.c_str(), path.c_str()) != 0) {
        std::remove(tempPath.c_str());
        return false;
    }
    return true;
}

// Byte for byte comparison, used to detect which BIOS variant is currently installed. This
// replaces an md5sum dependency, the files involved are at most 1 MB.
bool filesIdentical(const std::string& firstPath, const std::string& secondPath)
{
    std::ifstream first {firstPath, std::ios::binary};
    std::ifstream second {secondPath, std::ios::binary};
    if (!first.is_open() || !second.is_open())
        return false;

    std::array<char, 4096> firstBuffer {};
    std::array<char, 4096> secondBuffer {};
    while (true) {
        first.read(firstBuffer.data(), static_cast<std::streamsize>(firstBuffer.size()));
        second.read(secondBuffer.data(), static_cast<std::streamsize>(secondBuffer.size()));
        const std::streamsize firstCount {first.gcount()};
        const std::streamsize secondCount {second.gcount()};
        if (firstCount != secondCount)
            return false;
        if (firstCount == 0)
            break;
        if (std::memcmp(firstBuffer.data(), secondBuffer.data(),
                        static_cast<size_t>(firstCount)) != 0)
            return false;
    }
    return true;
}

// Runs a program without involving a shell. Returns the exit code, or -1 if the process could
// not be started at all.
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

#if !defined(__ANDROID__) && !defined(__IOS__)
// Sudo variant, the password travels on stdin so it never shows up in the process list.
int runSudo(const std::vector<std::string>& args, std::string* stdOut)
{
    std::vector<std::string> command {"sudo", "-S", "-k"};
    command.insert(command.end(), args.begin(), args.end());
    const std::string password {ExiaConfig::getInstance().get("security.sudo.password", "emu") +
                                "\n"};
    return runProcess(command, &password, stdOut);
}
#endif

// Reads the first line of output from a fixed command. The command never contains user input.
std::string popenOutput(const std::string& command)
{
    std::string output;
    FILE* pipeHandle {popen(command.c_str(), "r")};
    if (pipeHandle == nullptr)
        return output;
    std::array<char, 256> buffer {};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipeHandle) != nullptr)
        output += buffer.data();
    pclose(pipeHandle);
    return trimString(output);
}

#if !defined(__ANDROID__) && !defined(__IOS__)
std::string readLightdmValue()
{
    std::vector<std::string> lines;
    if (!readFileLines(lightdmConfPath, lines))
        return "";
    for (const std::string& line : lines) {
        const std::string trimmed {trimString(line)};
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        const size_t separator {trimmed.find('=')};
        if (separator == std::string::npos)
            continue;
        if (trimString(trimmed.substr(0, separator)) == lightdmKey)
            return trimString(trimmed.substr(separator + 1));
    }
    return "";
}

// Patches the LightDM configuration with the new timeout and installs it with sudo. The file
// mode of the target is preserved by cp, only the single key line is changed.
bool writeLightdmValue(const std::string& value)
{
    std::vector<std::string> lines;
    if (!readFileLines(lightdmConfPath, lines))
        return false;

    bool replaced {false};
    for (std::string& line : lines) {
        const std::string trimmed {trimString(line)};
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        const size_t separator {trimmed.find('=')};
        if (separator == std::string::npos)
            continue;
        if (trimString(trimmed.substr(0, separator)) == lightdmKey) {
            line = lightdmKey + "=" + value;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        return false;

    // Exia local fix (devdoc/15 §6.6): the real home, getHomePath() is the --home data
    // directory here and the staging file must sit next to the central configuration file.
    const std::string stagingPath {Utils::FileSystem::getSystemHomeDirectory() +
                                   "/.config/emu/lightdm.conf.staged"};
    if (!writeFileLines(stagingPath, lines))
        return false;

    const int result {runSudo({"cp", stagingPath, lightdmConfPath}, nullptr)};
    std::remove(stagingPath.c_str());
    return result == 0;
}
#endif

// Default content for ~/.config/emu/exia.conf, only used when the file does not exist.
const char* defaultConfTemplate {
R"(# ---------- Exia Game Station — 中央配置 ---------- #
# 风格参考 batocera.conf:分区标题 + ## 小节说明 + key=value;整行 # = 用默认值。
# 消费者:es-de-session(解析 esde.launch)、ES-DE 主菜单 EXIA SETTINGS(C++ 读写)、
#         ~/setting.sh → settings.py(备用)。
# 权限 0600(含 sudo 密码)。本文件是唯一真源,已合并 core.conf 与 es_de.conf。
# 注意:点分键不能被 shell source,消费方按第一个 = 切分;值必须写绝对路径(无 $HOME 展开)。

# ------------ A - Security ------------ #

## 提权写入 /etc/lightdm/lightdm.conf 时使用的 sudo 密码。
security.sudo.password=emu

# ------------ B - Interface ------------ #

## 菜单语言(原 core.conf 的 MENU_LANG 合并至此)。仅 setting.sh 备用入口读写;
## ES-DE 本体语言在 UI SETTINGS → APPLICATION LANGUAGE,互不相干。
## 取值 zh_CN / zh_TW / en_US / ja_JP / fr_FR / es_ES / de_DE / pt_PT
menu.lang=zh_TW

## EXIA SETTINGS → MENU FONT SIZE 写入的菜单行文字缩放系数(1.00 = 100%)。
## 取值 0.80~1.50;main.cpp 启动时读取,作用于 ThemeData::ThemeMenu::getFont(),
## 改动需重启 ES-DE 生效(主菜单 RESTART ES-DE)。
menu.font.scale=1.00

# ------------ C - Boot ------------ #

## 开机后可打断进桌面的倒计时(秒)。
## 此处是规范值,实际生效于 /etc/lightdm/lightdm.conf 的 autologin-user-timeout,
## 由 EXIA SETTINGS 经 sudo 同步过去。
boot.lightdm.timeout=7

# ------------ D - Network ------------ #

## git 全局代理开关与地址。
git.proxy.enabled=0
git.proxy.url=http://127.0.0.1:20171

# ------------ E - BIOS ------------ #

## 各平台当前 BIOS 版本(源 ~/biosalt → 目标 ~/bios)。
## 以目标文件实际内容为准:菜单每次进入做字节比对,若与本节不符则回写本节。
bios.pc98=font(cn).bmp
bios.pcecd=[BIOS]Super CD-ROM System (CN) (3.0_v2 for Shiawase_Usagi).pce
bios.pcfx=pcfx(JP).rom

# ------------ F - ES-DE Paths ------------ #

## 会话脚本与 EXIA SETTINGS 的 ES-DE PATHS 均以此为准(原 es_de.conf)。
esde.home.parent=/home/emu/.config/emu
esde.roms=/home/emu/roms/emus
esde.themes=/home/emu/themes
esde.media=/home/emu/.config/emu/ES-DE/downloaded_media
esde.launch=es-de --home /home/emu/.config/emu
)"};

std::vector<std::string> splitTemplate()
{
    std::vector<std::string> lines;
    std::istringstream stream {std::string {defaultConfTemplate}};
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

void indexLines(std::vector<std::string>& lines, std::map<std::string, size_t>& index)
{
    index.clear();
    for (size_t i {0}; i < lines.size(); ++i) {
        const std::string trimmed {trimString(lines[i])};
        if (trimmed.empty() || trimmed.front() == '#')
            continue;
        const size_t separator {trimmed.find('=')};
        if (separator == std::string::npos)
            continue;
        index[trimString(trimmed.substr(0, separator))] = i;
    }
}

} // namespace

ExiaConfig& ExiaConfig::getInstance()
{
    static ExiaConfig instance;
    if (!instance.mLoaded)
        instance.load();
    return instance;
}

std::string ExiaConfig::getPath()
{
    // Exia local fix (devdoc/15 §6.6): this must be the real user home, not
    // Utils::FileSystem::getHomePath(), because ES-DE is started with
    // --home ~/.config/emu which makes getHomePath() return that data directory. Every shell
    // consumer (es-de-session, settings.py, setting.sh) uses $HOME/.config/emu/exia.conf, so
    // getSystemHomeDirectory() (the HOME environment variable) is the matching path here.
    return Utils::FileSystem::getSystemHomeDirectory() + "/.config/emu/exia.conf";
}

bool ExiaConfig::load()
{
    mLoaded = true;
    mLines.clear();
    mIndex.clear();

    if (!readFileLines(getPath(), mLines)) {
        // Recreate the file from the default template if it was deleted. A failure here means
        // the template only lives in memory and every later set() would be silently lost, so
        // it must show up in the log (devdoc/15 §6.6).
        mLines = splitTemplate();
        if (!writeFileLines(getPath(), mLines)) {
            LOG(LogError) << "ExiaConfig: cannot create " << getPath()
                          << ", changes will not be persisted";
        }
    }
    indexLines(mLines, mIndex);
    return true;
}

bool ExiaConfig::save()
{
    if (!writeFileLines(getPath(), mLines)) {
        // Never swallow a failed write: that is exactly how the broken --home path stayed
        // invisible for three rounds (devdoc/15 §6.6).
        LOG(LogError) << "ExiaConfig: failed to write " << getPath();
        return false;
    }
#if !defined(__ANDROID__) && !defined(__IOS__)
    // The file stores the sudo password, so it must not be world readable.
    ::chmod(getPath().c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
}

std::string ExiaConfig::get(const std::string& key, const std::string& def)
{
    if (!mLoaded)
        load();
    const auto entry {mIndex.find(key)};
    if (entry == mIndex.end())
        return def;
    const std::string& line {mLines[entry->second]};
    const size_t separator {line.find('=')};
    if (separator == std::string::npos)
        return def;
    return trimString(line.substr(separator + 1));
}

bool ExiaConfig::set(const std::string& key, const std::string& value)
{
    if (!mLoaded)
        load();
    const std::string entry {key + "=" + value};
    const auto existing {mIndex.find(key)};
    if (existing != mIndex.end())
        mLines[existing->second] = entry;
    else {
        // New keys are appended at the end of the file, the section comments above them are
        // never rewritten.
        mLines.push_back(entry);
        mIndex[key] = mLines.size() - 1;
    }
    return save();
}

namespace {

struct BiosPlatform
{
    std::string label;
    std::string confKey;
    std::string target;
    std::vector<std::pair<std::string, std::string>> options;
};

// Target paths are taken from the Batocera hardware database (batocera-systems) and confirmed
// against the strings of the installed RetroArch cores: np2kai looks for <system>/np2kai/font.bmp,
// beetle pce for <system>/syscard3.pce and beetle pcfx for <system>/pcfx.rom. The sources are
// relative to ~/biosalt which is the backup and must never be written to.
const std::vector<BiosPlatform>& biosPlatforms()
{
    static const std::vector<BiosPlatform> platforms {
        {_("PC-98"), "bios.pc98", "np2kai/font.bmp",
         {{_("ORIGINAL FONT (JAPANESE)"), "pc98/font(jp).bmp"},
          {_("CHINESE FONT (FOR GALGAMES)"), "pc98/font(cn).bmp"},
          {_("EDO THE GREAT (CHINESE)"), "pc98/EDO(cn_v1.01).bmp"}}},
        {_("PC ENGINE CD"), "bios.pcecd", "syscard3.pce",
         {{_("ORIGINAL BIOS"), "pcecd/Originalsyscard3.pce"},
          {_("CHINESE BIOS"),
           "pcecd/[BIOS]Super CD-ROM System (CN) (3.0_v2 for Shiawase_Usagi).pce"}}},
        {_("PC-FX"), "bios.pcfx", "pcfx.rom",
         {{_("ORIGINAL BIOS"), "pcfx/pcfx(JP).rom"}, {_("CHINESE BIOS"), "pcfx/pcfx(CN).rom"}}},
    };
    return platforms;
}

std::string fileNameOf(const std::string& relativePath)
{
    const size_t separator {relativePath.find_last_of('/')};
    return separator == std::string::npos ? relativePath : relativePath.substr(separator + 1);
}

// Current menu font scale as a percentage, e.g. 100. The stored value is the factor itself
// (menu.font.scale=1.00), anything unusable falls back to 100%. The result is snapped to the
// 2% slider step (80, 82, ... 150) so an odd value left over from the old text input does not
// make the drag bar sit between two stops.
int currentScalePercent()
{
    const std::string stored {ExiaConfig::getInstance().get("menu.font.scale", "1.00")};
    const float scale {static_cast<float>(std::atof(stored.c_str()))};
    int percent {scale <= 0.0f ? 100 : static_cast<int>(scale * 100.0f + 0.5f)};
    if (percent < 80)
        percent = 80;
    else if (percent > 150)
        percent = 150;
    return 80 + static_cast<int>(std::round((percent - 80) / 2.0f)) * 2;
}

} // namespace

GuiExiaSettings::GuiExiaSettings(const std::string& title)
    : GuiSettings {title}
{
    // Make sure the central configuration file exists before anything reads from it.
    ExiaConfig::getInstance();

    addRow(makeNavRow(_("Chinese Localization settings"), "", [this] { openBiosSwitch(); }));
    addRow(makeNavRow(_("SYSTEM INFO"), "", [this] { openSystemInfo(); }));
    addRow(makeNavRow(_("GIT PROXY"), "", [this] { openGitProxy(); }));

#if !defined(__ANDROID__) && !defined(__IOS__)
    // Read current timeout from lightdm.conf (fallback to exia.conf).
    std::string currentTimeoutStr {readLightdmValue()};
    if (currentTimeoutStr.empty())
        currentTimeoutStr = ExiaConfig::getInstance().get("boot.lightdm.timeout", "7");
    int currentTimeout {std::atoi(currentTimeoutStr.c_str())};
    if (currentTimeout < 2)
        currentTimeout = 2;
    else if (currentTimeout > 10)
        currentTimeout = 10;

    // A drag bar operated with the direction keys, range 2-10 seconds, step 1.
    auto slider = std::make_shared<SliderComponent>(2.0f, 10.0f, 1.0f, "s");
    slider->setValue(static_cast<float>(currentTimeout));

    addWithLabel(_("LIGHTDM COUNTDOWN"), slider);
    addSaveFunc([slider, currentTimeout, window = mWindow] {
        const int newTimeout {static_cast<int>(std::round(slider->getValue()))};
        // Only write when the value really changed.
        if (newTimeout == currentTimeout)
            return;
        const std::string valueStr {std::to_string(newTimeout)};
        if (!writeLightdmValue(valueStr)) {
            window->queueInfoPopup(
                _("FAILED TO UPDATE LIGHTDM.CONF, WRONG SUDO PASSWORD?"), 6000);
            return;
        }
        ExiaConfig::getInstance().set("boot.lightdm.timeout", valueStr);
        window->queueInfoPopup(
            Utils::String::format(_("LIGHTDM COUNTDOWN SET TO %s SECONDS"),
                                  valueStr.c_str()),
            5000);
    });
#endif

    addFontSizeRow();
    addPasswordRow();
}

ComponentListRow GuiExiaSettings::makeNavRow(const std::string& label,
                                             const std::string& value,
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

ComponentListRow GuiExiaSettings::makeNoteRow(const std::string& text)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(text, menuValueFont(),
                                                   mMenuColorSecondary, ALIGN_LEFT, ALIGN_CENTER,
                                                   glm::ivec2 {0, 0}),
                   true);
    return row;
}

// Section header for the merged SYSTEM INFO page. It is not a data row, so there is no arrow
// and no action, and the secondary color keeps it visually apart from the rows below it.
ComponentListRow GuiExiaSettings::makeSectionRow(const std::string& label)
{
    ComponentListRow row;
    row.addElement(std::make_shared<TextComponent>(label, getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                   mMenuColorSecondary),
                   true);
    return row;
}

void GuiExiaSettings::openBiosSwitch()
{
    auto biosMenu = new GuiSettings(_("Chinese Localization settings"));
    for (const BiosPlatform& platform : biosPlatforms()) {
        biosMenu->addRow(makeNavRow(platform.label, "", [this, platform] {
            openBiosPlatform(platform.label, platform.confKey, platform.target, platform.options);
        }));
    }
    mWindow->pushGui(biosMenu);
}

void GuiExiaSettings::openBiosPlatform(const std::string& title,
                                       const std::string& confKey,
                                       const std::string& target,
                                       const std::vector<std::pair<std::string, std::string>>&
                                           options)
{
    // Exia local fix (devdoc/15 §6.6): ~/biosalt and ~/bios live in the real user home, the
    // --home data directory must not be used for these paths.
    const std::string sourceRoot {Utils::FileSystem::getSystemHomeDirectory() + "/biosalt"};
    const std::string targetPath {Utils::FileSystem::getSystemHomeDirectory() + "/bios/" + target};

    auto platformMenu = new GuiSettings(title);
    auto selector = std::make_shared<OptionListComponent<std::string>>(title, false);

    bool detected {false};
    for (const std::pair<std::string, std::string>& option : options) {
        const bool isCurrent {filesIdentical(targetPath, sourceRoot + "/" + option.second)};
        selector->add(option.first, option.second, isCurrent);
        if (isCurrent)
            detected = true;
    }

    // When nothing matched, no placeholder entry is added (by design): the list then simply
    // has nothing selected and the save function below leaves the file system untouched until
    // the user picks an option. getSelectedId() falls back to 0 for rendering, so the widget
    // stays safe to draw and to navigate in that state.

    // Keep the record in exia.conf in sync with what is actually installed. The files win over
    // the configuration file, never the other way around.
    if (detected)
        ExiaConfig::getInstance().set(confKey, fileNameOf(selector->getSelected()));

    platformMenu->addWithLabel(title, selector);
    platformMenu->addSaveFunc([selector, targetPath, sourceRoot, confKey, window = mWindow] {
        // An empty selection means the installed file matched no candidate. Never guess here,
        // a hand placed file must not be overwritten by accident.
        const std::vector<std::string> selection {selector->getSelectedObjects()};
        if (selection.empty() || selection.front().empty())
            return;
        const std::string source {selection.front()};
        // copyFile() does not create parent directories, so make sure the target directory
        // exists before copying (e.g. ~/bios/np2kai for the PC-98 font).
        Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(targetPath));
        // copyFile() follows the same return convention as renameFile(): true on error,
        // false on success (see FileSystemUtil.cpp and GuiApplicationUpdater.cpp).
        if (Utils::FileSystem::copyFile(sourceRoot + "/" + source, targetPath, true)) {
            window->queueInfoPopup(_("FAILED TO COPY BIOS FILE, CHECK THE LOG FILE"), 6000);
            return;
        }
        // No backup is made before the copy, ~/biosalt is the backup and is never written to.
        ExiaConfig::getInstance().set(confKey, fileNameOf(source));
        window->queueInfoPopup(
            Utils::String::format(_("BIOS FILE COPIED TO %s"), targetPath.c_str()), 5000);
    });
    mWindow->pushGui(platformMenu);
}

void GuiExiaSettings::openSystemInfo()
{
    auto infoMenu = new GuiSettings(_("SYSTEM INFO"));

    // Build version row. The label is a raw untranslated key (same style as the exia.conf keys
    // below) so no new translatable string is introduced (devdoc/17 §3).
    infoMenu->addWithLabel(
        "es-de.version",
        std::make_shared<TextComponent>(std::string("ES-DE ") + PROGRAM_VERSION_STRING + " (r" +
                                            std::to_string(PROGRAM_RELEASE_NUMBER) + ") - " +
                                            EXIA_VERSION_STRING,
                                        menuValueFont(), mMenuColorPrimary, ALIGN_RIGHT));

    // Section 1, counted live from the ES-DE system list.
    infoMenu->addRow(makeSectionRow(_("SYSTEM STATISTICS")));
    unsigned int systemCount {0};
    unsigned int gameCount {0};
    unsigned int favoriteCount {0};
    for (SystemData* system : SystemData::sSystemVector) {
        if (system->isCollection())
            continue;
        ++systemCount;
        const std::pair<unsigned int, unsigned int> count {system->getDisplayedGameCount()};
        gameCount += count.first;
        favoriteCount += count.second;
    }
    infoMenu->addWithLabel(
        _("SYSTEMS"),
        std::make_shared<TextComponent>(std::to_string(systemCount), menuValueFont(),
                                        mMenuColorPrimary, ALIGN_RIGHT));
    infoMenu->addWithLabel(
        _("GAMES"),
        std::make_shared<TextComponent>(std::to_string(gameCount), menuValueFont(),
                                        mMenuColorPrimary, ALIGN_RIGHT));
    infoMenu->addWithLabel(
        _("FAVORITES"),
        std::make_shared<TextComponent>(std::to_string(favoriteCount), menuValueFont(),
                                        mMenuColorPrimary, ALIGN_RIGHT));

    // Section 2, read from the F section of exia.conf.
    infoMenu->addRow(makeSectionRow(_("ES-DE PATHS")));
    const std::vector<std::string> keys {"esde.home.parent", "esde.roms",  "esde.themes",
                                         "esde.media",        "esde.launch"};
    for (const std::string& key : keys) {
        infoMenu->addWithLabel(
            key, std::make_shared<TextComponent>(ExiaConfig::getInstance().get(key),
                                                 menuValueFont(), mMenuColorPrimary,
                                                 ALIGN_RIGHT));
    }

    // Section 3, asked of systemd each time the page is opened.
    ComponentListRow sessionRow;
    sessionRow.addElement(std::make_shared<TextComponent>(_("AUTO-START ES-DE ON BOOT"),
                                                          getMenuThemeFont(FONT_SIZE_MEDIUM),
                                                          mMenuColorPrimary),
                          true);
#if !defined(__ANDROID__) && !defined(__IOS__)
    const std::string sessionStatus {popenOutput(
        "systemctl is-enabled esde-default-session.service 2>/dev/null")};
    sessionRow.addElement(std::make_shared<TextComponent>(sessionStatus.empty() ?
                                                              _("UNKNOWN") :
                                                              sessionStatus,
                                                          menuValueFont(),
                                                          mMenuColorPrimary, ALIGN_RIGHT),
                          false);
#endif
    infoMenu->addRow(sessionRow);

    mWindow->pushGui(infoMenu);
}

void GuiExiaSettings::openGitProxy()
{
    auto proxyMenu = new GuiSettings(_("GIT PROXY"));

    const std::string currentProxy {popenOutput("git config --global --get http.proxy")};
    const std::string proxyUrl {
        ExiaConfig::getInstance().get("git.proxy.url", "http://127.0.0.1:20171")};

    auto proxyToggle = std::make_shared<OptionListComponent<std::string>>(_("GIT PROXY"), false);
    proxyToggle->add(_("ENABLED"), "on", !currentProxy.empty());
    proxyToggle->add(_("DISABLED"), "off", currentProxy.empty());
    proxyMenu->addWithLabel(_("GIT PROXY"), proxyToggle);

    proxyMenu->addSaveFunc([proxyToggle, proxyUrl, currentProxy, window = mWindow] {
        const bool enable {proxyToggle->getSelected() == "on"};
        int result {0};
        if (enable) {
            result = runProcess({"git", "config", "--global", "http.proxy", proxyUrl}, nullptr,
                                nullptr);
        }
        else if (!currentProxy.empty()) {
            // --unset fails when the key does not exist, so only call it when there is
            // something to remove.
            result = runProcess({"git", "config", "--global", "--unset", "http.proxy"}, nullptr,
                                nullptr);
        }
        ExiaConfig::getInstance().set("git.proxy.enabled", enable ? "1" : "0");
        if (result != 0)
            window->queueInfoPopup(_("FAILED TO RUN GIT, IS IT INSTALLED?"), 6000);
        else
            window->queueInfoPopup(enable ? _("GIT PROXY ENABLED") : _("GIT PROXY DISABLED"),
                                   4000);
    });

    proxyMenu->addRow(makeNavRow(_("GIT PROXY URL"), proxyUrl, [this, proxyUrl] {
        mWindow->pushGui(new GuiTextEditPopup(
            _("GIT PROXY URL"), proxyUrl,
            [this](const std::string& value) {
                const std::string url {trimString(value)};
                if (url.empty())
                    return;
                ExiaConfig::getInstance().set("git.proxy.url", url);
                if (!popenOutput("git config --global --get http.proxy").empty())
                    runProcess({"git", "config", "--global", "http.proxy", url}, nullptr, nullptr);
                mWindow->queueInfoPopup(_("GIT PROXY URL SAVED"), 4000);
            },
            false));
    }));

    mWindow->pushGui(proxyMenu);
}

void GuiExiaSettings::addFontSizeRow()
{
    // A drag bar operated with the direction keys, in the same style as the upstream volume
    // sliders. The step is 2%, so the 80 to 150 range is 36 stops and holding the key repeats.
    auto slider = std::make_shared<SliderComponent>(80.0f, 150.0f, 2.0f, "%");
    const int originalPercent {currentScalePercent()};
    slider->setValue(static_cast<float>(originalPercent));

    addWithLabel(_("MENU FONT SIZE (REQUIRES RESTART)"), slider);
    addSaveFunc([slider, originalPercent, window = mWindow] {
        const int percent {static_cast<int>(std::round(slider->getValue()))};
        // Only write the config and nag about the restart when the value really changed,
        // closing an untouched menu must stay silent.
        if (percent == originalPercent)
            return;
        ExiaConfig::getInstance().set("menu.font.scale",
                                      Utils::String::format("%.2f", percent / 100.0f));
        window->queueInfoPopup(_("FONT SIZE SAVED, RESTART ES-DE TO APPLY"), 6000);
    });
}

void GuiExiaSettings::addPasswordRow()
{
    // Value font and label font follow the row font spec from the devdoc, the value must use
    // the same small font as every other row value or this row sticks out visually.
    auto passwordText = std::make_shared<TextComponent>("", menuValueFont(),
                                                        mMenuColorPrimary, ALIGN_RIGHT);
    const std::string storedPassword {ExiaConfig::getInstance().get("security.sudo.password")};
    std::string masked;
    if (!storedPassword.empty()) {
        masked = "********";
        passwordText->setHiddenValue(storedPassword);
    }
    addEditableTextComponent(_("SUDO PASSWORD"), passwordText, masked, "", true,
                             getMenuThemeFont(FONT_SIZE_MEDIUM));
    passwordText->setSize(0.0f, passwordText->getFont()->getHeight());
    addSaveFunc([passwordText] {
        const std::string entered {passwordText->getHiddenValue()};
        // An emptied field keeps the previous password instead of storing an empty one.
        if (!entered.empty() &&
            entered != ExiaConfig::getInstance().get("security.sudo.password"))
            ExiaConfig::getInstance().set("security.sudo.password", entered);
    });
}
