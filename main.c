#include "dispenser.h"

static DispenserState current_state = STATE_WAIT_FOR_CALIBRATION;
static dispenser_data_t sys_data;
static uint32_t last_dispense_time = 0;
static bool is_lora_online = false;

static bool is_button_pressed(uint pin);
static void blink_led(int times, int delay_ms);
static void send_lora_safe(lora_msg_type_t type);
static void print_detailed_log(const char* power_status, const char* exception, bool pill_success);
static void system_init(void);
static void lora_init_and_join(void);
static void restore_state(void);

int main() {
    system_init();

    // Load Data from EEPROM
    if (!storage_load(&sys_data)) {
        storage_init_default(&sys_data);
    }
    lora_init_and_join();
    restore_state();

    if (current_state == STATE_WAIT_FOR_CALIBRATION) {
        printf("[READY] Waiting for button press (SW0 to Calibrate)...\n");
    }

    uint32_t last_blink_time = 0;
    bool led_state = false;

    while (true) {
        watchdog_update();// update the watchdog

        switch (current_state) {

            case STATE_WAIT_FOR_CALIBRATION: {
                // blink LED while waiting
                uint32_t now = to_ms_since_boot(get_absolute_time());
                if (now - last_blink_time > BLINK_INTERVAL_MS) {
                    led_state = !led_state;
                    gpio_put(LED_PIN, led_state);
                    last_blink_time = now;
                }

                if (is_button_pressed(SW_0_PIN)) {
                    gpio_put(LED_PIN, 0);
                    printf("[User] SW0 pressed - Starting calibration\n");
                    current_state = STATE_CALIBRATING;
                }
                break;
            }

            case STATE_CALIBRATING: {
                sys_data.is_rotating = true;
                storage_save(&sys_data);

                motor_calibrate();

                sys_data.is_rotating = false;
                sys_data.is_calibrated = 1;
                sys_data.error_flags &= ~ERROR_CALIB_FAIL;
                storage_save(&sys_data);

                printf("[Motor] Calibration Done.\n");
                send_lora_safe(MSG_CALIB_OK);

                printf("\n[READY] Calibration OK. Press SW2 to START dispensing.\n");
                current_state = STATE_WAIT_FOR_START;
                break;
            }

            case STATE_WAIT_FOR_START: {
                gpio_put(LED_PIN, 1);
                if (is_button_pressed(SW_2_PIN)) {
                    printf("[User] SW2 pressed - Starting dispense cycle\n");
                    current_state = STATE_DISPENSING;
                }
                break;
            }

            case STATE_DISPENSING: {
                gpio_put(LED_PIN, 1);

                // mark rotation start
                sys_data.is_rotating = true;
                storage_save(&sys_data);

                piezo_reset_flag();
                motor_rotate_next();

                // rotation done
                sys_data.is_rotating = false;
                sys_data.pills_left--;

                // check if pill dropped
                bool pill_detected = piezo_pill_detected(PIEZO_DETECT_TIMEOUT_MS);
                const char* exception_str = "none";

                if (pill_detected) {
                    sys_data.error_flags &= ~ERROR_NO_PILL;
                    sys_data.total_dispensed++;
                    send_lora_safe(MSG_PILL_OK);
                } else {
                    sys_data.error_flags |= ERROR_NO_PILL;
                    exception_str = "piezo not triggered";
                    send_lora_safe(MSG_PILL_FAIL);
                }

                // Update log
                int slot = PILLS_TOTAL - sys_data.pills_left - 1;
                if (slot >= 0 && slot < 7) {
                    if (pill_detected) {
                        sys_data.dispense_log[slot] = 1;
                    } else {
                        sys_data.dispense_log[slot] = 0;
                    }
                }

                sys_data.total_cycles++;
                storage_save(&sys_data);

                print_detailed_log("normal", exception_str, pill_detected);

                // Check completion
                if (sys_data.pills_left <= 0) {
                    printf("[System] All pills dispensed. Refilling...\n");
                    sleep_ms(1500);

                    send_lora_safe(MSG_ALL_DONE);

                    // Reset for next cycle
                    sys_data.pills_left = PILLS_TOTAL;
                    sys_data.error_flags = ERROR_NONE;
                    memset(sys_data.dispense_log, 0, sizeof(sys_data.dispense_log));
                    storage_save(&sys_data);

                    printf("\n[READY] Refill Done. Press SW0 to Calibrate and Restart.\n");
                    current_state = STATE_WAIT_FOR_CALIBRATION;
                } else {
                    if (sys_data.error_flags & ERROR_NO_PILL) {
                        current_state = STATE_HANDLE_ERROR;
                    } else {
                        last_dispense_time = to_ms_since_boot(get_absolute_time());
                        printf("[System] Wait 30s or Press SW2 to continue.\n");
                        current_state = STATE_SLEEP_INTERVAL;
                    }
                }
                break;
            }

            case STATE_HANDLE_ERROR: {
                blink_led(5, 200);
                printf("[Error] No pill detected. Continuing schedule...\n");
                last_dispense_time = to_ms_since_boot(get_absolute_time());
                printf("[System] Wait 30s or Press SW2 to continue.\n");
                current_state = STATE_SLEEP_INTERVAL;
                break;
            }

            case STATE_SLEEP_INTERVAL: {
                gpio_put(LED_PIN, 1);
                uint32_t now = to_ms_since_boot(get_absolute_time());

                if ((now - last_dispense_time >= DISPENSE_INTERVAL_MS) || is_button_pressed(SW_2_PIN)) {
                    if (now - last_dispense_time < DISPENSE_INTERVAL_MS) {
                        printf("[User] SW2 pressed -> Skipping wait\n");
                    }
                    current_state = STATE_DISPENSING;
                }
                sleep_ms(10);
                break;
            }

            case STATE_DONE: {
                current_state = STATE_WAIT_FOR_CALIBRATION;
                break;
            }
        }
    }
}

