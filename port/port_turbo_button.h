#ifndef PORT_TURBO_BUTTON_H
#define PORT_TURBO_BUTTON_H

/* OPTIONS -> GAME -> TURBO BUTTON: which face button fast-forwards while
 * held. The platform reads it every input poll; the button it takes over is
 * no longer a soft-slot item button (port_softslots.h). Plain header rules so
 * the host tests and every backend share one definition. */

#include <stdbool.h>

enum {
    PORT_TURBO_BUTTON_OFF = 0,
    PORT_TURBO_BUTTON_X,
    PORT_TURBO_BUTTON_Y,
    PORT_TURBO_BUTTON_COUNT
};

static inline int Port_TurboButton_Clamp(int button) {
    return button >= PORT_TURBO_BUTTON_OFF && button < PORT_TURBO_BUTTON_COUNT ? button
                                                                                : PORT_TURBO_BUTTON_OFF;
}

/* The OPTIONS cell's cycle: OFF -> X -> Y -> OFF. */
static inline int Port_TurboButton_Cycle(int button) {
    if (button == PORT_TURBO_BUTTON_OFF) return PORT_TURBO_BUTTON_X;
    if (button == PORT_TURBO_BUTTON_X) return PORT_TURBO_BUTTON_Y;
    if (button == PORT_TURBO_BUTTON_Y) return PORT_TURBO_BUTTON_OFF;
    return PORT_TURBO_BUTTON_OFF;
}

static inline const char* Port_TurboButton_Name(int button) {
    switch (button) {
        case PORT_TURBO_BUTTON_X: return "X";
        case PORT_TURBO_BUTTON_Y: return "Y";
        default: return "OFF";
    }
}

/* True while the chosen button is held; OFF never triggers. */
static inline bool Port_TurboButton_Held(int button, bool xHeld, bool yHeld) {
    return (button == PORT_TURBO_BUTTON_X && xHeld) || (button == PORT_TURBO_BUTTON_Y && yHeld);
}

/* The soft slot the button takes over: 0 is X, 1 is Y (port_softslots.h). */
static inline bool Port_TurboButton_OwnsSoftSlot(int button, int softSlot) {
    return (button == PORT_TURBO_BUTTON_X && softSlot == 0) ||
           (button == PORT_TURBO_BUTTON_Y && softSlot == 1);
}

#endif
