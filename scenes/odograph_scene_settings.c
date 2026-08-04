#include "../odograph_i.h"

/* Order must match the add order below. */
typedef enum {
    SettingsItemBand,
    SettingsItemProfile,
    SettingsItemUnits,
    SettingsItemTemp,
    SettingsItemConfirm,
    SettingsItemSound,
    SettingsItemLed,
} SettingsItem;

static const char* const confirm_names[OdConfirmCount] = {"1 packet", "2 packets", "3 packets"};
static const char* const units_names[OdUnitsCount] = {"kPa", "PSI", "bar"};

static void settings_changed(OdographApp* app) {
    odograph_apply_settings(app);
    odograph_save_settings(app);
}

static void band_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.band_index = index;
    variable_item_set_current_value_text(item, od_bands[index].label);
    settings_changed(app);
}

static void profile_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.profile = index;
    variable_item_set_current_value_text(item, od_profile_names[index]);
    settings_changed(app);
}

static void units_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.units = index;
    variable_item_set_current_value_text(item, units_names[index]);
    settings_changed(app);
}

static void temp_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.temp_f = index != 0;
    variable_item_set_current_value_text(item, index ? "F" : "C");
    settings_changed(app);
}

static void confirm_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.confirm = index;
    variable_item_set_current_value_text(item, confirm_names[index]);
    settings_changed(app);
}

static void sound_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.sound = index != 0;
    variable_item_set_current_value_text(item, index ? "On" : "Off");
    settings_changed(app);
}

static void led_cb(VariableItem* item) {
    OdographApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.led = index != 0;
    variable_item_set_current_value_text(item, index ? "On" : "Off");
    settings_changed(app);
}

void odograph_scene_settings_on_enter(void* context) {
    OdographApp* app = context;
    VariableItemList* list = app->var_item_list;
    VariableItem* item;

    variable_item_list_reset(list);

    item = variable_item_list_add(list, "Band", OD_BAND_COUNT, band_cb, app);
    variable_item_set_current_value_index(item, app->settings.band_index);
    variable_item_set_current_value_text(item, od_bands[app->settings.band_index].label);

    item = variable_item_list_add(list, "Listen for", OdProfileCount, profile_cb, app);
    variable_item_set_current_value_index(item, app->settings.profile);
    variable_item_set_current_value_text(item, od_profile_names[app->settings.profile]);

    item = variable_item_list_add(list, "Pressure", OdUnitsCount, units_cb, app);
    variable_item_set_current_value_index(item, app->settings.units);
    variable_item_set_current_value_text(item, units_names[app->settings.units]);

    item = variable_item_list_add(list, "Temperature", 2, temp_cb, app);
    variable_item_set_current_value_index(item, app->settings.temp_f ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.temp_f ? "F" : "C");

    item = variable_item_list_add(list, "Confirm with", OdConfirmCount, confirm_cb, app);
    variable_item_set_current_value_index(item, app->settings.confirm);
    variable_item_set_current_value_text(item, confirm_names[app->settings.confirm]);

    item = variable_item_list_add(list, "Sound", 2, sound_cb, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.sound ? "On" : "Off");

    item = variable_item_list_add(list, "LED", 2, led_cb, app);
    variable_item_set_current_value_index(item, app->settings.led ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.led ? "On" : "Off");

    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, OdographSceneSettings));

    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewSettings);
}

bool odograph_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void odograph_scene_settings_on_exit(void* context) {
    OdographApp* app = context;
    scene_manager_set_scene_state(
        app->scene_manager,
        OdographSceneSettings,
        variable_item_list_get_selected_item_index(app->var_item_list));
    variable_item_list_reset(app->var_item_list);
}
