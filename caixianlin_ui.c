#include "caixianlin_ui.h"
#include "caixianlin_storage.h"
#include "caixianlin_radio.h"
#include "caixianlin_protocol.h"

// Forward declarations for internal functions
static void handle_setup_input(CaixianlinRemoteApp* app, InputEvent* event);

// Write a value stepped by a held Left/Right if its release was not seen yet
void caixianlin_ui_flush_setup(CaixianlinRemoteApp* app) {
    if(app->setup_dirty) {
        app->setup_dirty = false;
        caixianlin_storage_save(app);
    }
}
static void handle_listen_input(CaixianlinRemoteApp* app, InputEvent* event);
static void handle_main_input(CaixianlinRemoteApp* app, InputEvent* event);

// Initialize UI
void caixianlin_ui_init(CaixianlinRemoteApp* app) {
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, caixianlin_ui_draw, app);
    view_port_input_callback_set(app->view_port, caixianlin_ui_input, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
}

// Cleanup UI
void caixianlin_ui_deinit(CaixianlinRemoteApp* app) {
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
}

// Helper function: Draw horizontal progress bar
static void draw_progress_bar(
    Canvas* canvas,
    int x,
    int y,
    int width,
    int height,
    uint8_t value,
    bool show_outline) {
    if(show_outline) {
        canvas_draw_frame(canvas, x, y, width, height);
    }

    // Calculate filled width (value is 0-100)
    int filled_width = (value * (width - 2)) / 100;
    if(filled_width > 0) {
        canvas_draw_box(canvas, x + 1, y + 1, filled_width, height - 2);
    }
}

// Helper function: Draw menu item box with selection state
static void draw_menu_item(Canvas* canvas, const char* text, int y, bool selected) {
    int x = 4;
    int width = 120;
    int height = 13;

    canvas_draw_rframe(canvas, x, y, width, height, 2);
    if(selected) {
        // Draw double border for selected item (bold effect)
        canvas_draw_rframe(canvas, x + 1, y + 1, width - 2, height - 2, 1);
    }

    // Draw text with padding
    canvas_draw_str(canvas, x + 4, y + 10, text);
}

// Helper function: Draw animated spinner
static void draw_spinner(Canvas* canvas, int x, int y, uint32_t frame_counter) {
    uint32_t step = (frame_counter / 2) % 4;

    char* ch;
    switch(step) {
    case 0:
        ch = "-";
        break;
    case 1:
        ch = "\\";
        break;
    case 2:
        ch = "|";
        break;
    case 3:
        ch = "/";
        break;
    default:
        ch = "?";
        break;
    }

    canvas_draw_str(canvas, x, y, ch);
}

