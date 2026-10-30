//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  SystemDataSqlite.h
//
//  Reads the sqlite based system configuration stores which replace
//  es_systems.xml as the runtime source. See devdoc/54 for the schema and the
//  fallback rules.

#ifndef ES_APP_SYSTEM_DATA_SQLITE_H
#define ES_APP_SYSTEM_DATA_SQLITE_H

#include <string>

namespace pugi
{
    class xml_document;
}

namespace SystemDataSqlite
{
    // Loads the user store, falling back to the builtin store. Returns false
    // when neither store is usable, in which case the caller must use the
    // bundled XML files.
    bool loadSystems(pugi::xml_document& document, std::string& source);

    // Localized system introduction. Returns an empty string when the store has
    // no record for the system.
    std::string getLocalizedFullName(const std::string& systemName,
                                      const std::string& defaultFullName);

    std::string getIntro(const std::string& systemName);

    // Localized full name from system_name_i18n. Falls back to defaultFullName.
    std::string getLocalizedFullName(const std::string& systemName,
                                      const std::string& defaultFullName);
} // namespace SystemDataSqlite

#endif // ES_APP_SYSTEM_DATA_SQLITE_H
