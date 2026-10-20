//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GamelistStore.cpp
//
//  Reads and writes the sqlite based metadata stores which have replaced the
//  gamelist.xml files. See devdoc/38 for the schema and the behavior rules.
//
//  The stores are created lazily: a system only gets a store file the first time
//  something needs to be persisted for it, and a missing or unreadable store is
//  treated as "this system has no metadata", so scanning and startup are never
//  blocked by store problems.
//

#include "GamelistStore.h"

#include "FileData.h"
#include "Log.h"
#include "MetaData.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include "utils/LocalizationUtil.h"
#include "utils/StringUtil.h"

#include <pugixml.hpp>
#include <sqlite3.h>

#include <map>
#include <vector>

namespace GamelistStore
{
    namespace
    {
        // Bump when the schema changes in a way that needs to be detected on load.
        const char* schemaVersion {"1"};

        // Private per game metadata keys. MetaDataList is a plain key value map and only
        // keys from the metadata declaration are persisted, so these two stay in memory:
        // they remember which name belongs to the store (the untranslated one) and which
        // name the language aware overlay put in place for display, see devdoc/38 §7.
        const char* nameOriginalKey {"name_original"};
        const char* nameLocalizedKey {"name_localized"};

        // ------------------------------------------------------------------
        // sqlite helpers
        // ------------------------------------------------------------------

        class Connection
        {
        public:
            Connection(const std::string& path, bool readOnly)
            {
                const int flags {readOnly ? SQLITE_OPEN_READONLY
                                          : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)};
                if (sqlite3_open_v2(path.c_str(), &mHandle, flags, nullptr) != SQLITE_OK) {
                    LOG(LogError) << "GamelistStore: Couldn't open store \"" << path
                                  << "\": " << (mHandle ? sqlite3_errmsg(mHandle) : "no such file");
                    if (mHandle != nullptr) {
                        sqlite3_close(mHandle);
                        mHandle = nullptr;
                    }
                    return;
                }
                if (!readOnly) {
                    // WAL is safe on the btrfs system disk and keeps single row updates cheap.
                    sqlite3_exec(mHandle, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
                    sqlite3_exec(mHandle, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
                }
            }

            ~Connection()
            {
                if (mHandle != nullptr)
                    sqlite3_close(mHandle);
            }

            bool isValid() const { return mHandle != nullptr; }
            sqlite3* get() const { return mHandle; }

        private:
            sqlite3* mHandle {nullptr};
        };

        class Statement
        {
        public:
            Statement(sqlite3* handle, const std::string& sql)
            {
                if (sqlite3_prepare_v2(handle, sql.c_str(), -1, &mStatement, nullptr) != SQLITE_OK) {
                    LOG(LogError) << "GamelistStore: SQL statement failed: \"" << sql
                                  << "\": " << sqlite3_errmsg(handle);
                    mStatement = nullptr;
                }
            }

            ~Statement()
            {
                if (mStatement != nullptr)
                    sqlite3_finalize(mStatement);
            }

            bool isValid() const { return mStatement != nullptr; }
            sqlite3_stmt* get() const { return mStatement; }

            void bind(const int index, const std::string& value)
            {
                sqlite3_bind_text(mStatement, index, value.c_str(), static_cast<int>(value.size()),
                                  SQLITE_TRANSIENT);
            }

            bool step() { return sqlite3_step(mStatement) == SQLITE_ROW; }
            bool stepDone() { return sqlite3_step(mStatement) == SQLITE_DONE; }

            std::string column(const int index) const
            {
                const unsigned char* value {sqlite3_column_text(mStatement, index)};
                return value != nullptr ? reinterpret_cast<const char*>(value) : std::string();
            }

        private:
            sqlite3_stmt* mStatement {nullptr};
        };

        bool execSql(sqlite3* handle, const std::string& sql, const std::string& context)
        {
            char* error {nullptr};
            if (sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
                LOG(LogError) << "GamelistStore: " << context << ": " << (error ? error : sql);
                sqlite3_free(error);
                return false;
            }
            return true;
        }

        // Creates the tables described in devdoc/38 section 5 and makes sure the
        // schema version is present.
        bool createSchema(sqlite3* handle)
        {
            if (!execSql(handle,
                         "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)",
                         "Couldn't create the meta table") ||
                !execSql(handle,
                         "CREATE TABLE IF NOT EXISTS game_name_i18n (path TEXT, locale TEXT, "
                         "name TEXT, PRIMARY KEY (path, locale))",
                         "Couldn't create the game_name_i18n table"))
                return false;

            for (const MetaDataListType type : {GAME_METADATA, FOLDER_METADATA}) {
                std::string sql {"CREATE TABLE IF NOT EXISTS "};
                sql += (type == GAME_METADATA ? "game" : "folder");
                sql += " (path TEXT PRIMARY KEY";
                for (const auto& decl : getMDDByType(type))
                    sql += ", " + decl.key + " TEXT";
                sql += ")";
                if (!execSql(handle, sql, "Couldn't create the metadata table"))
                    return false;
            }

            Statement statement {handle,
                                 "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"};
            if (!statement.isValid())
                return false;
            statement.bind(1, "schema_version");
            statement.bind(2, schemaVersion);
            return statement.stepDone();
        }

