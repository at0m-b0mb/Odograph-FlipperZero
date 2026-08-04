#include "../odograph_i.h"

void odograph_scene_about_on_enter(void* context) {
    OdographApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Odograph " ODOGRAPH_VERSION "\e#\n"
        "A licence plate you cannot cover.\n"
        "\n"
        "\e#What it does\e#\n"
        "Every tyre on a modern car carries a "
        "pressure sensor with a fixed serial "
        "number, broadcast in the clear on "
        "315 or 433.92 MHz. Odograph listens "
        "for those broadcasts, decodes the "
        "serials, and shows you how "
        "identifying they are.\n"
        "\n"
        "\e#Controls\e#\n"
        "Listen: Up/Down pick a sensor, OK "
        "opens it, Right jumps to the report, "
        "Left steps the band.\n"
        "Sensor: Left/Right page through, OK "
        "saves it to the garage.\n"
        "Report: OK switches between "
        "everything heard and your car only.\n"
        "\n"
        "\e#Decodes\e#\n"
        "Ford, Renault, Citroen/Peugeot, "
        "Hyundai/Kia (VDO), Toyota and "
        "Schrader. Anything else that is "
        "clearly a tyre sensor is reported as "
        "an unrecognised fingerprint, twice "
        "confirmed before it is shown.\n"
        "\n"
        "\e#Nothing yet?\e#\n"
        "Sensors transmit when a wheel turns, "
        "and roughly once a minute at rest. "
        "Try the other band, try FSK+ instead "
        "of FSK, and give it a minute beside "
        "a moving car.\n"
        "\n"
        "\e#Listen only\e#\n"
        "Odograph has no transmit path. It "
        "never replays, spoofs or writes to "
        "anything, and it cannot change a "
        "reading on anyone's dashboard.\n"
        "\n"
        "\e#Use it on your own vehicle\e#\n"
        "or with permission. Tracking someone "
        "else's car is illegal in most places, "
        "and this app exists to show you that "
        "the option is sitting there in the "
        "open, not to hand it to anyone.\n"
        "\n"
        "MIT licensed.\n"
        "github.com/at0m-b0mb/Odograph-FlipperZero\n"
        "by at0m-b0mb\n");

    view_dispatcher_switch_to_view(app->view_dispatcher, OdographViewText);
}

bool odograph_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void odograph_scene_about_on_exit(void* context) {
    OdographApp* app = context;
    widget_reset(app->widget);
}
