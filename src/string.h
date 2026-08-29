/** @file src/string.h String definitions. */

#ifndef STRING_H
#define STRING_H

/**
 * Types of Language available in the game.
 */
typedef enum Language {
	LANGUAGE_ENGLISH     = 0,
	LANGUAGE_FRENCH      = 1,
	LANGUAGE_GERMAN      = 2,
	LANGUAGE_ITALIAN     = 3,
	LANGUAGE_SPANISH     = 4,
	LANGUAGE_HEBREW      = 5,

	LANGUAGE_MAX         = 6,
	LANGUAGE_INVALID     = 0xFF
} Language;

extern const char * const g_languageSuffixes[LANGUAGE_MAX];

/**
 * IDs for strings that live outside the original game's own string tables
 * (DUNE.HEB/TEXTH.HEB/etc, loaded by String_Init()/String_Get_ByIndex()) --
 * i.e. text this engine's C code hardcodes as a plain, always-English
 * string literal rather than looking up by STR_* index. House names
 * (src/table/houseinfo.c's g_table_houseInfo[].name) are one example:
 * every language draws "Atreides"/"Harkonnen"/etc. untranslated. A couple
 * of modal error messages (e.g. Game_LoadScenario()'s "No more
 * scenarios!") are the other kind -- edge cases that never got a STR_*
 * slot like their neighbors.
 *
 * EngineString_Get() looks these up in an optional ENGINE.<suffix> file
 * (built from hebrew/translations/engine_strings.json by
 * hebrew/tools/build_heb.py), falling back to the caller-supplied English
 * text when no such file exists for the active language -- so this is
 * safe to call unconditionally from every language, not just Hebrew.
 *
 * Order here must match the entry order in engine_strings.json exactly
 * (ENGINE.<suffix> has no per-entry ID, just a position-indexed offset
 * table, the same format as DUNE.HEB etc -- see hebrew/tools/eng.py).
 */
typedef enum EngineStringID {
	ENGINE_STR_HOUSE_HARKONNEN,
	ENGINE_STR_HOUSE_ATREIDES,
	ENGINE_STR_HOUSE_ORDOS,
	ENGINE_STR_HOUSE_FREMEN,
	ENGINE_STR_HOUSE_SARDAUKAR,
	ENGINE_STR_HOUSE_MERCENARY,
	ENGINE_STR_NO_MORE_SCENARIOS,
	ENGINE_STR_NO_ITEMS_IN_CONSTRUCTION_LIST,

	ENGINE_STR_COUNT
} EngineStringID;

extern uint16 String_DecompressAndTranslate(const char *source, char *dest, uint16 destLen);
extern const char *String_GenerateFilename(const char *name);
extern char *String_Get_ByIndex(uint16 stringID);
extern const char *EngineString_Get(EngineStringID id, const char *fallback);
extern void String_Init(void);
extern void String_Uninit(void);
extern uint8 *String_NextString(uint8 *ptr);
extern uint8 *String_PrevString(uint8 *ptr);
extern void String_Trim(char *string);

#endif /* STRING_H */
