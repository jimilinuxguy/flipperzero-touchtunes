#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <flipper_format/flipper_format_i.h>

#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/subghz_file_encoder_worker.h>
//#include <notification/notification_messages.h>

#define TAG "JukeBox"

//NotificationApp* notification;

typedef struct {
	bool press[6];
	uint16_t up;
	uint16_t down;
	uint16_t left;
	uint16_t right;
	uint16_t ok;
	uint16_t back;
} JukeboxAppState;

JukeboxAppState *state;

static void jukebox_reset_state(JukeboxAppState *state) {
	state->left = 0;
	state->right = 0;
	state->up = 0;
	state->down = 0;
	state->ok = 0;
}

//static void notify_blink_send() {
//    notification_message(notification, &sequence_blink_magenta_10);
//}

static void jukebox_send_signal(string_t signal) {
	uint32_t frequency = 433920000;
	uint32_t repeat = 1;
	FURI_LOG_E(TAG, "file to send: %s", string_get_cstr(signal));
	if (strlen(string_get_cstr(signal)) < 10) {
		return;
	}
	printf("Transmitting at %lu, repeat %lu. Press CTRL+C to stop\r\n",
			frequency, repeat);

	string_t flipper_format_string;
	string_init_printf(flipper_format_string, "%s", string_get_cstr(signal));

	FlipperFormat *flipper_format = flipper_format_string_alloc();
	Stream *stream = flipper_format_get_raw_stream(flipper_format);
	stream_clean(stream);
	stream_write_cstring(stream, string_get_cstr(flipper_format_string));

	SubGhzEnvironment *environment = subghz_environment_alloc();

	SubGhzTransmitter *transmitter = subghz_transmitter_alloc_init(environment,
			"RAW");
	subghz_transmitter_deserialize(transmitter, flipper_format);

	furi_hal_subghz_reset();
	furi_hal_subghz_load_preset(FuriHalSubGhzPresetOok270Async);
	frequency = furi_hal_subghz_set_frequency_and_path(transmitter->frequency);

	furi_hal_power_suppress_charge_enter();
//    notify_blink_send();
	furi_hal_subghz_start_async_tx(subghz_transmitter_yield, transmitter);

	while (!(furi_hal_subghz_is_async_tx_complete())) {
		printf(".");
		fflush(stdout);
		osDelay(333);
		jukebox_reset_state(state);
	}
	furi_hal_subghz_stop_async_tx();
	furi_hal_subghz_sleep();

	furi_hal_power_suppress_charge_exit();

	flipper_format_free(flipper_format);
	subghz_transmitter_free(transmitter);
	subghz_environment_free(environment);

}

static void jukebox_render_callback(Canvas *canvas, void *ctx) {
	state = (JukeboxAppState*) acquire_mutex((ValueMutex*) ctx, 25);
	canvas_clear(canvas);
	char strings[5][20];
	string_t signal;
	string_init(signal);
	sprintf(strings[0], "Ok: %s", "Pause");
	sprintf(strings[1], "L: %s", "Pwr");
	sprintf(strings[2], "R: %s", "P1");
	sprintf(strings[3], "U: %s", "P2");
	sprintf(strings[4], "D: %s", "OK");

	canvas_set_font(canvas, FontPrimary);
	canvas_draw_str(canvas, 0, 10, "TouchTunes");

	canvas_set_font(canvas, FontSecondary);
	canvas_draw_str(canvas, 0, 24, strings[1]);
	canvas_draw_str(canvas, 35, 24, strings[2]);
	canvas_draw_str(canvas, 0, 36, strings[3]);
	canvas_draw_str(canvas, 35, 36, strings[4]);
	canvas_draw_str(canvas, 0, 48, strings[0]);
//    canvas_draw_circle(canvas, 100, 26, 25);

	if (state->press[0]) {
		string_cat_printf(signal, "File_name: /any/subghz/%s.sub", "Pause");

		jukebox_send_signal(signal);
	}

	else if (state->press[1]) {
		string_cat_printf(signal, "File_name: /any/subghz/%s.sub", "P1");

		jukebox_send_signal(signal);
	} else if (state->press[2]) {
		string_cat_printf(signal, "File_name: /any/subghz/%s.sub", "OK");

		jukebox_send_signal(signal);
	} else if (state->press[3]) {
		string_cat_printf(signal, "File_name: /any/subghz/%s.sub", "On_Off");

		jukebox_send_signal(signal);
	}

	else if (state->press[4]) {
		string_cat_printf(signal, "File_name: /any/subghz/%s.sub", "P3_Skip");

		jukebox_send_signal(signal);
	}

	canvas_draw_str(canvas, 10, 63, "[back] - skip, hold to exit");

	release_mutex((ValueMutex*) ctx, state);
}

