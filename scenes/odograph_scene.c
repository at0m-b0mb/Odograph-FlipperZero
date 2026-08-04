#include "../odograph_i.h"

// Generate on_enter handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const odograph_scene_on_enter_handlers[])(void*) = {
#include "odograph_scene_config.h"
};
#undef ADD_SCENE

// Generate on_event handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const odograph_scene_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "odograph_scene_config.h"
};
#undef ADD_SCENE

// Generate on_exit handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const odograph_scene_on_exit_handlers[])(void* context) = {
#include "odograph_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers odograph_scene_handlers = {
    .on_enter_handlers = odograph_scene_on_enter_handlers,
    .on_event_handlers = odograph_scene_on_event_handlers,
    .on_exit_handlers = odograph_scene_on_exit_handlers,
    .scene_num = OdographSceneNum,
};