// Draw callback
void caixianlin_ui_draw(Canvas* canvas, void* ctx) {
    CaixianlinRemoteApp* app = ctx;

    canvas_clear(canvas);

    // Increment frame counter for animations
    app->frame_counter++;

    if(app->screen == ScreenSetup) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 2, 10, "Setup");

        canvas_set_font(canvas, FontSecondary);

        // Station ID
        char buf[32];
        if(app->editing_station_id) {
            // Draw menu item box around Station ID
            // draw_menu_item(canvas, "", 17, true);

            snprintf(buf, sizeof(buf), "Station ID:");
            canvas_draw_str(canvas, 8, 34, buf);

            // Draw each digit, highlight selected
            for(int i = 0; i < 5; i++) {
                char digit[2] = {0};
                digit[0] = '0' + caixianlin_protocol_get_station_digit(app->station_id, i);
                int x = 72 + i * 8;

                if(i == app->station_id_digit) {
                    canvas_draw_rbox(canvas, x - 2, 25, 9, 12, 2);
                    canvas_set_color(canvas, ColorWhite);
                }
                canvas_draw_str(canvas, x, 34, digit);
                canvas_set_color(canvas, ColorBlack);
            }
        } else {
            // Menu rows; the list scrolls so the selected item is always visible
            static const int slot_y[SETUP_VISIBLE_ITEMS] = {13, 25, 37, 49}; // rows share a border line
            int first = app->setup_first_visible;
            for(int slot = 0; slot < SETUP_VISIBLE_ITEMS; slot++) {
                int item = first + slot;
                if(item >= SetupItemCount) break;
                switch(item) {
                case SetupItemStationId:
                    snprintf(buf, sizeof(buf), "Station ID: %d", app->station_id);
                    break;
                case SetupItemChannel:
                    snprintf(buf, sizeof(buf), "Channel: %d", app->channel);
                    break;
                case SetupItemShockMax:
                    if(app->shock_max_s) {
                        snprintf(buf, sizeof(buf), "Shock max: %us", (unsigned)app->shock_max_s);
                    } else {
                        snprintf(buf, sizeof(buf), "Shock max: off");
                    }
                    break;
                case SetupItemVibration:
                    if(app->vibro_level) {
                        snprintf(
                            buf,
                            sizeof(buf),
                            "Vibration: %u%%",
                            (unsigned)app->vibro_level * 100 / VIBRO_LEVEL_MAX);
                    } else {
                        snprintf(buf, sizeof(buf), "Vibration: off");
                    }
                    break;
                case SetupItemListen:
                    snprintf(buf, sizeof(buf), "Listen for Remote");
                    break;
                default:
                    snprintf(buf, sizeof(buf), "Done");
                    break;
                }
                draw_menu_item(canvas, buf, slot_y[slot], app->setup_selected == item);
            }

            // Scroll bar on the right edge
            const int track_y = 13, track_h = 49; // spans the four rows
            int thumb_h = track_h * SETUP_VISIBLE_ITEMS / SetupItemCount;
            int thumb_y = track_y + (track_h - thumb_h) * first / (SetupItemCount - SETUP_VISIBLE_ITEMS);
            canvas_draw_line(canvas, 126, track_y, 126, track_y + track_h - 1);
            canvas_draw_box(canvas, 125, thumb_y, 3, thumb_h);
        }

    } else if(app->screen == ScreenListen) {
        canvas_set_font(canvas, FontPrimary);

        // Draw animated listening indicator
        draw_spinner(canvas, 6, 12, app->frame_counter);
        canvas_draw_str(canvas, 16, 12, "Listening");

        char buf[32];
        canvas_set_font(canvas, FontSecondary);

        if(app->rx_capture.capture_valid) {
            // Show the timings measured from the remote; Apply makes them the TX timings
            const CaixianlinTiming* t = &app->rx_capture.captured_timing;
            snprintf(
                buf,
                sizeof(buf),
                "Sync %u/%u End %u",
                (unsigned)t->sync_high_us,
                (unsigned)t->sync_low_us,
                (unsigned)t->end_bits);
            canvas_draw_str(canvas, 2, 25, buf);
            snprintf(
                buf,
                sizeof(buf),
                "Bit %u/%u %u/%u",
                (unsigned)t->one_high_us,
                (unsigned)t->one_low_us,
                (unsigned)t->zero_high_us,
                (unsigned)t->zero_low_us);
            canvas_draw_str(canvas, 2, 34, buf);
        } else {
            // Draw progress bar for buffer
            canvas_draw_str(canvas, 2, 25, "Buffer:");
            int buffer_percent = (app->rx_capture.work_buffer_len * 100) / WORK_BUFFER_SIZE;
            if(buffer_percent > 100) buffer_percent = 100;
            draw_progress_bar(canvas, 42, 18, 80, 6, buffer_percent, true);

            // Draw progress bar for queue
            canvas_draw_str(canvas, 2, 34, "Queue:");
            size_t queue_available =
                furi_stream_buffer_bytes_available(app->rx_capture.stream_buffer);
            queue_available /= sizeof(int32_t);
            int queue_percent = (queue_available * 100) / RX_BUFFER_SIZE;
            if(queue_percent > 100) queue_percent = 100;
            draw_progress_bar(canvas, 42, 27, 80, 6, queue_percent, true);
        }

        // Show last decoded message if available
        if(app->rx_capture.capture_valid) {
            // Draw box around captured result
            canvas_draw_rframe(canvas, 2, 39, 124, 13, 2);

            snprintf(
                buf,
                sizeof(buf),
                "ID=%d | CH=%d",
                app->rx_capture.captured_station_id,
                app->rx_capture.captured_channel);
            canvas_draw_str(canvas, 6, 49, buf);

            canvas_draw_str(canvas, 2, 62, "OK=Apply Back=Stop V=Rst");
        } else {
            canvas_draw_str(canvas, 2, 50, "Waiting for signal...");
            canvas_draw_str(canvas, 2, 62, "Back=Stop");
        }

    } else { // ScreenMain
        canvas_set_font(canvas, FontPrimary);

        char buf[32];
        snprintf(buf, sizeof(buf), "ID:%d | CH:%d", app->station_id, app->channel);
        canvas_draw_str(canvas, 2, 12, buf);

        // Draw header separator line
        canvas_draw_line(canvas, 0, 15, 127, 15);

        // Mode
        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "%s", mode_names[app->mode]);
        int mode_width = canvas_string_width(canvas, buf);
        int mode_x = (128 - mode_width) / 2;
        canvas_draw_str(canvas, mode_x, 29, buf);

        // Draw rounded box around mode
        canvas_draw_rframe(canvas, mode_x - 4, 19, mode_width + 8, 12, 2);

        // Draw arrows
        canvas_set_font(canvas, FontSecondary);
        int arrow_width = canvas_string_width(canvas, "<");
        canvas_draw_str(canvas, mode_x - 7 - arrow_width, 29, "<");
        canvas_draw_str(canvas, mode_x + mode_width + 7, 29, ">");

        // Strength
        if(app->mode != MODE_BEEP) {
            canvas_set_font(canvas, FontSecondary);
            snprintf(buf, sizeof(buf), "Strength: %d", app->strength);
            int str_width = canvas_string_width(canvas, buf);
            canvas_draw_str(canvas, (128 - str_width) / 2, 42, buf);

            // Draw strength progress bar
            draw_progress_bar(canvas, 13, 45, 102, 6, app->strength, true);
        }

        // Status
        char* status;
        if(app->shock_timed_out) {
            canvas_set_font(canvas, FontSecondary);
            status = "[ Shock timed out ]";
        } else if(app->is_transmitting) {
            canvas_set_font(canvas, FontPrimary);
            status = "[ Transmitting! ]";
        } else if(app->tx_failed) {
            canvas_set_font(canvas, FontPrimary);
            status = "[ TX failed! ]";
        } else {
            canvas_set_font(canvas, FontSecondary);
            status = "[ Hold OK to transmit ]";
        }
        int str_width = canvas_string_width(canvas, status);
        canvas_draw_str(canvas, (128 - str_width) / 2, 62, status);
    }
}