// button detection
static bool is_button_pressed(uint pin) {
    if (!gpio_get(pin)) {
        sleep_ms(50);
        if (!gpio_get(pin)) return true;
    }
    return false;
}

// LED Control
static void blink_led(int times, int delay_ms) {
    for (int i = 0; i < times; i++) {
        gpio_put(LED_PIN, 1); sleep_ms(delay_ms);
        gpio_put(LED_PIN, 0); sleep_ms(delay_ms);
        watchdog_update();
    }
}

static void send_lora_safe(lora_msg_type_t type) {
    if (is_lora_online) {
        if (!lora_send_status(type, &sys_data)) {
            printf("[LoRa] Msg send failed\n");
        }
    }
}

static void print_detailed_log(const char* power_status, const char* exception, bool pill_success) {
    uint32_t uptime_sec = to_ms_since_boot(get_absolute_time()) / 1000;
    int slot_index = 7 - sys_data.pills_left;
    if (slot_index < 0) slot_index = 0;

    int success_count = 0;
    for (int i = 0; i < 7; i++) {
        if (sys_data.dispense_log[i] == 1) {
            success_count++;
        }
    }

    const char* pill_status_str;
    if (pill_success) {
        pill_status_str = "dispensed";
    } else {
        pill_status_str = "missed";
    }

    const char* calib_str;
    if (sys_data.is_calibrated) {
        calib_str = "calibrated";
    } else {
        calib_str = "not_calibrated";
    }

    const char* lora_str;
    if (is_lora_online) {
        lora_str = "sent";
    } else {
        lora_str = "failed";
    }

    printf("\n--- Operation Log ---\n");
    printf("System Uptime\t: %d seconds\n", uptime_sec); // Uptime as timestamp
    printf("Slot Index\t: %d\n", slot_index);
    printf("Success Count\t: %d / 7\n", success_count);
    printf("Pill Status\t: %s\n", pill_status_str);
    printf("Calib Status\t: %s\n", calib_str);
    printf("Power Status\t: %s\n", power_status);
    printf("Exception\t: %s\n", exception);
    printf("LoRa Status\t: %s\n", lora_str);
}

static void system_init(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n=== PILL DISPENSER ===\n");

    watchdog_enable(8000, 1);// Enable Watchdog (8s timeout)

    motor_init();
    sensors_init();
    storage_init();

    gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT); gpio_put(LED_PIN, 0);
    gpio_init(SW_0_PIN); gpio_set_dir(SW_0_PIN, GPIO_IN); gpio_pull_up(SW_0_PIN);
    gpio_init(SW_2_PIN); gpio_set_dir(SW_2_PIN, GPIO_IN); gpio_pull_up(SW_2_PIN);

    // Hold SW0 at boot(emergency)
    if (!gpio_get(SW_0_PIN)) {
        printf("\n[SYSTEM] EMERGENCY RESET DETECTED!\n");
        printf("[SYSTEM] Wiping EEPROM...\n");
        for(int i=0; i<5; i++) {
            gpio_put(LED_PIN, 1); sleep_ms(100); gpio_put(LED_PIN, 0); sleep_ms(100);
        }
        storage_init_default(&sys_data);
        while(!gpio_get(SW_0_PIN)) { watchdog_update(); sleep_ms(10); }
        printf("[SYSTEM] Reset Complete.\n");
    }

    printf("[System] Hardware initialization complete\n");
}

static void lora_init_and_join(void) {
    if (!lora_init()) {
        printf("[WARN] LoRa init failed, running in offline mode\n");
        is_lora_online = false;
        return;
    }

    printf("[LoRa] Module connected. Joining network...\n");
    if (lora_join_network()) {
        printf("[LoRa] Joined Successfully\n");
        is_lora_online = true;
        send_lora_safe(MSG_BOOT);
        gpio_put(LED_PIN, 1); sleep_ms(1000); gpio_put(LED_PIN, 0);
    } else {
        printf("[LoRa] Failed (Offline Mode)\n");
        is_lora_online = false;
        blink_led(3, 100);
    }
}

// Power Restore
static void restore_state(void) {
    if (!storage_load(&sys_data)) {
        storage_init_default(&sys_data);
        print_detailed_log("boot", "none", false);
        return;
    }
    printf(" Storage: Loaded OK. (Pills Left: %d)\n", sys_data.pills_left);

    // check if power was lost during rotation
    if (sys_data.is_rotating) {
        printf("[WARNING] Power lost during rotation detected!\n");

        sys_data.error_flags |= ERROR_TURNING_INTERRUPTED;
        sys_data.is_rotating = false;
        storage_save(&sys_data);
        send_lora_safe(MSG_POWER_FAIL);

        print_detailed_log("power_loss", "rotation interrupted", false);
        // auto-recalibrate
        printf("[System] Auto-recalibrating...\n");

        current_state = STATE_CALIBRATING;
        return;
    }

    // Normal restore
    if (sys_data.pills_left <= 0) {
        printf("[System] Dispenser empty. Press SW0 to Calibrate/Refill.\n");
        current_state = STATE_WAIT_FOR_CALIBRATION;
    } else {
        printf("[System] System restarted. Press SW0 to Calibrate/Resume.\n");
        current_state = STATE_WAIT_FOR_CALIBRATION;
    }
}