        // Reads an entire table, keyed by column name so unknown or reordered
        // columns cannot corrupt the mapping.
        bool readTable(sqlite3* handle,
                       const std::string& table,
                       std::vector<std::map<std::string, std::string>>& rows)
        {
            Statement statement {handle, "SELECT * FROM " + table};
            if (!statement.isValid())
                return false;

            const int columnCount {sqlite3_column_count(statement.get())};
            while (statement.step()) {
                std::map<std::string, std::string> row;
                for (int i {0}; i < columnCount; ++i)
                    row[sqlite3_column_name(statement.get(), i)] = statement.column(i);
                rows.push_back(row);
            }
            return true;
        }

        std::map<std::string, std::string> readMeta(sqlite3* handle)
        {
            std::map<std::string, std::string> meta;
            std::vector<std::map<std::string, std::string>> rows;
            if (readTable(handle, "meta", rows)) {
                for (const auto& row : rows) {
                    if (row.count("key") && row.count("value") && row.at("key") != "schema_version")
                        meta[row.at("key")] = row.at("value");
                }
            }
            return meta;
        }

        bool tableExists(sqlite3* handle, const std::string& table)
        {
            Statement statement {handle,
                                 "SELECT name FROM sqlite_master WHERE type = 'table' AND name = ?"};
            if (!statement.isValid())
                return false;
            statement.bind(1, table);
            return statement.step();
        }

        // Reads all translated names for the active locale, keyed by the relative path
        // exactly as it is stored in the game table.
        std::map<std::string, std::string> readLocalizedNames(sqlite3* handle)
        {
            std::map<std::string, std::string> names;

            const std::string& locale {Utils::Localization::sCurrentLocale};
            if (locale.empty() || !tableExists(handle, "game_name_i18n"))
                return names;

            Statement statement {handle, "SELECT path, name FROM game_name_i18n WHERE locale = ?"};
            if (!statement.isValid())
                return names;
            statement.bind(1, locale);

            while (statement.step()) {
                const std::string path {statement.column(0)};
                if (!path.empty())
                    names[path] = statement.column(1);
            }
            return names;
        }

        // True when the name in memory is neither the store value nor the translated value,
        // i.e. when the user or the scraper has actually renamed the game.
        bool wasRenamed(const FileData* file)
        {
            const std::string& original {file->metadata.get(nameOriginalKey)};
            if (original.empty())
                return false;

            const std::string& current {file->metadata.get("name")};
            if (current == original)
                return false;

            return current != file->metadata.get(nameLocalizedKey);
        }

        // The name column always holds the untranslated name so switching the application
        // language can never lose a translation, and never writes one by accident.
        std::string storedName(const FileData* file)
        {
            if (wasRenamed(file))
                return file->metadata.get("name");

            const std::string& original {file->metadata.get(nameOriginalKey)};
            return original.empty() ? file->metadata.get("name") : original;
        }

        std::string insertSql(const std::string& table, const std::vector<MetaDataDecl>& mdd)
        {
            std::string sql {"INSERT OR REPLACE INTO " + table + " (path"};
            std::string values {" VALUES (?"};

            for (const auto& decl : mdd) {
                sql += ", " + decl.key;
                values += ", ?";
            }

            return sql + ")" + values + ")";
        }

        // Mirrors MetaDataList::createFromXML(): absent and empty values fall back to
        // the default, which is exactly how the gamelist.xml files behaved.
        MetaDataList createFromStore(const MetaDataListType type,
                                     const std::map<std::string, std::string>& row)
        {
            MetaDataList metadata {type};
            for (const auto& decl : getMDDByType(type)) {
                const auto entry {row.find(decl.key)};
                if (entry != row.cend() && !entry->second.empty())
                    metadata.set(decl.key, entry->second);
                else
                    metadata.set(decl.key, decl.defaultValue);
            }
            return metadata;
        }

        // ------------------------------------------------------------------
        // File tree helpers
        // ------------------------------------------------------------------

