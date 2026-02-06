#include "dispenser.h"

#define EEPROM_WRITE_DELAY_MS  5
#define EEPROM_SIZE_BYTES  (32 * 1024)       // AT24C256 = 32KB
#define STATE_ADDR    (EEPROM_SIZE_BYTES - 64)  // Address for state storage
#define LOG_START_ADDR  0
#define LOG_TOTAL_SIZE  8192              // 8KB log space
#define LOG_ENTRY_SIZE   64
#define MAX_LOG_ENTRIES  (LOG_TOTAL_SIZE / LOG_ENTRY_SIZE)
#define MAX_STRING_LENGTH  59

static int current_log_index = 0;

static uint16_t crc16(const uint8_t *data_p, size_t length) {
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while (length--) {
        x = crc >> 8 ^ *data_p++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t)(x << 12)) ^
              ((uint16_t)(x << 5)) ^ ((uint16_t)x);
    }
    return crc;
}

static void eeprom_write_block(uint16_t addr, const uint8_t *data, size_t len) {
    uint8_t buf[len + 2];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFF);
    memcpy(&buf[2], data, len);

    i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buf, len + 2, false);
    sleep_ms(EEPROM_WRITE_DELAY_MS);// EEPROM needs time to write
}

static void eeprom_read_block(uint16_t addr, uint8_t *data, size_t len) {
    uint8_t buf[2];
    buf[0] = (uint8_t)(addr >> 8);
    buf[1] = (uint8_t)(addr & 0xFF);

    i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buf, 2, true);
    i2c_read_blocking(I2C_PORT, EEPROM_ADDR, data, len, false);
}

// logging system
void storage_scan_logs(void) {
    uint8_t buffer[LOG_ENTRY_SIZE];

    for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
        uint16_t addr = LOG_START_ADDR + (i * LOG_ENTRY_SIZE);
        eeprom_read_block(addr, buffer, LOG_ENTRY_SIZE);
        // Check for empty or uninitialized entry
        if (buffer[0] == 0 || buffer[0] == 0xFF) { // empty slot
            current_log_index = i;
            return;
        }
    }
    // log area full, wrap around
    current_log_index = 0;
    printf("[Storage] Log area full, wrapping to 0\n");
}
// write a log message to EEPROM
void storage_log_msg(const char *message) {
    if (!message || !message[0])
        return;

    if (current_log_index >= MAX_LOG_ENTRIES) {
        current_log_index = 0;
    }

    uint8_t buffer[LOG_ENTRY_SIZE];
    memset(buffer, 0, LOG_ENTRY_SIZE);

    size_t len = strlen(message);
    if (len > MAX_STRING_LENGTH) len = MAX_STRING_LENGTH;
    memcpy(buffer, message, len);

    uint16_t crc = crc16(buffer, len + 1);
    buffer[len + 1] = (uint8_t)(crc >> 8);
    buffer[len + 2] = (uint8_t)(crc & 0xFF);

    uint16_t addr = LOG_START_ADDR + (current_log_index * LOG_ENTRY_SIZE);
    eeprom_write_block(addr, buffer, len + 3);// one \0, two crc bits.

    printf("[Log] [%d] %s\n", current_log_index, message);
    current_log_index++;
}

void storage_init(void) {
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    storage_scan_logs();
}

bool storage_save(const dispenser_data_t *data) {
    uint8_t buffer[sizeof(dispenser_data_t)];
    memcpy(buffer, data, sizeof(dispenser_data_t));

    uint16_t crc = crc16(buffer, sizeof(dispenser_data_t) - 2);
    buffer[sizeof(dispenser_data_t) - 2] = (uint8_t)(crc >> 8);
    buffer[sizeof(dispenser_data_t) - 1] = (uint8_t)crc;

    // write to EEPROM
    eeprom_write_block(STATE_ADDR, buffer, sizeof(dispenser_data_t));

    // verify write
    uint8_t verify[sizeof(dispenser_data_t)];
    eeprom_read_block(STATE_ADDR, verify, sizeof(dispenser_data_t));

    if (memcmp(buffer, verify, sizeof(dispenser_data_t)) == 0) {
        return true;
    } else {
        printf("[Storage] ERROR: Save verification failed\n");
        return false;
    }
}

bool storage_load(dispenser_data_t *data) {
    uint8_t buffer[sizeof(dispenser_data_t)] = {0};

    eeprom_read_block(STATE_ADDR, buffer, sizeof(dispenser_data_t));
    dispenser_data_t *temp = (dispenser_data_t *)buffer;

    if (temp->init_marker != 0xDEADBEEF) {
        printf("[Storage] Invalid magic: 0x%08X (expected 0xDEADBEEF)\n",
               temp->init_marker);
        return false;
    }

    if (crc16(buffer, sizeof(dispenser_data_t)) == 0) {
        memcpy(data, buffer, sizeof(dispenser_data_t));
        printf("[Storage] Data loaded successfully (Pills=%d)\n",
               data->pills_left);
        return true;
    } else {
        printf("[Storage] CRC check failed (Result != 0)\n");
        return false;
    }
}

void storage_init_default(dispenser_data_t *data) {
    memset(data, 0, sizeof(dispenser_data_t));

    data->init_marker = 0xDEADBEEF;
    data->pills_left = PILLS_TOTAL;
    data->is_calibrated = 0;
    data->total_dispensed = 0;
    data->total_cycles = 0;
    data->error_flags = ERROR_NONE;
    data->is_rotating = false;
    memset(data->dispense_log, 0, sizeof(data->dispense_log));

    storage_save(data);
    printf("[Storage] Defaults initialized and saved\n");
}




