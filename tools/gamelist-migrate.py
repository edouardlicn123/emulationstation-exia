#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# ES-DE Frontend
# gamelist-migrate.py
#
# Offline migration of gamelist.xml files (and custom collection configuration
# files) into the sqlite based metadata store described in devdoc/38.
#
# This is step 1 of the implementation order in devdoc/38 section 16: it is a
# standalone safety net which does not modify ES-DE itself. The source xml and
# cfg files are only ever read, never written, so the migration can be rolled
# back simply by removing the destination directory.
#
# The xml semantics are replicated from es-app/src/GamelistFileParser.cpp
# (parseGamelist / updateGamelist) and es-app/src/MetaData.cpp (gameDecls /
# folderDecls), including:
#   * gamelist location resolution (SystemData::getGamelistPath())
#   * only the 24 game and 18 folder metadata fields are stored, all as TEXT
#   * <path> is stored verbatim, i.e. relative to the system start path
#   * root level <alternativeEmulator> and <launchOnOtherScreen> go to meta
#   * empty or missing <path> entries are skipped (ES-DE skips them as well)
#
# Usage:
#   ./gamelist-migrate.py                       import everything, then verify
#   ./gamelist-migrate.py --verify-only         compare an existing store
#   ./gamelist-migrate.py --only snes --force   re-import a single system
#   ./gamelist-migrate.py --dest /tmp/store     import into a scratch directory

import argparse
import os
import re
import shutil
import sqlite3
import sys
import xml.etree.ElementTree as ET

SCHEMA_VERSION = "1"

# Fallback field lists, kept in sync with es-app/src/MetaData.cpp. The tool
# parses MetaData.cpp directly when it is available so that adding a metadata
# field cannot silently produce a store with a missing column.
FALLBACK_GAME_FIELDS = [
    ("name", ""), ("sortname", ""), ("collectionsortname", ""), ("desc", ""),
    ("rating", "0.0"), ("releasedate", ""), ("developer", ""), ("publisher", ""),
    ("genre", ""), ("players", "0"), ("favorite", "false"), ("completed", "false"),
    ("kidgame", "false"), ("hidden", "false"), ("broken", "false"),
    ("nogamecount", "false"), ("nomultiscrape", "false"), ("hidemetadata", "false"),
    ("playcount", "0"), ("playtime", "0"), ("screen", "0"), ("controller", ""),
    ("altemulator", ""), ("lastplayed", "0"),
]
FALLBACK_FOLDER_FIELDS = [
    ("name", ""), ("desc", ""), ("rating", "0.0"), ("releasedate", ""),
    ("developer", ""), ("publisher", ""), ("genre", ""), ("players", "0"),
    ("favorite", "false"), ("completed", "false"), ("kidgame", "false"),
    ("hidden", "false"), ("broken", "false"), ("nomultiscrape", "false"),
    ("hidemetadata", "false"), ("controller", ""), ("folderlink", ""),
    ("lastplayed", "0"),
]


_seen_warnings = set()


def log(message):
    print(message, flush=True)


def warn(message):
    # The verification pass parses every file a second time, so only report
    # each distinct warning once.
    if message in _seen_warnings:
        return
    _seen_warnings.add(message)
    print("WARNING: " + message, flush=True)


def fail(message):
    print("ERROR: " + message, file=sys.stderr, flush=True)


# --------------------------------------------------------------------------
# Metadata field declarations
# --------------------------------------------------------------------------

def load_mdd(metadat_cpp):
    """Extract (key, default) pairs from MetaData.cpp gameDecls / folderDecls."""
    try:
        with open(metadat_cpp, "r", encoding="utf-8") as handle:
            source = handle.read()
    except OSError:
        return None, None

    entry_re = re.compile(r'\{\s*"([^"]+)"\s*,\s*(MD_\w+)\s*,\s*"([^"]*)"')

    def block(name):
        match = re.search(r"MetaDataDecl\s+" + name + r"\s*\[\]\s*\{(.*?)\n\s*\};", source, re.S)
        if not match:
            return None
        fields = entry_re.findall(match.group(1))
        return [(key, default) for key, _type, default in fields] or None

    return block("gameDecls"), block("folderDecls")


