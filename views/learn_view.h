/**
 * Odograph - how tyre-pressure tracking actually works.
 *
 * Five animated frames that build the argument end to end: where the radio
 * is, what it says, how two receivers turn that into a movement record, why
 * it cannot be switched off, and what is actually left to do about it.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LEARN_FRAMES 5

typedef struct LearnView LearnView;

LearnView* learn_view_alloc(void);
void learn_view_free(LearnView* v);
View* learn_view_get_view(LearnView* v);

/** Advance the animation. Call from the app tick. */
void learn_view_tick(LearnView* v);

/** Back to the first frame - call when the screen is opened. */
void learn_view_reset(LearnView* v);

#ifdef __cplusplus
}
#endif
