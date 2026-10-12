#ifndef PORT_TEXT_I18N_H
#define PORT_TEXT_I18N_H

#include <stdint.h>

/* Languages the port's own UI text (bottom screen, updater) is translated
 * into. Follows the language the player picked in the game's file-select
 * screen; the USA ROM (English only) and the JP ROM always resolve to EN. */
typedef enum {
    PORT_TEXT_EN = 0,
    PORT_TEXT_FR,
    PORT_TEXT_DE,
    PORT_TEXT_ES,
    PORT_TEXT_IT,
    PORT_TEXT_COUNT
} PortTextLanguage;

#ifdef __cplusplus
extern "C" {
#endif

/* Takes the game's public Language enum (LANGUAGE_JP..LANGUAGE_IT in
 * include/main.h, not the EU save slot). Unknown values select English. */
void Port_Text_SetLanguage(int publicLanguage);
PortTextLanguage Port_Text_GetLanguage(void);

/* Translate an English UI string, keyed by its exact (upper-case) literal.
 * Strings with no entry are returned unchanged, so call sites keep the
 * English literal and new text degrades to English, never to nothing.
 * The result is a static string; it is never freed. */
const char* Port_Text_Tr(const char* english);

/* Same, for an explicit language (tests, tools). */
const char* Port_Text_TrFor(PortTextLanguage lang, const char* english);

/* Table access for the unit test: row count, and one cell (NULL when out
 * of range). */
int Port_Text_EntryCount(void);
const char* Port_Text_EntryAt(int index, PortTextLanguage lang);

/* Decode one UTF-8 code point from *p (advancing it) and return the game's
 * character code for it: the game's text charmap is Latin-1 for the
 * accented range (0xE9 = é, 0xC9 = É, 0xA1 = ¡, 0xBF = ¿). Anything
 * outside Latin-1 or malformed returns '?'. Never reads past the NUL. */
uint8_t Port_Text_Utf8Next(const char** p);

/* ASCII letter an accented Latin-1 code reduces to (é -> e, Ñ -> N), or 0
 * if it has none. Lets a renderer fall back when a font lacks the glyph. */
char Port_Text_BaseLetter(uint8_t code);

#ifdef __cplusplus
}
#endif

#endif
