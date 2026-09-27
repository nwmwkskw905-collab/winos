/* Portico Runtime — estado de entrada unificado (touch virtual, gamepad físico). */
#ifndef PORTICO_PR_INPUT_H
#define PORTICO_PR_INPUT_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PR_BTN_A      = 1u << 0,
    PR_BTN_B      = 1u << 1,
    PR_BTN_X      = 1u << 2,
    PR_BTN_Y      = 1u << 3,
    PR_BTN_LB     = 1u << 4,
    PR_BTN_RB     = 1u << 5,
    PR_BTN_START  = 1u << 6,
    PR_BTN_SELECT = 1u << 7,
    PR_BTN_DUP    = 1u << 8,
    PR_BTN_DDOWN  = 1u << 9,
    PR_BTN_DLEFT  = 1u << 10,
    PR_BTN_DRIGHT = 1u << 11
};

enum {
    PR_AXIS_MOVE_X = 0,   /* -1..1 */
    PR_AXIS_MOVE_Y = 1,
    PR_AXIS_CAM_X  = 2,
    PR_AXIS_CAM_Y  = 3,
    PR_AXIS_MAX    = 4
};

typedef struct pr_input_state {
    uint32_t buttons;        /* máscara PR_BTN_* */
    float    axes[PR_AXIS_MAX];
    float    trigger_lt;     /* 0..1 */
    float    trigger_rt;     /* 0..1 */
} pr_input_state;

void pr_input_clear(pr_input_state* st);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_INPUT_H */
