//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  SystemDataSqlite.cpp
//
//  Reads the two sqlite system stores introduced in devdoc/54. The stores are
//  read-only at runtime; schema migrations are the only write performed here
//  (and only when a store uses an older schema).

#include "SystemDataSqlite.h"

#include "Log.h"
#include "resources/ResourceManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"

#include <pugixml.hpp>
#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr const char* SCHEMA_VERSION {"1"};

    std::map<std::string, std::map<std::string, std::string>> sIntros;
    bool sIntrosLoaded {false};

    std::map<std::string, std::map<std::string, std::string>> sLocalizedNames;
    bool sLocalizedNamesLoaded {false};

    std::string userDatabasePath()
    {
        return Utils::FileSystem::getAppDataDirectory() +
               "/custom_systems/es_systems_user.sqlite";
    }

    std::string builtinDatabasePath()
    {
#if defined(__linux__) && !defined(LINUX_AARCH64)
        return ResourceManager::getInstance().getResourcePath(
            ":/systems/linux/es_systems_builtin.sqlite", false);
#elif defined(LINUX_AARCH64)
        return ResourceManager::getInstance().getResourcePath(
            ":/systems/linuxarm/es_systems_builtin.sqlite", false);
#else
        return "";
#endif
    }

    std::string columnText(sqlite3_stmt* statement, const int column)
    {
        const unsigned char* text {sqlite3_column_text(statement, column)};
        return text == nullptr ? std::string {} :
                                 std::string {reinterpret_cast<const char*>(text)};
    }

    bool tableExists(sqlite3* database, const std::string& table)
    {
        sqlite3_stmt* statement {nullptr};
        if (sqlite3_prepare_v2(
                database,
                "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?",
                -1, &statement, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(statement, 1, table.c_str(), -1, SQLITE_TRANSIENT);
        const bool exists {sqlite3_step(statement) == SQLITE_ROW};
        sqlite3_finalize(statement);
        return exists;
    }

    bool columnExists(sqlite3* database, const std::string& table, const std::string& column)
    {
        sqlite3_stmt* statement {nullptr};
        const std::string query {"PRAGMA table_info(" + table + ")"};
        if (sqlite3_prepare_v2(database, query.c_str(), -1, &statement, nullptr) != SQLITE_OK)
            return false;
        bool exists {false};
        while (sqlite3_step(statement) == SQLITE_ROW) {
            if (columnText(statement, 1) == column) {
                exists = true;
                break;
            }
        }
        sqlite3_finalize(statement);
        return exists;
    }

    std::string schemaVersion(sqlite3* database)
    {
        if (!tableExists(database, "meta"))
            return "";
        sqlite3_stmt* statement {nullptr};
        if (sqlite3_prepare_v2(
                database, "SELECT value FROM meta WHERE key = 'schema_version'", -1,
                &statement, nullptr) != SQLITE_OK)
            return "";
        std::string version;
        if (sqlite3_step(statement) == SQLITE_ROW)
            version = columnText(statement, 0);
        sqlite3_finalize(statement);
        return version;
    }

    bool execSql(sqlite3* database, const std::string& sql)
    {
        char* error {nullptr};
        const bool ok {sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error) == SQLITE_OK};
        if (!ok && error != nullptr) {
            LOG(LogError) << "SystemDataSqlite: SQL error \"" << error << "\"";
            sqlite3_free(error);
        }
        return ok;
    }

    bool migrateDatabase(const std::string& path)
    {
        sqlite3* database {nullptr};
        if (sqlite3_open_v2(path.c_str(), &database,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX,
                            nullptr) != SQLITE_OK) {
            if (database != nullptr)
                sqlite3_close(database);
            return false;
        }
        sqlite3_busy_timeout(database, 5000);
        const bool coreTables {tableExists(database, "system") && tableExists(database, "command")};
        if (!coreTables) {
            sqlite3_close(database);
            return false;
        }

        if (!tableExists(database, "meta"))
            execSql(database, "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)");

        const std::map<std::string, std::string> systemColumns {
            {"fullname", "TEXT"},
            {"platform", "TEXT"},
            {"theme", "TEXT"},
            {"manufacturer", "TEXT"},
            {"release_date", "TEXT"},
            {"hardware_type", "TEXT"},
            {"freeplay", "INTEGER NOT NULL DEFAULT 0"},
            {"systemsortname", "TEXT"},
            {"origin", "TEXT NOT NULL DEFAULT 'user'"},
            {"ord", "INTEGER"},
        };
        for (const auto& column : systemColumns) {
            if (!columnExists(database, "system", column.first)) {
                execSql(database,
                        "ALTER TABLE system ADD COLUMN " + column.first + " " + column.second);
            }
        }
        if (!columnExists(database, "command", "label"))
            execSql(database, "ALTER TABLE command ADD COLUMN label TEXT");
        if (!columnExists(database, "command", "ord"))
            execSql(database, "ALTER TABLE command ADD COLUMN ord INTEGER");

        execSql(database,
                "CREATE TABLE IF NOT EXISTS system_intro ("
                "id INTEGER PRIMARY KEY, system_id INTEGER NOT NULL REFERENCES system(id) "
                "ON DELETE CASCADE, lang TEXT NOT NULL DEFAULT 'en', "
                "description TEXT NOT NULL, UNIQUE(system_id, lang))");
        execSql(database,
                "CREATE TABLE IF NOT EXISTS system_name_i18n ("
                "id INTEGER PRIMARY KEY, system_id INTEGER NOT NULL REFERENCES system(id) "
                "ON DELETE CASCADE, lang TEXT NOT NULL, name TEXT NOT NULL, "
                "UNIQUE(system_id, lang))");
        execSql(database,
                "INSERT INTO meta(key, value) VALUES('schema_version', '" +
                    std::string {SCHEMA_VERSION} +
                    "') ON CONFLICT(key) DO UPDATE SET value = excluded.value");

        sqlite3_close(database);
        LOG(LogInfo) << "SystemDataSqlite: migrated store \"" << path << "\"";
        return true;
    }

    bool schemaValid(sqlite3* database)
    {
        if (!tableExists(database, "system") || !tableExists(database, "command"))
            return false;
        for (const char* column : {"name", "path", "extension"}) {
            if (!columnExists(database, "system", column))
                return false;
        }
        if (!columnExists(database, "command", "system_id") ||
            !columnExists(database, "command", "cmd"))
            return false;
        return true;
    }

    void appendSystem(pugi::xml_node& systemList, sqlite3_stmt* commandQuery,
                      const std::string& name, const std::string& fullname,
                      const std::string& path, const std::string& extension,
                      const std::string& platform, const std::string& theme,
                      const std::string& manufacturer, const std::string& releaseDate,
                      const std::string& hardwareType, const int freeplay,
                      const std::string& sortName, const sqlite3_int64 systemId)
    {
        if (name.empty() || fullname.empty() || path.empty() || extension.empty()) {
            LOG(LogError) << "SystemDataSqlite: skipping system \"" << name
                          << "\" as a required field is missing";
            return;
        }

        sqlite3_reset(commandQuery);
        sqlite3_clear_bindings(commandQuery);
        sqlite3_bind_int64(commandQuery, 1, systemId);

        std::vector<std::pair<std::string, std::string>> commands;
        while (sqlite3_step(commandQuery) == SQLITE_ROW) {
            const std::string command {columnText(commandQuery, 1)};
            if (command.empty())
                continue;
            commands.emplace_back(columnText(commandQuery, 0), command);
        }
        if (commands.empty()) {
            LOG(LogError) << "SystemDataSqlite: skipping system \"" << name
                          << "\" as it has no launch command";
            return;
        }

        pugi::xml_node system {systemList.append_child("system")};
        system.append_child("name").text().set(name.c_str());
        system.append_child("fullname").text().set(fullname.c_str());
        if (!sortName.empty())
            system.append_child("systemsortname").text().set(sortName.c_str());
        system.append_child("path").text().set(path.c_str());
        system.append_child("extension").text().set(extension.c_str());
        for (const auto& command : commands) {
            pugi::xml_node entry {system.append_child("command")};
            if (!command.first.empty())
                entry.append_attribute("label").set_value(command.first.c_str());
            entry.text().set(command.second.c_str());
        }
        if (!platform.empty())
            system.append_child("platform").text().set(platform.c_str());
        if (!theme.empty())
            system.append_child("theme").text().set(theme.c_str());
        if (!manufacturer.empty())
            system.append_child("manufacturer").text().set(manufacturer.c_str());
        if (!releaseDate.empty())
            system.append_child("releaseDate").text().set(releaseDate.c_str());
        if (!hardwareType.empty())
            system.append_child("hardwareType").text().set(hardwareType.c_str());
        if (freeplay != 0)
            system.append_child("freeplay").text().set("true");
    }

    bool readDatabase(const std::string& path, pugi::xml_document& document, std::string& error)
    {
        sqlite3* database {nullptr};
        if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            error = "cannot open database";
            if (database != nullptr)
                sqlite3_close(database);
            return false;
        }
        sqlite3_busy_timeout(database, 5000);

        if (!schemaValid(database)) {
            sqlite3_close(database);
            if (!migrateDatabase(path)) {
                error = "schema is not usable";
                return false;
            }
            if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) !=
                SQLITE_OK) {
                error = "cannot reopen database";
                if (database != nullptr)
                    sqlite3_close(database);
                return false;
            }
            sqlite3_busy_timeout(database, 5000);
            if (!schemaValid(database)) {
                error = "schema is still not usable after migration";
                sqlite3_close(database);
                return false;
            }
        }

        const std::string version {schemaVersion(database)};
        if (version != SCHEMA_VERSION) {
            sqlite3_close(database);
            if (!migrateDatabase(path)) {
                error = "schema version mismatch and migration failed";
                return false;
            }
            if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) !=
                SQLITE_OK) {
                error = "cannot reopen database after version migration";
                if (database != nullptr)
                    sqlite3_close(database);
                return false;
            }
            sqlite3_busy_timeout(database, 5000);
        }

        const char* systemQuery =
            "SELECT id, name, fullname, path, extension, platform, theme, manufacturer, "
            "release_date, hardware_type, freeplay, systemsortname FROM system ORDER BY "
            "CASE WHEN ord IS NULL THEN 1 ELSE 0 END, ord, name";
        sqlite3_stmt* systemStatement {nullptr};
        if (sqlite3_prepare_v2(database, systemQuery, -1, &systemStatement, nullptr) != SQLITE_OK) {
            error = "cannot query system table";
            sqlite3_close(database);
            return false;
        }

        sqlite3_stmt* commandStatement {nullptr};
        if (sqlite3_prepare_v2(database,
                               "SELECT label, cmd FROM command WHERE system_id = ? ORDER BY "
                               "CASE WHEN ord IS NULL THEN 1 ELSE 0 END, ord, id",
                               -1, &commandStatement, nullptr) != SQLITE_OK) {
            error = "cannot query command table";
            sqlite3_finalize(systemStatement);
            sqlite3_close(database);
            return false;
        }

        document.reset();
        pugi::xml_node systemList {document.append_child("systemList")};
        while (sqlite3_step(systemStatement) == SQLITE_ROW) {
            appendSystem(systemList, commandStatement,
                         columnText(systemStatement, 1), columnText(systemStatement, 2),
                         columnText(systemStatement, 3), columnText(systemStatement, 4),
                         columnText(systemStatement, 5), columnText(systemStatement, 6),
                         columnText(systemStatement, 7), columnText(systemStatement, 8),
                         columnText(systemStatement, 9), sqlite3_column_int(systemStatement, 10),
                         columnText(systemStatement, 11), sqlite3_column_int64(systemStatement, 0));
        }
        sqlite3_finalize(commandStatement);
        sqlite3_finalize(systemStatement);
        sqlite3_close(database);
        return true;
    }

    std::string currentLanguage()
    {
        const std::string locale {Utils::Localization::sCurrentLocale};
        const std::string language {locale.substr(0, locale.find('_'))};
        if (language == "zh") {
            if (locale.find("TW") != std::string::npos ||
                locale.find("HK") != std::string::npos)
                return "tw";
            return "zh";
        }
        return language.empty() ? "en" : language;
    }

    void loadIntros(const std::string& path)
    {
        sIntros.clear();
        sIntrosLoaded = true;
        sqlite3* database {nullptr};
        if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (database != nullptr)
                sqlite3_close(database);
            return;
        }
        if (!tableExists(database, "system_intro")) {
            sqlite3_close(database);
            return;
        }
        sqlite3_stmt* statement {nullptr};
        if (sqlite3_prepare_v2(
                database,
                "SELECT s.name, i.lang, i.description FROM system_intro i "
                "JOIN system s ON s.id = i.system_id",
                -1, &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const std::string name {columnText(statement, 0)};
                const std::string lang {columnText(statement, 1)};
                const std::string description {columnText(statement, 2)};
                if (!name.empty() && !lang.empty() && !description.empty())
                    sIntros[name][lang] = description;
            }
            sqlite3_finalize(statement);
        }
        sqlite3_close(database);
    }

    bool ensureIntrosLoaded()
    {
        if (sIntrosLoaded)
            return true;
        const std::string userPath {userDatabasePath()};
        if (Utils::FileSystem::exists(userPath)) {
            loadIntros(userPath);
            return true;
        }
        const std::string builtinPath {builtinDatabasePath()};
        if (!builtinPath.empty() && Utils::FileSystem::exists(builtinPath)) {
            loadIntros(builtinPath);
            return true;
        }
        sIntrosLoaded = true;
        return true;
    }

    void loadLocalizedNames(const std::string& path)
    {
        sLocalizedNames.clear();
        sLocalizedNamesLoaded = true;
        sqlite3* database {nullptr};
        if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (database != nullptr)
                sqlite3_close(database);
            return;
        }
        if (!tableExists(database, "system_name_i18n")) {
            sqlite3_close(database);
            return;
        }
        sqlite3_stmt* statement {nullptr};
        if (sqlite3_prepare_v2(
                database,
                "SELECT s.name, n.lang, n.name FROM system_name_i18n n "
                "JOIN system s ON s.id = n.system_id",
                -1, &statement, nullptr) == SQLITE_OK) {
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const std::string name {columnText(statement, 0)};
                const std::string lang {columnText(statement, 1)};
                const std::string localizedName {columnText(statement, 2)};
                if (!name.empty() && !lang.empty() && !localizedName.empty())
                    sLocalizedNames[name][lang] = localizedName;
            }
            sqlite3_finalize(statement);
        }
        sqlite3_close(database);
    }

    bool ensureLocalizedNamesLoaded()
    {
        if (sLocalizedNamesLoaded)
            return true;
        const std::string userPath {userDatabasePath()};
        if (Utils::FileSystem::exists(userPath)) {
            loadLocalizedNames(userPath);
            return true;
        }
        const std::string builtinPath {builtinDatabasePath()};
        if (!builtinPath.empty() && Utils::FileSystem::exists(builtinPath)) {
            loadLocalizedNames(builtinPath);
            return true;
        }
        sLocalizedNamesLoaded = true;
        return true;
    }
} // namespace