        FileData* findOrCreateFile(SystemData* system, const std::string& path, FileType type)
        {
            // First, verify that path is within the system's root folder.
            FileData* root {system->getRootFolder()};
            bool contains {false};
            std::string relative {Utils::FileSystem::removeCommonPath(path, root->getPath(), contains)};

            if (!contains) {
                LOG(LogError) << "Path \"" << path << "\" is outside system path \""
                              << system->getStartPath() << "\"";
                return nullptr;
            }

            const Utils::FileSystem::StringList& pathList {Utils::FileSystem::getPathList(relative)};
            auto path_it = pathList.begin();
            FileData* treeNode {root};
            bool found {false};

            while (path_it != pathList.end()) {
                // Workaround for an extremely rare issue that can basically only happen if a dot (.)
                // has been defined as a valid extension for the system (meaning extensionless files
                // are loaded), in combination with the "Only show ROMs from the metadata store"
                // option being enabled and a stale entry being present in the store that perfectly
                // matches a folder which is actually in use. The workaround is not a perfect solution
                // but it at least prevents the application from crashing.
                if (treeNode->getType() != FOLDER) {
                    LOG(LogWarning)
                        << "Invalid store entry caused by folder having the same name as a stale "
                           "extensionless game file (this may cause undefined behavior):";
                    return nullptr;
                }

                const std::unordered_map<std::string, FileData*>& children {
                    treeNode->getChildrenByFilename()};

                const std::string key {*path_it};
                found = children.find(key) != children.cend();
                if (found)
                    treeNode = children.at(key);

                if (treeNode->getNoLoad())
                    return treeNode;

                // This is the end.
                if (path_it == --pathList.end()) {
                    if (found)
                        return treeNode;

                    if (type == FOLDER) {
                        if (!Utils::FileSystem::exists(path + "/noload.txt")) {
                            LOG(LogWarning) << "A folder defined in the metadata store does not "
                                               "exist or contains no valid games: \""
                                            << path << "\"";
                        }
                        return nullptr;
                    }

                    // Handle the special situation where a file exists and has an entry in the
                    // metadata store but the file extension is not configured in es_systems.xml.
                    const std::vector<std::string>& extensions {
                        system->getSystemEnvData()->mSearchExtensions};

                    if (std::find(extensions.cbegin(), extensions.cend(),
                                  Utils::FileSystem::getExtension(path)) == extensions.cend()) {
#if defined(_WIN64)
                        LOG(LogWarning) << "File \"" << Utils::String::replace(path, "/", "\\")
#else
                        LOG(LogWarning) << "File \"" << path
#endif
                                        << "\" is present in the metadata store but the extension "
                                           "is not configured in es_systems.xml";
                        // In case there are no entries left in the folder.
                        if (treeNode != system->getRootFolder() && treeNode->getChildren().empty())
                            delete treeNode;
                        return nullptr;
                    }

                    FileData* file {new FileData(type, path, system->getSystemEnvData(), system)};

                    // Skipping arcade assets from the metadata store.
                    if (!file->isArcadeAsset())
                        treeNode->addChild(file);
                    return file;
                }

                if (!found) {
                    // Don't create folders unless they're including any games.
                    // If the type is FOLDER it's going to be empty, so don't bother.
                    if (type == FOLDER) {
                        LOG(LogWarning) << "A folder defined in the metadata store does not exist "
                                           "or contains no valid games: \""
                                        << path << "\"";
                        return nullptr;
                    }

                    if (!system->getFlattenFolders()) {
                        // Create missing folder.
                        FileData* folder {new FileData(FOLDER, treeNode->getPath() + "/" + *path_it,
                                                       system->getSystemEnvData(), system)};
                        treeNode->addChild(folder);
                        treeNode = folder;
                    }
                }

                ++path_it;
            }

            return nullptr;
        }

        void removeOrphanedNoLoadEntries(SystemData* system)
        {
            // Get rid of any orphaned noload.txt folder entries.
            for (auto child : system->getRootFolder()->getChildrenRecursive()) {
                if (child->getNoLoad())
                    delete child;
            }
        }

