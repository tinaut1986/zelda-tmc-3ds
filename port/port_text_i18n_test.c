#include "port_text_i18n.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

int main(void) {
    /* Public Language enum: JP=0 EN=1 FR=2 DE=3 ES=4 IT=5. */
    Port_Text_SetLanguage(0);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_EN);
    Port_Text_SetLanguage(1);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_EN);
    Port_Text_SetLanguage(2);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_FR);
    Port_Text_SetLanguage(3);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_DE);
    Port_Text_SetLanguage(4);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_ES);
    Port_Text_SetLanguage(5);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_IT);
    Port_Text_SetLanguage(6);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_EN);
    Port_Text_SetLanguage(-1);
    CHECK(Port_Text_GetLanguage() == PORT_TEXT_EN);

    /* English and unknown strings come back untouched (same pointer). */
    const char* unknown = "NOT IN THE TABLE";
    Port_Text_SetLanguage(4);
    CHECK(Port_Text_Tr(unknown) == unknown);
    Port_Text_SetLanguage(1);
    CHECK(strcmp(Port_Text_Tr("BACK"), "BACK") == 0);
    Port_Text_SetLanguage(4);
    CHECK(strcmp(Port_Text_Tr("BACK"), "ATR\xC3\x81S") == 0);
    CHECK(strcmp(Port_Text_TrFor(PORT_TEXT_DE, "BACK"), "ZUR\xC3\x9C" "CK") == 0);
    CHECK(Port_Text_TrFor(PORT_TEXT_FR, NULL) == NULL);

    /* Table shape: no empty cell, no duplicate English key, and a
     * translation is never wildly longer than its English (chips and
     * buttons are sized to the English word). */
    int n = Port_Text_EntryCount();
    CHECK(n > 100);
    for (int i = 0; i < n; i++) {
        const char* en = Port_Text_EntryAt(i, PORT_TEXT_EN);
        CHECK(en != NULL && en[0] != '\0');
        for (int j = i + 1; j < n; j++) {
            if (strcmp(en, Port_Text_EntryAt(j, PORT_TEXT_EN)) == 0) {
                fprintf(stderr, "duplicate key: %s\n", en);
                return 1;
            }
        }
        for (int lang = PORT_TEXT_FR; lang < PORT_TEXT_COUNT; lang++) {
            const char* t = Port_Text_EntryAt(i, (PortTextLanguage)lang);
            if (t == NULL || t[0] == '\0') {
                fprintf(stderr, "empty cell: %s [%d]\n", en, lang);
                return 1;
            }
        }
    }

    /* Quarter-width cells of the compact OPTIONS page hold 11 characters of
     * the 5x7 font (6 px each in ~68 px); a longer word would run into the
     * next cell. */
    static const char* const kCellWords[] = {
        "VIEW", "STYLE", "FRAMES", "TOP HUD", "TURBO", "VOLUME", "AUTOSAVE", "HOLD TEXT", "MAP FOLLOW",
        "CREST PINS", "FLOOR BACK", "COLOURS", "THEME", "SHOW FPS", "SHOW", "HIDE", "ON", "OFF", "RESTART",
        "ORIGINAL", "STABLE", "SLATE", "PARCHMENT", "NIGHT GOLD", "FOREST", "PATTERN", "NONE", "BACK",
        "PREV", "NEXT", "SAVE OVER", "DELETE", "LOAD", "SURE?",
    };
    for (size_t i = 0; i < sizeof(kCellWords) / sizeof(kCellWords[0]); i++) {
        for (int lang = PORT_TEXT_FR; lang < PORT_TEXT_COUNT; lang++) {
            const char* t = Port_Text_TrFor((PortTextLanguage)lang, kCellWords[i]);
            int n = 0;
            for (const char* p = t; *p; n++) {
                (void)Port_Text_Utf8Next(&p);
            }
            if (n > 11) {
                fprintf(stderr, "too wide for a cell (%d): %s -> %s\n", n, kCellWords[i], t);
                return 1;
            }
        }
    }

    /* UTF-8 -> game charcode (Latin-1 for the accented range). */
    const char* p = "a\xC3\xA9\xC3\x91\xE2\x99\xAA!";
    CHECK(Port_Text_Utf8Next(&p) == 'a');
    CHECK(Port_Text_Utf8Next(&p) == 0xE9);
    CHECK(Port_Text_Utf8Next(&p) == 0xD1);
    CHECK(Port_Text_Utf8Next(&p) == '?'); /* U+266A is outside Latin-1 */
    CHECK(Port_Text_Utf8Next(&p) == '!');
    CHECK(*p == '\0');
    CHECK(Port_Text_Utf8Next(&p) == 0 && *p == '\0'); /* never past the NUL */

    CHECK(Port_Text_BaseLetter(0xE9) == 'e');
    CHECK(Port_Text_BaseLetter(0xC9) == 'E');
    CHECK(Port_Text_BaseLetter(0xD1) == 'N');
    CHECK(Port_Text_BaseLetter(0xDC) == 'U');
    CHECK(Port_Text_BaseLetter(0xA1) == 0);
    CHECK(Port_Text_BaseLetter(0xDF) == 0);
    CHECK(Port_Text_BaseLetter('A') == 0);

    puts("text_i18n_test OK");
    return 0;
}
