#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include "subghz/subghz_i.h"
#include "lib/flipper_format/flipper_format_i.h"
#include <lib/subghz/subghz_file_encoder_worker.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_tx_rx_worker.h>

#include <stdint.h>
#include <stdio.h>

#include <string.h>

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} PluginEvent;

typedef struct {
    int x;
    int y;
} PluginState;

static void render_callback(Canvas* const canvas, void* ctx) {
    const PluginState* plugin_state = acquire_mutex((ValueMutex*)ctx, 25);
    if(plugin_state == NULL) {
        return;
    }
    // border around the edge of the screen
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, plugin_state->x, plugin_state->y, AlignRight, AlignBottom, "Hello World");

    release_mutex((ValueMutex*)ctx, plugin_state);
}

static void input_callback(InputEvent* input_event, osMessageQueueId_t event_queue) {
    furi_assert(event_queue);

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    osMessageQueuePut(event_queue, &event, 0, osWaitForever);
}

static void hello_world_state_init(PluginState* const plugin_state) {
    plugin_state->x = 50;
    plugin_state->y = 30;
}



void subghz_command_tx() {
    uint32_t frequency = 433920000;
    uint32_t key = 0x0074BADE;
    uint32_t repeat = 10;

    printf(
        "Transmitting at %lu, key %lx, repeat %lu. Press CTRL+C to stop\r\n",
        frequency,
        key,
        repeat);

    string_t flipper_format_string;
    string_init_printf(
        flipper_format_string,
        "Protocol: Princeton\n"
        "Bit: 24\n"
        "Key: 00 00 00 00 00 %X %X %X\n"
        "TE: 403\n"
        "Repeat: %d\n",
        (uint8_t)((key >> 16) & 0xFF),
        (uint8_t)((key >> 8) & 0xFF),
        (uint8_t)(key & 0xFF),
        repeat);
    FlipperFormat* flipper_format = flipper_format_string_alloc();
    Stream* stream = flipper_format_get_raw_stream(flipper_format);
    stream_clean(stream);
    stream_write_cstring(stream, string_get_cstr(flipper_format_string));

    SubGhzEnvironment* environment = subghz_environment_alloc();

    SubGhzTransmitter* transmitter = subghz_transmitter_alloc_init(environment, "Princeton");
    subghz_transmitter_deserialize(transmitter, flipper_format);

    furi_hal_subghz_reset();
    furi_hal_subghz_load_preset(FuriHalSubGhzPresetOok650Async);
    frequency = furi_hal_subghz_set_frequency_and_path(frequency);

    furi_hal_power_suppress_charge_enter();

    furi_hal_subghz_start_async_tx(subghz_transmitter_yield, transmitter);

    while(!(furi_hal_subghz_is_async_tx_complete())) {
        printf(".");
        fflush(stdout);
        osDelay(333);
    }
    furi_hal_subghz_stop_async_tx();
    furi_hal_subghz_sleep();

    furi_hal_power_suppress_charge_exit();

    flipper_format_free(flipper_format);
    subghz_transmitter_free(transmitter);
    subghz_environment_free(environment);
}

int32_t hello_world_app(void* p) {
    osMessageQueueId_t event_queue = osMessageQueueNew(8, sizeof(PluginEvent), NULL);

    PluginState* plugin_state = malloc(sizeof(PluginState));
    hello_world_state_init(plugin_state);
    ValueMutex state_mutex;
    if (!init_mutex(&state_mutex, plugin_state, sizeof(PluginState))) {
        FURI_LOG_E("Hello_world", "cannot create mutex\r\n");
        free(plugin_state);
        return 255;
    }

    // Set system callbacks
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &state_mutex);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    // Open GUI and register view_port
    Gui* gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);


    PluginEvent event;
    for(bool processing = true; processing;) {
        osStatus_t event_status = osMessageQueueGet(event_queue, &event, NULL, 100);
        PluginState* plugin_state = (PluginState*)acquire_mutex_block(&state_mutex);

        if(event_status == osOK) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
                    	subghz_command_tx("up");
                    	//plugin_state->y--;
                        break;
                    case InputKeyDown:
                    //	subghz_cli_command_tx("down");
                    	//plugin_state->y++;
                        break;
                    case InputKeyRight:
                  //  	subghz_cli_command_tx("right");
                    	//plugin_state->x++;
                        break;
                    case InputKeyLeft:
                  //  	subghz_command_tx("left");
                    	//plugin_state->x--;
                        break;
                    case InputKeyOk:
                    //	subghz_cli_command_tx("ok");
                    	//plugin_state->x++;
                    	break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    }
                }
            }
        } else {
            FURI_LOG_D("Hello_world", "osMessageQueue: event timeout");
            // event timeout
        }

        view_port_update(view_port);
        release_mutex(&state_mutex, plugin_state);
    }

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close("gui");
    view_port_free(view_port);
    osMessageQueueDelete(event_queue);


    return 0;
}