        // Applies one store row to the file tree, using the same rules as the
        // gamelist.xml parser did.
        void applyEntry(SystemData* system,
                        const std::map<std::string, std::string>& row,
                        FileType type,
                        const std::map<std::string, std::string>& localizedNames)
        {
            const auto pathEntry {row.find("path")};
            if (pathEntry == row.cend() || pathEntry->second.empty()) {
                LOG(LogError) << "Metadata store entry contains no path, skipping entry";
                return;
            }

            const std::string& relativeTo {system->getStartPath()};
            const std::string path {
                Utils::FileSystem::resolveRelativePath(pathEntry->second, relativeTo, false)};
            const bool trustGamelist {Settings::getInstance()->getBool("ParseGamelistOnly")};
            const bool showHiddenFiles {Settings::getInstance()->getBool("ShowHiddenFiles")};

            if (!trustGamelist && !Utils::FileSystem::exists(path)) {
#if defined(_WIN64)
                LOG(LogWarning) << (type == GAME ? "File \"" : "Folder \"")
                                << Utils::String::replace(path, "/", "\\")
#else
                LOG(LogWarning) << (type == GAME ? "File \"" : "Folder \"") << path
#endif
                                << "\" does not exist, skipping entry";
                return;
            }

            // Skip hidden files, check both the file itself and the directory in which
            // it is located.
            if (!showHiddenFiles &&
                (Utils::FileSystem::isHidden(path) ||
                 Utils::FileSystem::isHidden(Utils::FileSystem::getParent(path)))) {
                LOG(LogDebug) << "GamelistStore::load(): Skipping hidden file \"" << path << "\"";
                return;
            }

            FileData* file {findOrCreateFile(system, path, type)};

            if (file != nullptr && file->getNoLoad())
                return;

            // Don't load entries with the wrong type. This should very rarely (if ever) happen.
            if (file != nullptr && ((type == GAME && file->getType() == FOLDER) ||
                                    (type == FOLDER && file->getType() == GAME))) {
                LOG(LogWarning) << "Game/folder mismatch for \"" << path << "\", skipping entry";
                return;
            }

            if (!file) {
                if (!Utils::FileSystem::exists(path + "/noload.txt")) {
#if defined(_WIN64)
                    LOG(LogWarning) << "Couldn't process \"" << Utils::String::replace(path, "/", "\\")
                                    << "\", skipping entry";
#else
                    LOG(LogWarning) << "Couldn't process \"" << path << "\", skipping entry";
#endif
                }
                return;
            }

            if (file->isArcadeAsset()) {
                // Skip arcade asset entries as these will not be used in any way inside
                // the application.
                LOG(LogDebug) << "GamelistStore::load(): Skipping arcade asset \""
                              << file->getName() << "\"";
                delete file;
                return;
            }

            const std::string defaultName {file->metadata.get("name")};
            file->metadata = createFromStore(type == FOLDER ? FOLDER_METADATA : GAME_METADATA, row);

            // Make sure a name gets set if one doesn't exist.
            if (file->metadata.get("name").empty())
                file->metadata.set("name", defaultName);

            const std::string originalName {file->metadata.get("name")};

            // Language aware display name. The untranslated name stays in the store and is
            // remembered in memory so a later save cannot overwrite a translation, see
            // devdoc/38 section 7.
            const auto localized {localizedNames.find(pathEntry->second)};
            if (localized != localizedNames.cend() && !localized->second.empty() &&
                localized->second != originalName) {
                file->metadata.set(nameLocalizedKey, localized->second);
                file->metadata.set("name", localized->second);
            }
            file->metadata.set(nameOriginalKey, originalName);

            file->metadata.resetChangedFlag();

            // If the game is flagged as hidden and the option has not been set to show hidden
            // games, then delete the entry. This leaves no trace of the entry at all in ES
            // but that is fine as the option to show hidden files is defined as requiring an
            // application restart.
            if (!Settings::getInstance()->getBool("ShowHiddenGames")) {
                if (file->getHidden()) {
                    LOG(LogDebug) << "GamelistStore::load(): Skipping hidden "
                                  << (type == GAME ? "file" : "folder") << " entry \""
                                  << file->getName() << "\"" << " (\"" << file->getPath() << "\")";
                    FileData* parent {file->getParent()};
                    delete file;
                    // In case there are no entries left in the folder.
                    if (parent != system->getRootFolder() && parent->getChildren().empty())
                        delete parent;
                }
                // Also delete any folders which are empty, i.e. all their entries are hidden.
                else if (file->getType() == FOLDER && file->getChildren().size() == 0) {
                    delete file;
                }
            }
        }

        // Reads the system level values, i.e. the alternative emulator and the launch on
        // other screen flag, from the store meta table.
        void applySystemMetaData(SystemData* system,
                                 const std::map<std::string, std::string>& meta)
        {
            const auto alternativeEmulator {meta.find("alternativeEmulator")};
            if (alternativeEmulator != meta.cend() && !alternativeEmulator->second.empty()) {
                bool validLabel {false};
                for (auto& command : system->getSystemEnvData()->mLaunchCommands) {
                    if (command.second == alternativeEmulator->second)
                        validLabel = true;
                }
                if (validLabel) {
                    system->setAlternativeEmulator(alternativeEmulator->second);
                    LOG(LogDebug) << "GamelistStore::load(): System \"" << system->getName()
                                  << "\" has a valid alternativeEmulator entry: \""
                                  << alternativeEmulator->second << "\"";
                }
                else {
                    system->setAlternativeEmulator("<INVALID>" + alternativeEmulator->second);
                    LOG(LogWarning) << "System \"" << system->getName()
                                    << "\" has an invalid alternativeEmulator entry that does not "
                                       "match any command tag in es_systems.xml: \""
                                    << alternativeEmulator->second << "\"";
                }
            }

            const auto launchOnOtherScreen {meta.find("launchOnOtherScreen")};
            if (launchOnOtherScreen != meta.cend()) {
#if defined(__ANDROID__)
                if (Utils::String::toLower(launchOnOtherScreen->second) == "false")
                    system->setLaunchOnOtherScreen(false);
                else
                    system->setLaunchOnOtherScreen(true);
#endif
            }
        }

        std::string storedPath(const FileData* file, const SystemData* system)
        {
            // Same relative path convention as the gamelist.xml files used, so the
            // stores stay comparable with the xml files left in the ROM directories.
            return Utils::FileSystem::createRelativePath(file->getPath(), system->getStartPath(),
                                                         false);
        }

        bool writeEntry(sqlite3* handle,
                        const FileData* file,
                        const SystemData* system,
                        const std::string& sql)
        {
            Statement statement {handle, sql};
            if (!statement.isValid())
                return false;

            statement.bind(1, storedPath(file, system));

            const std::vector<MetaDataDecl>& mdd {file->metadata.getMDD()};
            for (size_t i {0}; i < mdd.size(); ++i) {
                const std::string& key {mdd.at(i).key};
                statement.bind(static_cast<int>(i) + 2,
                               key == "name" ? storedName(file) : file->metadata.get(key));
            }

            return statement.stepDone();
        }

