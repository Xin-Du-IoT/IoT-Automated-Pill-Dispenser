#include "dispenser.h"

#define UART_BUFFER_SIZE 256
#define LORA_CMD_BUFFER_SIZE 128
#define LORA_MSG_BUFFER_SIZE 128

static lora_state_t lora_current_state = LORA_STATE_DISCONNECTED;

// convert message type to string
static const char*get_msg_type_str(int type) {
    switch (type) {
        case MSG_BOOT:  return "BOOT";
        case MSG_CALIB_OK:  return "CALIB_OK";
        case MSG_CALIB_FAIL:  return "CALIB_FAIL";
        case MSG_PILL_OK:  return "PILL_OK";
        case MSG_PILL_FAIL:  return "PILL_FAIL";
        case MSG_POWER_FAIL:  return "PWR_FAIL";
        default:  return "EVENT";
    }
}

static void uart_clear_buffer(void) {
    uint8_t unused;
    while (iuart_read(LORA_UART_NR, &unused, 1) > 0) { }
}
// read UART until newline or timeout
static int uart_read_line(char *buffer, int max_len, uint32_t timeout_ms) {
    uint32_t start = time_us_32() / 1000;
    int pos = 0;
    while (pos < max_len - 1) {
        uint32_t elapsed = time_us_32() / 1000 - start;
        if (elapsed > timeout_ms) {
            buffer[pos] = '\0';
            return pos;
        }
        uint8_t c;
        int r = iuart_read(LORA_UART_NR, &c, 1);
        if (r > 0) {
            buffer[pos++] = c;
            if (c == '\n') break;
        } else {
            sleep_us(100);
        }
        watchdog_update();
    }
    buffer[pos] = '\0';
    return pos;
}

// send AT command and wait for expected response
// returns true if we got the response we wanted
static bool send_at_command(const char *cmd, const char *expected, uint32_t timeout_ms) { // module is slow,Optimize this later
    char buffer[UART_BUFFER_SIZE];
    uart_clear_buffer();
    iuart_send(LORA_UART_NR, cmd);
    iuart_send(LORA_UART_NR, "\r\n");

    uint32_t start = time_us_32() / 1000;
    while ((time_us_32() / 1000 - start) < timeout_ms) {
        int len = uart_read_line(buffer, sizeof(buffer), 200);
        if (len > 0) {
            while (len > 0 && (buffer[len-1] == '\r' || buffer[len-1] == '\n')) buffer[--len] = '\0';
            if (strstr(buffer, expected) != NULL) return true; // check response
            // some responses indicate failure
            if (strstr(buffer, "Join failed") != NULL) return false;
            if (strstr(buffer, "Please join") != NULL) return false;
        }
        watchdog_update();
    }
    printf("[LoRa] Timeout waiting for: %s\n", expected);
    return false;
}

bool lora_init(void) {
    iuart_setup(LORA_UART_NR, LORA_TX_PIN, LORA_RX_PIN, LORA_BAUDRATE);
    sleep_ms(4000); // module needs time to boot up
    for (int i = 0; i < 3; i++) {
        if (send_at_command("AT", "OK", LORA_TIMEOUT_SHORT)) {
            lora_current_state = LORA_STATE_DISCONNECTED;
            return true;
        }
        sleep_ms(500);
    }
    printf("[LoRa] Init failed\n");
    lora_current_state = LORA_STATE_ERROR;
    return false;
}

bool lora_join_network(void) {
    if (lora_current_state == LORA_STATE_CONNECTED) return true;
    lora_current_state = LORA_STATE_CONNECTING;
    char cmd[LORA_CMD_BUFFER_SIZE];

    if (!send_at_command("AT+MODE=LWOTAA", "LWOTAA", LORA_TIMEOUT_SHORT)) return false;
    snprintf(cmd, sizeof(cmd), "AT+KEY=APPKEY,\"%s\"", LORA_APPKEY);
    if (!send_at_command(cmd, "KEY", LORA_TIMEOUT_SHORT)) return false;
    if (!send_at_command("AT+CLASS=A", "A", LORA_TIMEOUT_SHORT)) return false;
    if (!send_at_command("AT+PORT=8", "8", LORA_TIMEOUT_SHORT)) return false;

    for (int i = 0; i < 2; i++) { //try joining network (this can take 20+ seconds)
        printf("[LoRa] Join attempt %d/2\n", i + 1);
        if (send_at_command("AT+JOIN", "Done", LORA_TIMEOUT_LONG)) {
            lora_current_state = LORA_STATE_CONNECTED;
            return true;
        }
        if (i < 1) sleep_ms(2000); //wait before retry
        watchdog_update();
    }

    printf("[LoRa] Failed to join network after 2 attempts\n");
    lora_current_state = LORA_STATE_DISCONNECTED;
    return false;
}
// send a text message over LoRaWAN
static bool lora_send_message(const char *msg) {
    if (lora_current_state != LORA_STATE_CONNECTED) return false;
    char cmd[LORA_CMD_BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+MSG=\"%s\"", msg);
    return send_at_command(cmd, "Done", 15000);
}

// send status update based on current state
bool lora_send_status(lora_msg_type_t type, const dispenser_data_t *data) {
    if (lora_current_state != LORA_STATE_CONNECTED) return false;

    char msg[LORA_MSG_BUFFER_SIZE];
    uint32_t uptime_sec = to_ms_since_boot(get_absolute_time()) / 1000;

    // special message format when cycle completes
    if (type == MSG_ALL_DONE) {
        int success_count = 0;
        for(int i=0; i<7; i++) {
            if (data->dispense_log[i] == 1) success_count++;
        }
        int fail_count = 7 - success_count;

        snprintf(msg, sizeof(msg), "[SUMMARY] Time:%us OK:%d Fail:%d Status:Refilling",
                 uptime_sec, success_count, fail_count);

    } else {
        // regular status update
        const char* type_str = get_msg_type_str(type);

        int slot = 7 - data->pills_left;
        if (slot < 0) slot = 0;

        snprintf(msg, sizeof(msg), "[%s] Time:%us Slot:%d Left:%d",
                 type_str,
                 uptime_sec,
                 slot,
                 data->pills_left);
    }

    printf("[LoRa] Sending readable status: %s\n", msg);
    return lora_send_message(msg);
}
// get current connection state
lora_state_t lora_get_state(void) {
    return lora_current_state;
}