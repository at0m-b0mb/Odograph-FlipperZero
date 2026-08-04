#include "../odograph_i.h"

/**
 * The vehicles Odograph has been told to remember. This is where the point
 * stops being theoretical: the serials here were saved on some earlier day,
 * and the device still recognises them.
 */

#define GARAGE_INDEX_FORGET_ALL 200

static void odograph_scene_garage_submenu_cb(void* context, uint32_t index) {
    OdographApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/** "3 days ago" / "4h ago" / "just now", from two unix timestamps. */
static void garage_when(char* out, size_t size, uint32_t then) {
    if(then == 0) {
        snprintf(out, size, "unknown");
        return;
    }

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint32_t now = datetime_datetime_to_timestamp(&dt);
    if(now <= then) {
        snprintf(out, size, "just now");
        return;
    }

    uint32_t sec = now - then;
    if(sec < 3600) {
        snprintf(out, size, "%lum ago", (unsigned long)(sec / 60));
    } else if(sec < 86400) {
        snprintf(out, size, "%luh ago", (unsigned long)(sec / 3600));
    } else {
        unsigned long days = (unsigned long)(sec / 86400);
        snprintf(out, size, "%lu day%s ago", days, days == 1 ? "" : "s");
    }
}

void odograph_scene_garage_on_enter(void* context) {
    OdographApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Garage");

    uint8_t count = od_store_garage_count(app->store);
    for(uint8_t i = 0; i < count; i++) {
        OdGarageEntry e;
        if(!od_store_garage_get(app->store, i, &e)) continue;

        char id[10];
        od_ui_id(id, sizeof(id), e.id, od_tpms_proto_id_bits(e.proto));

        char when[16];
        garage_when(when, sizeof(when), e.last_seen);

        char item[40];
        snprintf(item, sizeof(item), "%s %s", id, when);
        submenu_add_item(submenu, item, i, odograph_scene_garage_submenu_cb, app);
    }

    if(count == 0) {
        submenu_add_item(
            submenu, "Nothing saved yet", GARAGE_INDEX_FORGET_ALL + 1, NULL, app);
    } else {
        submenu_add_item(
            submenu, "Forget all", GARAGE_INDEX_FORGET_ALL, odograph_scene_garage_submenu_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewSubmenu);
}

bool odograph_scene_garage_on_event(void* context, SceneManagerEvent event) {
    OdographApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GARAGE_INDEX_FORGET_ALL) {
            od_store_garage_clear(app->store);
            odograph_notify_click(app);
            /* Rebuild in place so the list reflects the change immediately. */
            odograph_scene_garage_on_enter(app);
            consumed = true;
        } else if(event.event < OD_GARAGE_MAX) {
            OdGarageEntry e;
            if(od_store_garage_get(app->store, (uint8_t)event.event, &e)) {
                /*
                 * Prefer the live row if this serial is on the air right now;
                 * otherwise show what the card remembers, with no live
                 * telemetry to invent.
                 */
                memset(&app->selected, 0, sizeof(app->selected));
                app->selected.id = e.id;
                app->selected.proto = e.proto;
                app->selected.id_bits = od_tpms_proto_id_bits(e.proto);
                app->selected.known = true;
                app->selected.known_since = e.first_seen;
                app->selected.first_tick = furi_get_tick();
                app->selected.last_tick = furi_get_tick();

                uint8_t live = od_store_count(app->store);
                for(uint8_t i = 0; i < live; i++) {
                    OdSensor s;
                    if(!od_store_get(app->store, i, &s)) continue;
                    if(s.id == e.id && s.proto == e.proto) {
                        app->selected = s;
                        break;
                    }
                }

                app->selected_valid = true;
                app->from_garage = true;
                scene_manager_next_scene(app->scene_manager, OdographSceneSensor);
            }
            consumed = true;
        }
    }

    return consumed;
}

void odograph_scene_garage_on_exit(void* context) {
    OdographApp* app = context;
    submenu_reset(app->submenu);
}