namespace SystemDataSqlite
{
    bool loadSystems(pugi::xml_document& document, std::string& source)
    {
        const std::string userPath {userDatabasePath()};
        if (Utils::FileSystem::exists(userPath)) {
            std::string error;
            if (readDatabase(userPath, document, error)) {
                LOG(LogInfo) << "Loading systems from user store \"" << userPath << "\"";
                loadIntros(userPath);
                source = userPath;
                return true;
            }
            LOG(LogError) << "SystemDataSqlite: user store \"" << userPath
                          << "\" is unusable (" << error << "), falling back";
        }

        const std::string builtinPath {builtinDatabasePath()};
        if (!builtinPath.empty() && Utils::FileSystem::exists(builtinPath)) {
            std::string error;
            if (readDatabase(builtinPath, document, error)) {
                LOG(LogInfo) << "Loading systems from builtin store \"" << builtinPath << "\"";
                loadIntros(builtinPath);
                source = builtinPath;
                return true;
            }
            LOG(LogError) << "SystemDataSqlite: builtin store \"" << builtinPath
                          << "\" is unusable (" << error << "), falling back to XML";
        }
        return false;
    }

    std::string getIntro(const std::string& systemName)
    {
        ensureIntrosLoaded();
        const auto system {sIntros.find(systemName)};
        if (system == sIntros.end())
            return "";
        const std::string language {currentLanguage()};
        const auto localized {system->second.find(language)};
        if (localized != system->second.end())
            return localized->second;
        const auto english {system->second.find("en")};
        return english == system->second.end() ? "" : english->second;
    }

    std::string getLocalizedFullName(const std::string& systemName,
                                      const std::string& defaultFullName)
    {
        ensureLocalizedNamesLoaded();
        const auto system {sLocalizedNames.find(systemName)};
        if (system == sLocalizedNames.end())
            return defaultFullName;
        const std::string language {currentLanguage()};
        const auto localized {system->second.find(language)};
        if (localized != system->second.end())
            return localized->second;
        const auto english {system->second.find("en")};
        return english == system->second.end() ? defaultFullName : english->second;
    }
} // namespace SystemDataSqlite