static void jukebox_input_callback(InputEvent *input_event, void *ctx) {
	osMessageQueueId_t event_queue = ctx;
	osMessageQueuePut(event_queue, input_event, 0, osWaitForever);
}

int32_t jukebox_app(void *p) {
	UNUSED(p);
	osMessageQueueId_t event_queue = osMessageQueueNew(32, sizeof(InputEvent),
			NULL);
	furi_check(event_queue);

	JukeboxAppState _state = { { false, false, false, false, false, false }, 0,
			0, 0, 0, 0, 0 };

	ValueMutex state_mutex;
	if (!init_mutex(&state_mutex, &_state, sizeof(JukeboxAppState))) {
		FURI_LOG_E(TAG, "cannot create mutex");
		return 0;
	}

	ViewPort *view_port = view_port_alloc();

	view_port_draw_callback_set(view_port, jukebox_render_callback,
			&state_mutex);
	view_port_input_callback_set(view_port, jukebox_input_callback,
			event_queue);

	// Open GUI and register view_port
	Gui *gui = furi_record_open("gui");
	gui_add_view_port(gui, view_port, GuiLayerFullscreen);

	InputEvent event;
	while (osMessageQueueGet(event_queue, &event, NULL, osWaitForever) == osOK) {
		JukeboxAppState *state = (JukeboxAppState*) acquire_mutex_block(
				&state_mutex);
		FURI_LOG_I(TAG, "key: %s type: %s", input_get_key_name(event.key),
				input_get_type_name(event.type));
		if (event.type == InputTypeLong) {
			if (event.key == InputKeyBack) {
				release_mutex(&state_mutex, state);
				break;
			}
		}
		if (event.type == InputTypePress || event.type == InputTypeRelease) {
			if (event.type == InputTypePress) {
				if (event.key == InputKeyRight) {
					state->press[0] = true;
				} else if (event.key == InputKeyLeft) {
					state->press[1] = true;
				} else if (event.key == InputKeyUp) {
					state->press[2] = true;
				} else if (event.key == InputKeyDown) {
					state->press[3] = true;
				} else if (event.key == InputKeyOk) {
					state->press[4] = true;
				} else if (event.key == InputKeyBack) {
					state->press[5] = true;
				}
			} else if (event.type == InputTypeRelease) {
				if (event.key == InputKeyRight) {
					state->press[0] = false;
				} else if (event.key == InputKeyLeft) {
					state->press[1] = false;
				} else if (event.key == InputKeyUp) {
					state->press[2] = false;
				} else if (event.key == InputKeyDown) {
					state->press[3] = false;
				} else if (event.key == InputKeyOk) {
					state->press[4] = false;
				} else if (event.key == InputKeyBack) {
					state->press[5] = false;
				}
			}

		}
		release_mutex(&state_mutex, state);
		view_port_update(view_port);
	}

	// remove & free all stuff created by app
	gui_remove_view_port(gui, view_port);
	view_port_free(view_port);
	osMessageQueueDelete(event_queue);
	delete_mutex(&state_mutex);

	furi_record_close("gui");

	return 0;
}