        bool deleteEntry(sqlite3* handle, const FileData* file, const SystemData* system)
        {
            Statement statement {handle, "DELETE FROM " +
                                             std::string(file->getType() == FOLDER ? "folder" : "game") +
                                             " WHERE path = ?"};
            if (!statement.isValid())
                return false;
            statement.bind(1, storedPath(file, system));
            return statement.stepDone();
        }

        // Drops the translation for the active locale once a game has been renamed, the new
        // name is the one which should be displayed from then on.
        bool deleteLocalizedName(sqlite3* handle, const FileData* file, const SystemData* system)
        {
            const std::string& locale {Utils::Localization::sCurrentLocale};
            if (locale.empty() || !tableExists(handle, "game_name_i18n"))
                return true;

            Statement statement {handle,
                                 "DELETE FROM game_name_i18n WHERE path = ? AND locale = ?"};
            if (!statement.isValid())
                return false;
            statement.bind(1, storedPath(file, system));
            statement.bind(2, locale);
            return statement.stepDone();
        }

    } // namespace

    // ------------------------------------------------------------------
    // Store paths
    // ------------------------------------------------------------------

    std::string getLibraryRoot()
    {
        // Hardcoded on purpose, the stores are not supposed to follow --home (the ES-DE
        // data directory is a different thing entirely), see devdoc/38 section 4.2.
        // getHomePath() returns the --home value so it must not be used here, the HOME
        // environment variable is what the stores are anchored to, exactly like exia.conf.
        std::string home {Utils::FileSystem::getSystemHomeDirectory()};
        if (home.empty())
            home = Utils::FileSystem::getHomePath();
        return home + "/.config/gamelists";
    }

    std::string getPlatformStorePath(const std::string& systemName)
    {
        return getLibraryRoot() + "/platforms/" + systemName + ".sqlite";
    }

    bool platformStoreExists(const std::string& systemName)
    {
        return Utils::FileSystem::exists(getPlatformStorePath(systemName));
    }

    std::string getCollectionStoreFolder()
    {
        return getLibraryRoot() + "/collections";
    }

    std::string getCollectionStorePath(const std::string& collectionName)
    {
        return getCollectionStoreFolder() + "/" + collectionName + ".sqlite";
    }

    // Returns the stored member paths of a custom collection, in the same form
    // they were saved in (i.e. with the %ROMPATH% prefix for paths below the ROM
    // directory). An empty list is returned when the collection has no store yet.
    std::vector<std::string> loadCollectionMembers(const std::string& collectionName)
    {
        std::vector<std::string> members;
        const std::string storePath {getCollectionStorePath(collectionName)};
        if (!Utils::FileSystem::exists(storePath))
            return members;

        Connection connection {storePath, true};
        if (!connection.isValid())
            return members;

        Statement statement {connection.get(), "SELECT path FROM member ORDER BY rowid"};
        if (!statement.isValid())
            return members;

        while (statement.step())
            members.push_back(statement.column(0));
        return members;
    }

    // Replaces the member list of a custom collection, creating the store if it
    // does not exist yet. The members are stored exactly as passed in, see
    // devdoc/38 section 9 for the schema.
    bool saveCollectionMembers(const std::string& collectionName,
                               const std::vector<std::string>& members)
    {
        const std::string storePath {getCollectionStorePath(collectionName)};
        if (!Utils::FileSystem::exists(storePath))
            Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(storePath));

        Connection connection {storePath, false};
        if (!connection.isValid()) {
            LOG(LogError) << "GamelistStore::saveCollectionMembers(): Couldn't write the "
                             "collection store for \""
                          << collectionName << "\"";
            return false;
        }

        if (!execSql(connection.get(),
                     "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)",
                     "Couldn't create the meta table") ||
            !execSql(connection.get(),
                     "CREATE TABLE IF NOT EXISTS member (path TEXT PRIMARY KEY)",
                     "Couldn't create the member table"))
            return false;

        if (!execSql(connection.get(), "BEGIN", "Couldn't start the store transaction"))
            return false;

        bool success {execSql(connection.get(), "DELETE FROM member",
                              "Couldn't clear the member table")};

        if (success) {
            for (const std::string& member : members) {
                Statement statement {connection.get(),
                                     "INSERT OR REPLACE INTO member (path) VALUES (?)"};
                if (!statement.isValid()) {
                    success = false;
                    break;
                }
                statement.bind(1, member);
                if (!statement.stepDone()) {
                    success = false;
                    break;
                }
            }
        }

        if (success) {
            const std::vector<std::pair<std::string, std::string>> meta {
                {"schema_version", schemaVersion}, {"collection_name", collectionName}};
            for (const auto& entry : meta) {
                Statement statement {connection.get(),
                                     "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"};
                if (!statement.isValid()) {
                    success = false;
                    break;
                }
                statement.bind(1, entry.first);
                statement.bind(2, entry.second);
                if (!statement.stepDone()) {
                    success = false;
                    break;
                }
            }
        }

