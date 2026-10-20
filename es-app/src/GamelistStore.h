//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GamelistStore.h
//
//  Reads and writes the sqlite based metadata stores which have replaced the
//  gamelist.xml files. See devdoc/38 for the schema and the behavior rules.
//

#ifndef ES_APP_GAMELIST_STORE_H
#define ES_APP_GAMELIST_STORE_H

#include <string>
#include <vector>

class SystemData;

namespace GamelistStore
{
    // Root of the store library. This is deliberately placed outside the ES-DE
    // data directory so the stores do not follow the --home setting, see
    // devdoc/38 section 4.2.
    std::string getLibraryRoot();

    std::string getPlatformStorePath(const std::string& systemName);
    bool platformStoreExists(const std::string& systemName);

    // Custom collection stores, one per collection with the collection name as
    // the file name. Only the membership is stored here, the game metadata
    // always comes from the platform stores, see devdoc/38 section 9.
    std::string getCollectionStoreFolder();
    std::string getCollectionStorePath(const std::string& collectionName);
    std::vector<std::string> loadCollectionMembers(const std::string& collectionName);
    bool saveCollectionMembers(const std::string& collectionName,
                               const std::vector<std::string>& members);

    // Loads metadata into a SystemData, importing an existing gamelist.xml file
    // one time if the store for the system does not exist yet.
    void load(SystemData* system);

    // Writes changed metadata to the store for the system, creating it if it does
    // not exist yet. A store is never created when there is nothing to persist.
    void save(SystemData* system,
              const bool updateAlternativeEmulator = false,
              const bool updateScreenLaunch = false);

    // One time import of a gamelist.xml file into a new store. The xml file is only
    // ever read, it is never modified or removed.
    bool importXml(SystemData* system);

    // Writes the store contents back to the gamelist.xml files inside the ROM
    // directories, merging into any existing files. Only fields known to ES-DE are
    // overwritten, external data such as media tags written by other frontends is
    // preserved. Returns the number of systems which were exported.
    int exportAllXml();

} // namespace GamelistStore

#endif // ES_APP_GAMELIST_STORE_H
