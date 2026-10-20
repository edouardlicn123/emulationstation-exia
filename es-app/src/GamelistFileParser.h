//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GamelistFileParser.h
//
//  Loads and stores per game metadata. These are thin wrappers around the
//  sqlite based GamelistStore so existing call sites keep working unchanged.
//

#ifndef ES_APP_GAMELIST_FILE_PARSER_H
#define ES_APP_GAMELIST_FILE_PARSER_H

class SystemData;

namespace GamelistFileParser
{
    // Loads stored metadata into a SystemData, importing gamelist.xml one time
    // if the system has no store yet.
    void parseGamelist(SystemData* system);

    // Writes currently loaded metadata for a SystemData to its metadata store.
    void updateGamelist(SystemData* system,
                        const bool updateAlternativeEmulator = false,
                        const bool updateScreenLaunch = false);

} // namespace GamelistFileParser

#endif // ES_APP_GAMELIST_FILE_PARSER_H
