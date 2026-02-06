#ifndef DISPENSER_H
#define DISPENSER_H

#include <pico/time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pico/stdio.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/watchdog.h>
#include "iuart.h"

// Pin Definitions
// Motor
#define MOTOR_PIN_1   2
#define MOTOR_PIN_2   3
#define MOTOR_PIN_3   6
#define MOTOR_PIN_4   13

// Sensors
#define OPTO_PIN  28
#define PIEZO_PIN  27

// UI Buttons & LEDs
#define SW_0_PIN  9 // Calibration
#define SW_2_PIN  7 // Dispensing
#define LED_PIN  20 // Signal

// Communication & Storage
#define I2C_PORT  i2c0
#define I2C_SDA_PIN  16
#define I2C_SCL_PIN  17
#define EEPROM_ADDR   0x50

#define LORA_UART_NR  1
#define LORA_TX_PIN  4
#define LORA_RX_PIN  5
#define LORA_BAUDRATE 9600

// System Constants
#define PILLS_TOTAL   7
#define DISPENSE_INTERVAL_MS  30000   // 30s
#define BLINK_INTERVAL_MS   500
#define PIEZO_DETECT_TIMEOUT_MS 1000 // 1s to wait for pill dropping

//LoRa Configuration
#define LORA_TIMEOUT_SHORT 2000
#define LORA_TIMEOUT_LONG  20000
#define LORA_APPKEY   "c24500f38e2104def45e59422db86803"

// Error Flags
#define ERROR_NONE    0x00
#define ERROR_MOTOR_STUCK  0x01
#define ERROR_POWER_FAIL  0x02
#define ERROR_NO_PILL    0x04
#define ERROR_CALIB_FAIL   0x08
#define ERROR_TURNING_INTERRUPTED  0x10 // Power lost during rotation

// main states
typedef enum {
    STATE_WAIT_FOR_CALIBRATION,
    STATE_CALIBRATING,
    STATE_WAIT_FOR_START,
    STATE_DISPENSING,
    STATE_HANDLE_ERROR,
    STATE_SLEEP_INTERVAL,
    STATE_DONE
} DispenserState;

// LoRaWAN States
typedef enum {
    LORA_STATE_DISCONNECTED = 0,
    LORA_STATE_CONNECTING,
    LORA_STATE_CONNECTED,
    LORA_STATE_ERROR
} lora_state_t;

// LoRa Message Types
typedef enum {
    MSG_BOOT = 0,    // System Boot
    MSG_CALIB_OK,
    MSG_CALIB_FAIL,
    MSG_PILL_OK,
    MSG_PILL_FAIL,
    MSG_ALL_DONE,    // Cycle Complete (Summary)
    MSG_POWER_FAIL,     // Power Loss Detected
    MSG_ERROR
} lora_msg_type_t;

// storage Structure (EEPROM)
typedef struct __attribute__((packed)) {
    uint32_t init_marker;
    uint8_t  pills_left;
    uint8_t  is_calibrated;
    uint16_t total_dispensed;
    uint16_t total_cycles;
    uint8_t  error_flags;
    uint8_t  is_rotating;     // set during motor operation,
    uint8_t  dispense_log[7];   // current cycle: 1=success, 0=fail
    uint16_t crc16;       // Data integrity check
} dispenser_data_t;

// motor.c
void motor_init(void);
void motor_calibrate(void);
void motor_rotate_next(void);
void motor_off(void);

// sensors.c
void sensors_init(void);
bool opto_is_aligned(void);
bool piezo_pill_detected(uint32_t timeout_ms);
void piezo_reset_flag(void);

// storage.c
void storage_init(void);
bool storage_save(const dispenser_data_t *data);
bool storage_load(dispenser_data_t *data);
void storage_init_default(dispenser_data_t *data);
void storage_log_msg(const char *message);

// lora.c
bool lora_init(void);
bool lora_join_network(void);
bool lora_send_status(lora_msg_type_t type, const dispenser_data_t *data);

#endif // DISPENSER_H