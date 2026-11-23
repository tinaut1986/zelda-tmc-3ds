/* Host test for the pure turbo-button rules (port/port_turbo_button.h).
 *
 * Build (w64devkit):
 *   gcc -std=gnu11 -I port port/port_turbo_button_test.c -o build/turbo_button_test.exe
 */
#include "port_turbo_button.h"

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
    /* Clamping: an unknown value (hand-edited ini/json) is OFF. */
    CHECK(Port_TurboButton_Clamp(-1) == PORT_TURBO_BUTTON_OFF);
    CHECK(Port_TurboButton_Clamp(0) == PORT_TURBO_BUTTON_OFF);
    CHECK(Port_TurboButton_Clamp(1) == PORT_TURBO_BUTTON_X);
    CHECK(Port_TurboButton_Clamp(2) == PORT_TURBO_BUTTON_Y);
    CHECK(Port_TurboButton_Clamp(3) == PORT_TURBO_BUTTON_OFF);

    /* The OPTIONS cell cycles OFF -> X -> Y -> OFF. */
    CHECK(Port_TurboButton_Cycle(PORT_TURBO_BUTTON_OFF) == PORT_TURBO_BUTTON_X);
    CHECK(Port_TurboButton_Cycle(PORT_TURBO_BUTTON_X) == PORT_TURBO_BUTTON_Y);
    CHECK(Port_TurboButton_Cycle(PORT_TURBO_BUTTON_Y) == PORT_TURBO_BUTTON_OFF);
    CHECK(Port_TurboButton_Cycle(99) == PORT_TURBO_BUTTON_OFF);

    CHECK(strcmp(Port_TurboButton_Name(PORT_TURBO_BUTTON_OFF), "OFF") == 0);
    CHECK(strcmp(Port_TurboButton_Name(PORT_TURBO_BUTTON_X), "X") == 0);
    CHECK(strcmp(Port_TurboButton_Name(PORT_TURBO_BUTTON_Y), "Y") == 0);
    CHECK(strcmp(Port_TurboButton_Name(99), "OFF") == 0);

    /* Only the chosen button holds the turbo on. */
    CHECK(!Port_TurboButton_Held(PORT_TURBO_BUTTON_OFF, true, true));
    CHECK(Port_TurboButton_Held(PORT_TURBO_BUTTON_X, true, false));
    CHECK(!Port_TurboButton_Held(PORT_TURBO_BUTTON_X, false, true));
    CHECK(Port_TurboButton_Held(PORT_TURBO_BUTTON_Y, false, true));
    CHECK(!Port_TurboButton_Held(PORT_TURBO_BUTTON_Y, true, false));
    CHECK(!Port_TurboButton_Held(99, true, true));

    /* The owned soft slot (0 = X, 1 = Y) is the one the button takes over. */
    CHECK(Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_X, 0));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_X, 1));
    CHECK(Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_Y, 1));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_Y, 0));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_OFF, 0));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_OFF, 1));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_X, -1));
    CHECK(!Port_TurboButton_OwnsSoftSlot(PORT_TURBO_BUTTON_X, 2));

    puts("turbo_button_test OK");
    return 0;
}