# --------------------------------------------------------------------------
# System enumeration and gamelist location (SystemData::getGamelistPath)
# --------------------------------------------------------------------------

def expand_path(path, rompath=None):
    path = path.strip()
    if rompath:
        path = path.replace("%ROMPATH%", rompath)
    path = os.path.expandvars(path)
    return os.path.abspath(os.path.expanduser(path))


def read_settings(es_home):
    """Return the settings as {name: value}. ES-DE writes es_settings.xml as a
    bare list of top level elements without a root node (pugixml tolerates that),
    so the file has to be wrapped before an XML parser accepts it."""
    settings_path = os.path.join(es_home, "settings", "es_settings.xml")
    values = {}
    try:
        with open(settings_path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        # Drop the XML declaration so the file can be wrapped in a root node.
        text = re.sub(r"^\s*<\?xml[^>]*\?>", "", text, count=1)
        root = ET.fromstring("<config>" + text + "</config>")
    except (OSError, ET.ParseError) as error:
        warn("could not read \"%s\": %s" % (settings_path, error))
        return values
    for node in root:
        name = node.get("name")
        value = node.get("value")
        if not name or value is None:
            continue
        values[name] = (value == "true") if node.tag == "bool" else value
    return values


def read_rom_directory(settings):
    """FileData::getROMDirectory(): the ROMDirectory setting, else ~/ROMs/."""
    rom_dir = settings.get("ROMDirectory", "")
    if not isinstance(rom_dir, str) or not rom_dir:
        rom_dir = os.path.expanduser("~/ROMs/")
    rom_dir = os.path.abspath(os.path.expanduser(rom_dir))
    return rom_dir if rom_dir.endswith(os.sep) else rom_dir + os.sep


def read_systems(systems_files, rompath):
    """Return an ordered list of (name, startPath) tuples. The caller passes the
    custom systems file first because SystemData::getConfigPath() puts it ahead
    of the built in one, i.e. custom entries take priority."""
    systems = []
    seen = set()
    for systems_file in systems_files:
        if not systems_file or not os.path.isfile(systems_file):
            continue
        try:
            root = ET.parse(systems_file).getroot()
        except (OSError, ET.ParseError) as error:
            warn("could not parse \"%s\": %s" % (systems_file, error))
            continue
        for system in root.findall("system"):
            name_node = system.find("name")
            path_node = system.find("path")
            if name_node is None or not (name_node.text or "").strip():
                continue
            name = name_node.text.strip()
            if name in seen:
                continue
            if path_node is None or not path_node.text:
                continue
            seen.add(name)
            systems.append((name, expand_path(path_node.text, rompath)))
    return systems


def locate_gamelist(name, start_path, es_home, legacy):
    """Resolve the gamelist.xml ES-DE would read for this system, or None."""
    rom_side = os.path.join(start_path, "gamelist.xml")
    appdata_side = os.path.join(es_home, "gamelists", name, "gamelist.xml")

    if os.path.isfile(rom_side):
        if legacy:
            return rom_side
        warn("found \"%s\" but LegacyGamelistFileLocation is disabled, using \"%s\" instead"
             % (rom_side, appdata_side))
    if os.path.isfile(appdata_side):
        return appdata_side
    return None


# --------------------------------------------------------------------------
# gamelist.xml reading
# --------------------------------------------------------------------------

def resolve_path(value, start_path):
    if os.path.isabs(value):
        return os.path.normpath(value)
    return os.path.normpath(os.path.join(start_path, value))


def document_level_alternative_emulator(text):
    """pugixml reads <alternativeEmulator> as a sibling of <gameList>, which
    ElementTree does not expose, so look it up in the raw file text."""
    boundary = text.find("<gameList")
    if boundary < 0:
        boundary = len(text)
    match = re.search(r"<alternativeEmulator>\s*<label>(.*?)</label>",
                      text[:boundary], re.S)
    return match.group(1) if match else None


def read_gamelist(xml_path, start_path, game_fields, folder_fields):
    """Parse a gamelist.xml into (meta, game rows, folder rows). start_path is
    SystemData::getStartPath(), i.e. the system ROM directory, because that is
    what ES-DE resolves relative <path> values against even when the file
    itself lives in the application data directory."""
    meta = {}
    rows = {"game": {}, "folder": {}}
    skipped = 0
    duplicates = 0

    try:
        with open(xml_path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError as error:
        fail("could not read \"%s\": %s" % (xml_path, error))
        return None, None, None, None

    try:
        root = ET.fromstring(text)
    except ET.ParseError as error:
        fail("could not parse \"%s\": %s" % (xml_path, error))
        return None, None, None, None

    if root.tag != "gameList":
        fail("\"%s\" has no <gameList> root element" % xml_path)
        return None, None, None, None

    label = document_level_alternative_emulator(text)
    if label is None:
        emulator_node = root.find("alternativeEmulator")
        if emulator_node is not None:
            label_node = emulator_node.find("label")
            if label_node is not None:
                label = label_node.text
    if label:
        meta["alternativeEmulator"] = label

    screen_node = root.find("launchOnOtherScreen")
    if screen_node is not None and screen_node.text is not None:
        meta["launchOnOtherScreen"] = screen_node.text

    for tag, fields in (("game", game_fields), ("folder", folder_fields)):
        keys = [key for key, _default in fields]
        for element in root.findall(tag):
            path_node = element.find("path")
            path = path_node.text if path_node is not None else None
            if not path:
                warn("\"%s\": <%s> entry without <path>, skipping" % (xml_path, tag))
                skipped += 1
                continue

            row = dict(fields)
            for key in keys:
                if key == "path":
                    continue
                child = element.find(key)
                # pugixml only falls back to the default when there is no text
                # node at all, an element containing whitespace keeps its value.
                if child is not None and child.text is not None:
                    row[key] = child.text
            row["path"] = path

            # ES-DE applies later entries on top of earlier ones for the same
            # file, so the last entry for a resolved path wins.
            key = resolve_path(path, start_path)
            if key in rows[tag]:
                duplicates += 1
                if duplicates <= 5:
                    warn("\"%s\": duplicate <%s> entry for \"%s\"" % (xml_path, tag, key))
            rows[tag][key] = row

    return meta, rows, skipped, duplicates


# --------------------------------------------------------------------------
# Store writing and verification
# --------------------------------------------------------------------------

def column_names(fields):
    return ["path"] + [key for key, _default in fields]


def create_schema(connection, game_fields, folder_fields, members=False):
    connection.execute("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)")
    if members:
        connection.execute("CREATE TABLE member (path TEXT PRIMARY KEY)")
        return
    for table, fields in (("game", game_fields), ("folder", folder_fields)):
        columns = ", ".join(["path TEXT PRIMARY KEY"] +
                            [name + " TEXT" for name in column_names(fields)[1:]])
        connection.execute("CREATE TABLE %s (%s)" % (table, columns))
    connection.execute("CREATE TABLE game_name_i18n (path TEXT, locale TEXT, name TEXT, "
                       "PRIMARY KEY (path, locale))")


def has_unique_path_index(connection, table):
    """True when the table enforces one row per path, i.e. path is the primary key."""
    for entry in connection.execute("PRAGMA index_list(%s)" % table).fetchall():
        if entry[2] == 1:  # unique
            columns = [column[2] for column in
                       connection.execute("PRAGMA index_info(%s)" % entry[1]).fetchall()]
            if columns == ["path"]:
                return True
    return False


def write_store(db_path, meta, rows, game_fields, folder_fields):
    parent = os.path.dirname(db_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)

    temp_path = db_path + ".tmp"
    if os.path.exists(temp_path):
        os.remove(temp_path)

    connection = sqlite3.connect(temp_path)
    try:
        connection.execute("PRAGMA journal_mode=WAL")
        connection.execute("PRAGMA synchronous=NORMAL")
        create_schema(connection, game_fields, folder_fields)
        entries = list(meta.items()) + [("schema_version", SCHEMA_VERSION)]
        connection.executemany("INSERT INTO meta (key, value) VALUES (?, ?)", entries)
        for table in ("game", "folder"):
            fields = game_fields if table == "game" else folder_fields
            keys = column_names(fields)
            payload = [tuple([row["path"]] +
                             [str(row.get(key, default)) for key, default in fields])
                       for row in rows[table].values()]
            connection.executemany(
                "INSERT INTO %s (%s) VALUES (%s)"
                % (table, ", ".join(keys), ", ".join("?" * len(keys))), payload)
        connection.commit()
    finally:
        connection.close()

    if os.path.exists(db_path):
        os.remove(db_path)
    os.rename(temp_path, db_path)

    # WAL files belong to an open connection, make sure none are left behind.
    for suffix in ("-wal", "-shm"):
        leftover = db_path + suffix
        if os.path.exists(leftover):
            os.remove(leftover)


def read_store(db_path):
    """Read a store back into the same shape read_gamelist() produces."""
    meta = {}
    rows = {"game": {}, "folder": {}}
    connection = sqlite3.connect(db_path)
    try:
        for key, value in connection.execute("SELECT key, value FROM meta"):
            if key != "schema_version":
                meta[key] = value
        for table in ("game", "folder"):
            columns = [description[0] for description in
                       connection.execute("SELECT * FROM %s LIMIT 0" % table).description]
            if not columns:
                continue
            if columns[0] != "path":
                fail("store \"%s\" has no leading path column" % db_path)
                return None, None
            if not has_unique_path_index(connection, table):
                fail("store \"%s\" table \"%s\" does not enforce one row per path"
                     % (db_path, table))
                return None, None
            keys = columns[1:]
            for values in connection.execute("SELECT * FROM %s" % table):
                row = dict(zip(keys, values[1:]))
                row["path"] = values[0]
                rows[table][row["path"]] = row
    finally:
        connection.close()
    return meta, rows


def verify(name, xml_path, db_path, start_path, game_fields, folder_fields):
    expected_meta, expected_rows, _skipped, _dupes = read_gamelist(xml_path, start_path,
                                                                   game_fields, folder_fields)
    if expected_meta is None:
        return None
    actual_meta, actual_rows = read_store(db_path)
    if actual_meta is None:
        return ["store could not be read"]

    problems = []
    if expected_meta != actual_meta:
        problems.append("meta differs: xml=%s store=%s" % (expected_meta, actual_meta))
    for table in ("game", "folder"):
        # Index both sides by the resolved path, the store keeps <path> exactly
        # as it appears in the xml but entries are matched like ES-DE does.
        expected = {resolve_path(row["path"], start_path): row
                    for row in expected_rows[table].values()}
        actual = {resolve_path(row["path"], start_path): row
                  for row in actual_rows[table].values()}
        missing = sorted(set(expected) - set(actual))
        extra = sorted(set(actual) - set(expected))
        if missing:
            problems.append("%s: %d entries missing from store (e.g. %s)"
                            % (table, len(missing), missing[0]))
        if extra:
            problems.append("%s: %d unexpected entries in store (e.g. %s)"
                            % (table, len(extra), extra[0]))
        for key in sorted(set(expected) & set(actual)):
            if dict(expected[key]) != dict(actual[key]):
                for field in sorted(set(expected[key]) | set(actual[key])):
                    if expected[key].get(field) != actual[key].get(field):
                        problems.append("%s[%s].%s: xml=%r store=%r"
                                        % (table, key, field,
                                           expected[key].get(field), actual[key].get(field)))
                break
    return problems


# --------------------------------------------------------------------------
# Collections
# --------------------------------------------------------------------------

def read_custom_collections(collections_dir):
    """Return {collection name: [member paths]} for custom-*.cfg files."""
    collections = {}
    if not os.path.isdir(collections_dir):
        return collections
    for entry in sorted(os.listdir(collections_dir)):
        if not entry.startswith("custom-") or not entry.endswith(".cfg"):
            continue
        name = entry[len("custom-"):-len(".cfg")]
        members = []
        try:
            with open(os.path.join(collections_dir, entry), "r",
                      encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    line = line.replace("\r", "").strip()
                    if line:
                        members.append(line)
        except OSError as error:
            fail("could not read \"%s\": %s" % (entry, error))
            continue
        collections[name] = members
    return collections


def write_collection_store(db_path, name, members):
    parent = os.path.dirname(db_path)
    if parent and not os.path.isdir(parent):
        os.makedirs(parent)
    connection = sqlite3.connect(db_path)
    try:
        connection.execute("PRAGMA journal_mode=WAL")
        connection.execute("PRAGMA synchronous=NORMAL")
        create_schema(connection, [], [], members=True)
        connection.execute("INSERT INTO meta (key, value) VALUES (?, ?)",
                           ("schema_version", SCHEMA_VERSION))
        connection.execute("INSERT INTO meta (key, value) VALUES (?, ?)", ("name", name))
        connection.executemany("INSERT OR IGNORE INTO member (path) VALUES (?)",
                               [(member,) for member in members])
        connection.commit()
    finally:
        connection.close()
    for suffix in ("-wal", "-shm"):
        leftover = db_path + suffix
        if os.path.exists(leftover):
            os.remove(leftover)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)

    parser = argparse.ArgumentParser(
        description="Import gamelist.xml files into the sqlite metadata store (devdoc/38)")
    parser.add_argument("--es-home", default=os.path.expanduser("~/.config/ES-DE"),
                        help="ES-DE data directory (default: ~/.config/ES-DE)")
    parser.add_argument("--dest", default=os.path.expanduser("~/.config/gamelists"),
                        help="store root (default: ~/.config/gamelists)")
    parser.add_argument("--systems", action="append", default=[],
                        help="limit to this system name, may be repeated")
    parser.add_argument("--force", action="store_true",
                        help="rebuild store files that already exist")
    parser.add_argument("--verify-only", action="store_true",
                        help="compare the existing store against the xml files")
    parser.add_argument("--no-collections", action="store_true",
                        help="skip custom collection migration")
    parser.add_argument("--metadat-cpp",
                        default=os.path.join(repo_root, "es-app", "src", "MetaData.cpp"),
                        help="path to MetaData.cpp for the field declarations")
    parser.add_argument("--platforms-file", default=None,
                        help="es_systems.xml used to enumerate systems")
    parser.add_argument("--custom-systems-file", default=None,
                        help="custom es_systems.xml used to enumerate systems")
    args = parser.parse_args()

    es_home = os.path.abspath(os.path.expanduser(args.es_home))
    dest = os.path.abspath(os.path.expanduser(args.dest))
    settings = read_settings(es_home)
    legacy = settings.get("LegacyGamelistFileLocation") is True
    rompath = read_rom_directory(settings)

    game_fields, folder_fields = load_mdd(args.metadat_cpp)
    field_source = "MetaData.cpp"
    if not game_fields or not folder_fields:
        game_fields, folder_fields = FALLBACK_GAME_FIELDS, FALLBACK_FOLDER_FIELDS
        field_source = "built-in fallback list"
    log("metadata fields: %d game / %d folder (from %s)"
        % (len(game_fields), len(folder_fields), field_source))

    platforms_file = args.platforms_file
    if not platforms_file:
        candidate = os.path.join(repo_root, "resources", "systems", "linux", "es_systems.xml")
        platforms_file = candidate if os.path.isfile(candidate) else None
    custom_file = args.custom_systems_file
    if not custom_file:
        candidate = os.path.join(es_home, "custom_systems", "es_systems.xml")
        custom_file = candidate if os.path.isfile(candidate) else None

    systems = read_systems([custom_file, platforms_file], rompath)
    if not systems:
        fail("no systems found, pass --platforms-file")
        return 1
    if args.systems:
        wanted = set(args.systems)
        systems = [(name, path) for name, path in systems if name in wanted]
        unknown = wanted - {name for name, _path in systems}
        for name in sorted(unknown):
            warn("unknown system \"%s\"" % name)

    log("data directory: %s" % es_home)
    log("store root:     %s" % dest)
    log("ROM directory:  %s" % rompath)
    log("gamelist order: %s" % ("ROM directory (LegacyGamelistFileLocation=true)"
                                if legacy else "app data directory"))

    platforms_dir = os.path.join(dest, "platforms")
    totals = {"xml": 0, "game": 0, "folder": 0, "skipped": 0, "dupes": 0, "missing": 0,
              "imported": 0, "reused": 0, "failed": 0}
    problems = []

    for name, start_path in systems:
        xml_path = locate_gamelist(name, start_path, es_home, legacy)
        if not xml_path:
            continue

        meta, rows, skipped, duplicates = read_gamelist(xml_path, start_path,
                                                        game_fields, folder_fields)
        if meta is None:
            totals["failed"] += 1
            continue

        db_path = os.path.join(platforms_dir, name + ".sqlite")
        existing = os.path.isfile(db_path)

        if args.verify_only:
            if not existing:
                problems.append("%s: no store file at \"%s\"" % (name, db_path))
                continue
            issues = verify(name, xml_path, db_path, start_path, game_fields, folder_fields)
            if issues:
                problems.extend("%s: %s" % (name, issue) for issue in issues)
            else:
                totals["reused"] += 1
        else:
            if existing and not args.force:
                totals["reused"] += 1
            else:
                write_store(db_path, meta, rows, game_fields, folder_fields)
                totals["imported"] += 1
            issues = verify(name, xml_path, db_path, start_path, game_fields, folder_fields)
            if issues:
                problems.extend("%s: %s" % (name, issue) for issue in issues)

        missing = 0
        for table in ("game", "folder"):
            for row in rows[table].values():
                resolved = resolve_path(row["path"], start_path)
                if not os.path.exists(resolved):
                    missing += 1
        totals["xml"] += 1
        totals["game"] += len(rows["game"])
        totals["folder"] += len(rows["folder"])
        totals["skipped"] += skipped
        totals["dupes"] += duplicates
        totals["missing"] += missing

    collections = {}
    if not args.no_collections and not args.systems:
        collections = read_custom_collections(os.path.join(es_home, "collections"))
        for name, members in collections.items():
            db_path = os.path.join(dest, "collections", name + ".sqlite")
            if args.verify_only or (os.path.isfile(db_path) and not args.force):
                totals["reused"] += 1
            else:
                write_collection_store(db_path, name, members)
                totals["imported"] += 1

    log("")
    log("gamelist files processed: %d" % totals["xml"])
    log("game rows:                %d" % totals["game"])
    log("folder rows:              %d" % totals["folder"])
    log("rows without <path>:      %d" % totals["skipped"])
    log("duplicate rows merged:    %d" % totals["dupes"])
    log("rows not on disk:         %d" % totals["missing"])
    log("collections imported:     %d" % len(collections))
    log("store files written:      %d" % totals["imported"])
    log("store files reused:       %d" % totals["reused"])
    log("store files failed:       %d" % totals["failed"])

    if problems:
        log("")
        for problem in problems[:50]:
            fail(problem)
        if len(problems) > 50:
            fail("... and %d more" % (len(problems) - 50))
        return 1

    log("")
    log("verification: all imported rows match their xml source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
