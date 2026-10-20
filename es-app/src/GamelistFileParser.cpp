//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GamelistFileParser.cpp
//
//  Compatibility wrapper for the sqlite based metadata store. The gamelist.xml
//  files are neither read nor written anymore, they are only imported one time
//  when a system has no store yet (see devdoc/38).
//

#include "GamelistFileParser.h"

#include "GamelistStore.h"

namespace GamelistFileParser
{
    void parseGamelist(SystemData* system)
    {
        GamelistStore::load(system);
    }

    void updateGamelist(SystemData* system,
                        const bool updateAlternativeEmulator,
                        const bool updateScreenLaunch)
    {
        GamelistStore::save(system, updateAlternativeEmulator, updateScreenLaunch);
    }

} // namespace GamelistFileParser