// Input callback
void caixianlin_ui_input(InputEvent* event, void* ctx) {
    CaixianlinRemoteApp* app = ctx;
    furi_message_queue_put(app->event_queue, event, FuriWaitForever);
}

// Handle setup screen input
static void handle_setup_input(CaixianlinRemoteApp* app, InputEvent* event) {
    if(app->editing_station_id) {
        // Station ID digit editing mode
        if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
            if(event->key == InputKeyUp) {
                uint8_t digit =
                    caixianlin_protocol_get_station_digit(app->station_id, app->station_id_digit);
                digit = (digit + 1) % 10;
                app->station_id = caixianlin_protocol_set_station_digit(
                    app->station_id, app->station_id_digit, digit);
            } else if(event->key == InputKeyDown) {
                uint8_t digit =
                    caixianlin_protocol_get_station_digit(app->station_id, app->station_id_digit);
                digit = (digit + 9) % 10;
                app->station_id = caixianlin_protocol_set_station_digit(
                    app->station_id, app->station_id_digit, digit);
            } else if(event->key == InputKeyRight) {
                if(app->station_id_digit < 4) app->station_id_digit++;
            } else if(event->key == InputKeyLeft) {
                if(app->station_id_digit > 0) app->station_id_digit--;
            } else if(event->key == InputKeyOk) {
                app->editing_station_id = false;
                caixianlin_storage_save(app);
            } else if(event->key == InputKeyBack) {
                // Cancel: put back the value from before the edit
                app->station_id = app->station_id_backup;
                app->editing_station_id = false;
            }
        }
    } else {
        // Normal setup navigation
        if(event->type == InputTypeLong && event->key == InputKeyOk &&
           app->setup_selected == SetupItemListen) {
            // Hold OK on "Listen for Remote": forget learned timings, back to defaults
            caixianlin_timing_set_default(&app->timing);
            caixianlin_storage_save(app);
            notification_message(app->notifications, &sequence_success);
            return;
        }
        bool adjust_key = event->key == InputKeyLeft || event->key == InputKeyRight;
        if(event->type == InputTypeRelease && adjust_key && app->setup_dirty) {
            // A held Left/Right changed values step by step; write them once
            app->setup_dirty = false;
            caixianlin_storage_save(app);
            return;
        }
        bool adjust_repeat = event->type == InputTypeRepeat && adjust_key;
        if(event->type == InputTypeShort || adjust_repeat) {
            if(event->key == InputKeyUp) {
                if(app->setup_selected > 0) app->setup_selected--;
            } else if(event->key == InputKeyDown) {
                if(app->setup_selected < SetupItemCount - 1) app->setup_selected++;
            } else if(event->key == InputKeyLeft || event->key == InputKeyRight) {
                int step = (event->key == InputKeyRight) ? 1 : -1;
                bool changed = false;
                if(app->setup_selected == SetupItemChannel) {
                    int channel = app->channel + step;
                    if(channel < 0) channel = 0;
                    if(channel > 2) channel = 2; // channels 0-2 per the protocol
                    if(channel != app->channel) { // also steps a captured 3..15 back into range
                        app->channel = (uint8_t)channel;
                        changed = true;
                    }
                } else if(app->setup_selected == SetupItemShockMax) {
                    int seconds = app->shock_max_s + step;
                    if(seconds >= 0 && seconds <= SHOCK_MAX_S_LIMIT) {
                        app->shock_max_s = (uint8_t)seconds;
                        changed = true;
                    }
                } else if(app->setup_selected == SetupItemVibration) {
                    int level = app->vibro_level + step;
                    if(level >= 0 && level <= VIBRO_LEVEL_MAX) {
                        app->vibro_level = (uint8_t)level;
                        changed = true;
                    }
                }
                if(changed) {
                    if(adjust_repeat) {
                        app->setup_dirty = true; // saved once on release
                    } else {
                        caixianlin_storage_save(app);
                    }
                }
            } else if(event->key == InputKeyOk) {
                if(app->setup_selected == SetupItemStationId) {
                    app->station_id_backup = app->station_id;
                    app->editing_station_id = true;
                    app->station_id_digit = 0;
                } else if(app->setup_selected == SetupItemListen) {
                    caixianlin_ui_flush_setup(app);
                    app->screen = ScreenListen;
                    caixianlin_radio_start_rx(app);
                } else if(app->setup_selected == SetupItemDone) {
                    caixianlin_ui_flush_setup(app);
                    app->screen = ScreenMain;
                }
            } else if(event->key == InputKeyBack) {
                caixianlin_ui_flush_setup(app);
                app->running = false;
            }

            // Keep the selected item on screen
            if(app->setup_selected < app->setup_first_visible) {
                app->setup_first_visible = app->setup_selected;
            } else if(app->setup_selected >= app->setup_first_visible + SETUP_VISIBLE_ITEMS) {
                app->setup_first_visible = app->setup_selected - (SETUP_VISIBLE_ITEMS - 1);
            }
        }
    }
}