        if (!execSql(connection.get(), success ? "COMMIT" : "ROLLBACK",
                     "Couldn't finalize the store transaction"))
            success = false;

        if (!success)
            LOG(LogError) << "GamelistStore::saveCollectionMembers(): Couldn't write the "
                             "collection store for \""
                          << collectionName << "\"";
        return success;
    }

    // ------------------------------------------------------------------
    // Loading
    // ------------------------------------------------------------------

    void load(SystemData* system)
    {
        std::string storePath {getPlatformStorePath(system->getName())};

        if (!Utils::FileSystem::exists(storePath)) {
            // First time this system is loaded, import a gamelist.xml file if there is
            // one. The xml file is kept as is, ES-DE never writes to it again.
            importXml(system);

            if (!Utils::FileSystem::exists(storePath)) {
                LOG(LogDebug) << "GamelistStore::load(): System \"" << system->getName()
                              << "\" does not have a metadata store";
                removeOrphanedNoLoadEntries(system);
                return;
            }
        }

        Connection connection {storePath, true};
        if (!connection.isValid()) {
            LOG(LogError) << "GamelistStore::load(): Using no metadata for system \""
                          << system->getName() << "\"";
            removeOrphanedNoLoadEntries(system);
            return;
        }

        LOG(LogInfo) << "Reading metadata store \"" << storePath << "\"...";

        std::vector<std::map<std::string, std::string>> gameRows;
        std::vector<std::map<std::string, std::string>> folderRows;

        if (!readTable(connection.get(), "game", gameRows) ||
            !readTable(connection.get(), "folder", folderRows)) {
            LOG(LogError) << "GamelistStore: Using no metadata for system \"" << system->getName()
                          << "\", the store could not be read";
            removeOrphanedNoLoadEntries(system);
            return;
        }

        applySystemMetaData(system, readMeta(connection.get()));

        const std::map<std::string, std::string> localizedNames {readLocalizedNames(connection.get())};

        for (const auto& row : gameRows)
            applyEntry(system, row, GAME, localizedNames);
        for (const auto& row : folderRows)
            applyEntry(system, row, FOLDER, localizedNames);

        removeOrphanedNoLoadEntries(system);
    }

    // ------------------------------------------------------------------
    // Saving
    // ------------------------------------------------------------------

    void save(SystemData* system, const bool updateAlternativeEmulator,
              const bool updateScreenLaunch)
    {
        if (Settings::getInstance()->getBool("IgnoreGamelist"))
            return;

        FileData* rootFolder {system->getRootFolder()};
        if (rootFolder == nullptr) {
            LOG(LogError) << "Found no root folder for system \"" << system->getName() << "\"";
            return;
        }

        // Collect the entries that need to be persisted. A store is never created for a
        // system which has nothing to save, see devdoc/38 section 6.1.
        std::vector<FileData*> changedFiles;
        for (FileData* file : rootFolder->getFilesRecursive(GAME | FOLDER)) {
            if (file->metadata.wasChanged() || file->getDeletionFlag())
                changedFiles.push_back(file);
        }

        std::map<std::string, std::string> desiredMeta;
        if (updateAlternativeEmulator) {
            const std::string& label {system->getAlternativeEmulator()};
            // Labels prefixed with <INVALID> were not resolvable when the store was read,
            // they must never be written back or they would grow on every restart.
            if (!label.empty() && label.find("<INVALID>") != 0)
                desiredMeta["alternativeEmulator"] = label;
        }
        if (updateScreenLaunch && !system->getLaunchOnOtherScreen())
            desiredMeta["launchOnOtherScreen"] = "false";

        const std::string storePath {getPlatformStorePath(system->getName())};
        const bool storeExists {Utils::FileSystem::exists(storePath)};

        std::map<std::string, std::string> storedMeta;
        if (storeExists) {
            Connection existing {storePath, true};
            if (existing.isValid())
                storedMeta = readMeta(existing.get());
        }

        // Only the values covered by the flags are compared, anything else in the meta
        // table belongs to another code path and must be left alone.
        auto covered = [](const std::map<std::string, std::string>& values,
                          const bool withAlternativeEmulator, const bool withScreenLaunch) {
            std::map<std::string, std::string> result;
            if (withAlternativeEmulator && values.count("alternativeEmulator"))
                result["alternativeEmulator"] = values.at("alternativeEmulator");
            if (withScreenLaunch && values.count("launchOnOtherScreen"))
                result["launchOnOtherScreen"] = values.at("launchOnOtherScreen");
            return result;
        };

        const bool metaChanged {covered(desiredMeta, updateAlternativeEmulator,
                                        updateScreenLaunch) !=
                                covered(storedMeta, updateAlternativeEmulator,
                                        updateScreenLaunch)};

        if (changedFiles.empty() && !metaChanged)
            return;

        if (!storeExists)
            Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(storePath));

