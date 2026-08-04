#pragma once

#include <gui/scene_manager.h>

// Generate the scene id enum
#define ADD_SCENE(prefix, name, id) OdographScene##id,
typedef enum {
#include "odograph_scene_config.h"
    OdographSceneNum,
} OdographScene;
#undef ADD_SCENE

extern const SceneManagerHandlers odograph_scene_handlers;

// Generate scene handler prototypes
#define ADD_SCENE(prefix, name, id)                                                \
    void prefix##_scene_##name##_on_enter(void* context);                          \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "odograph_scene_config.h"
#undef ADD_SCENE