// Handle listen screen input
static void handle_listen_input(CaixianlinRemoteApp* app, InputEvent* event) {
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
            // Apply captured station ID and channel if valid
            if(app->rx_capture.capture_valid) {
                app->station_id = app->rx_capture.captured_station_id;
                app->channel = app->rx_capture.captured_channel;
                // Transmit with the timings this remote uses
                if(caixianlin_timing_is_valid(&app->rx_capture.captured_timing)) {
                    app->timing = app->rx_capture.captured_timing;
                }
                caixianlin_storage_save(app);

                // Stop listening and return to setup
                if(app->is_listening) {
                    caixianlin_radio_stop_rx(app);
                }
                app->screen = ScreenSetup;
            }
        } else if(event->key == InputKeyBack) {
            // Stop listening and return to setup
            if(app->is_listening) {
                caixianlin_radio_stop_rx(app);
            }
            app->screen = ScreenSetup;
        } else if(event->key == InputKeyDown) {
            // Reset the capture flag
            app->rx_capture.capture_valid = false;
        }
    }
}

// Handle main screen input
static void handle_main_input(CaixianlinRemoteApp* app, InputEvent* event) {
    if(event->key == InputKeyOk) {
        if(event->type == InputTypePress) {
            caixianlin_radio_start_tx(app);
        } else if(event->type == InputTypeRelease) {
            caixianlin_radio_stop_tx(app);
            app->shock_timed_out = false;
        }
    } else if(event->type == InputTypeShort) {
        if(event->key == InputKeyRight) {
            if(app->mode < 3) {
                app->mode++;
            } else {
                app->mode = 1;
            }
        } else if(event->key == InputKeyLeft) {
            if(app->mode > 1) {
                app->mode--;
            } else {
                app->mode = 3;
            }
        } else if(event->key == InputKeyUp && app->mode != MODE_BEEP) {
            if(app->strength < MAX_STRENGTH) app->strength++;
        } else if(event->key == InputKeyDown && app->mode != MODE_BEEP) {
            if(app->strength > 0) app->strength--;
        }
    } else if(event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp && app->mode != MODE_BEEP) {
            if(app->strength + 10 < MAX_STRENGTH) {
                app->strength += 10;
            } else {
                app->strength = MAX_STRENGTH;
            }
        } else if(event->key == InputKeyDown && app->mode != MODE_BEEP) {
            if(app->strength > 10) {
                app->strength -= 10;
            } else {
                app->strength = 0;
            }
        }
    }

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeLong) {
            if(app->is_transmitting) caixianlin_radio_stop_tx(app);
            app->shock_timed_out = false;
            app->screen = ScreenSetup;
        } else if(event->type == InputTypeShort) {
            if(app->is_transmitting) caixianlin_radio_stop_tx(app);
            app->shock_timed_out = false;
            app->running = false;
        }
    }
}

// Process input event for current screen
void caixianlin_ui_handle_event(CaixianlinRemoteApp* app, InputEvent* event) {
    switch(app->screen) {
    case ScreenSetup:
        handle_setup_input(app, event);
        break;
    case ScreenListen:
        handle_listen_input(app, event);
        break;
    case ScreenMain:
        handle_main_input(app, event);
        break;
    }
}
