/**
 * Odograph - the exposure report.
 *
 * The verdict, the arithmetic behind it, and an honest page about what can
 * and cannot be done about it.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/od_expose.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ExposePageVerdict = 0,
    ExposePageNumbers,
    ExposePageMeaning,
    ExposePageCount,
} ExposePage;

typedef struct {
    OdExposure exposure;
    /** Set when the report covers only the sensors in your garage. */
    bool mine_only;
    bool garage_available;
    uint32_t frame;
} ExposeUi;

typedef enum {
    ExposeEventToggleScope, /* OK: whole sweep vs. just my car */
} ExposeViewEvent;

typedef void (*ExposeViewEventCallback)(void* context, ExposeViewEvent event);

typedef struct ExposeView ExposeView;

ExposeView* expose_view_alloc(void);
void expose_view_free(ExposeView* v);
View* expose_view_get_view(ExposeView* v);
void expose_view_set_event_callback(ExposeView* v, ExposeViewEventCallback cb, void* context);
void expose_view_update(ExposeView* v, const ExposeUi* ui);
void expose_view_reset(ExposeView* v);

#ifdef __cplusplus
}
#endif