        Connection connection {storePath, false};
        if (!connection.isValid()) {
            LOG(LogError) << "GamelistStore::save(): Couldn't write metadata for system \""
                          << system->getName() << "\"";
            return;
        }

        if (!createSchema(connection.get())) {
            LOG(LogError) << "GamelistStore::save(): Couldn't prepare the store for system \""
                          << system->getName() << "\"";
            return;
        }

        if (!execSql(connection.get(), "BEGIN", "Couldn't start the store transaction"))
            return;

        std::vector<FileData*> writtenFiles;
        bool success {true};

        for (const auto& entry : desiredMeta) {
            Statement statement {connection.get(),
                                 storedMeta.count(entry.first)
                                     ? "UPDATE meta SET value = ? WHERE key = ?"
                                     : "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"};
            if (!statement.isValid()) {
                success = false;
                break;
            }
            if (storedMeta.count(entry.first)) {
                statement.bind(1, entry.second);
                statement.bind(2, entry.first);
            }
            else {
                statement.bind(1, entry.first);
                statement.bind(2, entry.second);
            }
            if (!statement.stepDone()) {
                success = false;
                break;
            }
        }

        // Values which are no longer set must be removed from the meta table. Keys that
        // are not covered by the flags belong to another code path and are left alone.
        for (const auto& entry : storedMeta) {
            const bool coveredByFlag {
                (updateAlternativeEmulator && entry.first == "alternativeEmulator") ||
                (updateScreenLaunch && entry.first == "launchOnOtherScreen")};
            if (!coveredByFlag || desiredMeta.count(entry.first))
                continue;
            Statement statement {connection.get(), "DELETE FROM meta WHERE key = ?"};
            if (!statement.isValid()) {
                success = false;
                break;
            }
            statement.bind(1, entry.first);
            if (!statement.stepDone()) {
                success = false;
                break;
            }
        }

        if (success) {
            std::map<MetaDataListType, std::string> sqlCache;
            for (FileData* file : changedFiles) {
                if (file->getDeletionFlag()) {
                    if (!deleteEntry(connection.get(), file, system)) {
                        success = false;
                        break;
                    }
                    file->metadata.resetChangedFlag();
                    continue;
                }

                const MetaDataListType type {file->getType() == FOLDER ? FOLDER_METADATA
                                                                      : GAME_METADATA};
                if (!sqlCache.count(type)) {
                    sqlCache[type] = insertSql(file->getType() == FOLDER ? "folder" : "game",
                                               getMDDByType(type));
                }
                if (!writeEntry(connection.get(), file, system, sqlCache[type])) {
                    success = false;
                    break;
                }
                if (wasRenamed(file) && !deleteLocalizedName(connection.get(), file, system)) {
                    success = false;
                    break;
                }
                writtenFiles.push_back(file);
            }
        }

        if (!execSql(connection.get(), success ? "COMMIT" : "ROLLBACK",
                     "Couldn't finalize the store transaction"))
            success = false;

        if (!success) {
            LOG(LogError) << "GamelistStore::save(): Couldn't write metadata for system \""
                          << system->getName() << "\"";
            return;
        }

        for (FileData* file : writtenFiles) {
            // A renamed game has its new name in the store now, remember it so the next
            // save does not mistake the translated display name for another rename.
            if (wasRenamed(file)) {
                file->metadata.set(nameOriginalKey, file->metadata.get("name"));
                file->metadata.set(nameLocalizedKey, std::string());
            }
            file->metadata.resetChangedFlag();
        }

        LOG(LogDebug) << "GamelistStore::save(): Updated " << writtenFiles.size()
                      << (writtenFiles.size() == 1 ? " entity in \"" : " entities in \"")
                      << storePath << "\"";
    }

    // ------------------------------------------------------------------
    // One time import of gamelist.xml
    // ------------------------------------------------------------------

    bool importXml(SystemData* system)
    {
        const std::string xmlPath {system->getGamelistPath(false)};
        const std::string storePath {getPlatformStorePath(system->getName())};

        if (!Utils::FileSystem::exists(xmlPath))
            return false;

        if (Utils::FileSystem::getFileSize(xmlPath) == 0) {
            LOG(LogWarning) << "GamelistStore::importXml(): System \"" << system->getName()
                            << "\" has an empty gamelist.xml file";
            return false;
        }

        pugi::xml_document doc;
#if defined(_WIN64)
        const pugi::xml_parse_result& result {
            doc.load_file(Utils::String::stringToWideString(xmlPath).c_str())};
#else
        const pugi::xml_parse_result& result {doc.load_file(xmlPath.c_str())};
#endif

        if (!result) {
            LOG(LogError) << "Error parsing gamelist file \"" << xmlPath
                          << "\": " << result.description();
            return false;
        }

        const pugi::xml_node& root {doc.child("gameList")};
        if (!root) {
            LOG(LogError) << "Couldn't find <gameList> node in gamelist \"" << xmlPath << "\"";
            return false;
        }

        std::map<std::string, std::string> meta;

        // The long term plan is to move the alternativeEmulator element so it becomes a
        // child to the gameList root element, ES-DE reads it from either location.
        const pugi::xml_node& alternativeEmulator {doc.child("alternativeEmulator") != nullptr ?
                                                       doc.child("alternativeEmulator") :
                                                       root.child("alternativeEmulator")};

        if (alternativeEmulator) {
            const std::string& label {alternativeEmulator.child("label").text().get()};
            if (!label.empty() && label.find("<INVALID>") != 0)
                meta["alternativeEmulator"] = label;
        }

        const pugi::xml_node& launchOnOtherScreen {root.child("launchOnOtherScreen")};
        if (launchOnOtherScreen)
            meta["launchOnOtherScreen"] = launchOnOtherScreen.text().get();

        struct ImportedEntry
        {
            std::string path;
            std::map<std::string, std::string> fields;
        };

        std::vector<ImportedEntry> entries[2];

        for (int i {0}; i < 2; ++i) {
            const std::string tag {i == 0 ? "game" : "folder"};
            const MetaDataListType type {i == 0 ? GAME_METADATA : FOLDER_METADATA};

            for (pugi::xml_node fileNode {root.child(tag.c_str())}; fileNode;
                 fileNode = fileNode.next_sibling(tag.c_str())) {
                const pugi::xml_node& pathNode {fileNode.child("path")};
                ImportedEntry entry;
                entry.path = pathNode ? pathNode.text().get() : "";

                if (entry.path.empty()) {
                    LOG(LogDebug) << "\"" << xmlPath << "\": <" << tag
                                  << "> entry without <path>, skipping entry";
                    continue;
                }

                for (const auto& decl : getMDDByType(type)) {
                    pugi::xml_node md {fileNode.child(decl.key.c_str())};
                    if (md && !md.text().empty())
                        entry.fields[decl.key] = md.text().get();
                }

                entries[i].push_back(entry);
            }
        }

        LOG(LogInfo) << "Importing gamelist file \"" << xmlPath << "\" into \"" << storePath
                     << "\" (" << entries[0].size() << " games, " << entries[1].size()
                     << " folders)...";

        // Write to a temporary file and rename it into place so an interrupted import
        // cannot leave a half populated store behind.
        const std::string tempPath {storePath + ".tmp"};
        bool success {true};

        // A store left over from an interrupted import must not be reused, it could
        // contain rows from an earlier partial run.
        Utils::FileSystem::removeFile(tempPath);

        {
            if (!Utils::FileSystem::exists(Utils::FileSystem::getParent(storePath)))
                Utils::FileSystem::createDirectory(Utils::FileSystem::getParent(storePath));

            Connection connection {tempPath, false};
            if (!connection.isValid())
                return false;

            success = createSchema(connection.get()) &&
                      execSql(connection.get(), "BEGIN", "Couldn't start the store transaction");

            if (success) {
                for (const auto& entry : meta) {
                    Statement statement {connection.get(),
                                         "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"};
                    if (!statement.isValid()) {
                        success = false;
                        break;
                    }
                    statement.bind(1, entry.first);
                    statement.bind(2, entry.second);
                    if (!statement.stepDone()) {
                        success = false;
                        break;
                    }
                }
            }

            for (int i {0}; success && i < 2; ++i) {
                const std::string table {i == 0 ? "game" : "folder"};
                const MetaDataListType type {i == 0 ? GAME_METADATA : FOLDER_METADATA};
                const std::vector<MetaDataDecl>& mdd {getMDDByType(type)};
                const std::string sql {insertSql(table, mdd)};

                for (const auto& entry : entries[i]) {
                    Statement statement {connection.get(), sql};
                    if (!statement.isValid()) {
                        success = false;
                        break;
                    }
                    statement.bind(1, entry.path);
                    for (size_t n {0}; n < mdd.size(); ++n) {
                        const auto value {entry.fields.find(mdd.at(n).key)};
                        statement.bind(static_cast<int>(n) + 2,
                                       value != entry.fields.cend() ? value->second
                                                                    : mdd.at(n).defaultValue);
                    }
                    if (!statement.stepDone()) {
                        success = false;
                        break;
                    }
                }
            }

            if (!execSql(connection.get(), success ? "COMMIT" : "ROLLBACK",
                         "Couldn't finalize the store transaction"))
                success = false;
        }

        if (!success || Utils::FileSystem::exists(storePath)) {
            Utils::FileSystem::removeFile(tempPath);
            return false;
        }

        // Utils::FileSystem::renameFile() forwards the raw std::rename() result, so its
        // return value is inverted (zero on success means false), the outcome is therefore
        // verified by looking at the files instead of at the return value.
        Utils::FileSystem::renameFile(tempPath, storePath, true);
        if (Utils::FileSystem::exists(tempPath) || !Utils::FileSystem::exists(storePath)) {
            LOG(LogError) << "GamelistStore::importXml(): Couldn't move \"" << tempPath
                          << "\" to \"" << storePath << "\"";
            Utils::FileSystem::removeFile(tempPath);
            return false;
        }

        return true;
    }

} // namespace GamelistStore
