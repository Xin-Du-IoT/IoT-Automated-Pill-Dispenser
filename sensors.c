#include "dispenser.h"

static volatile bool pill_drop_flag = false;
// ISR for piezo sensor
static void gpio_irq_handler(uint gpio, uint32_t events) {
    if (gpio == PIEZO_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        pill_drop_flag = true;  // pill detected
    }
}

void sensors_init(void) {
    gpio_init(OPTO_PIN);
    gpio_set_dir(OPTO_PIN, GPIO_IN);
    gpio_pull_up(OPTO_PIN);

    gpio_init(PIEZO_PIN);
    gpio_set_dir(PIEZO_PIN, GPIO_IN);
    gpio_pull_up(PIEZO_PIN);

    gpio_set_irq_enabled_with_callback(
        PIEZO_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        &gpio_irq_handler
    );
}


bool opto_is_aligned(void) {
    return !gpio_get(OPTO_PIN);
}

void piezo_reset_flag(void) {
    pill_drop_flag = false;
}
// wait for pill to drop and trigger piezo
bool piezo_pill_detected(uint32_t timeout_ms) {
    // maybe it already dropped during rotation
    if (pill_drop_flag) {
        printf("[Sensors] Pill detected (immediate)\n");
        return true;
    }

    //  wait for it to drop
    printf("[Sensors] Waiting for pill drop (timeout=%dms)...\n", timeout_ms);
    uint32_t start_time = to_ms_since_boot(get_absolute_time());

    while (to_ms_since_boot(get_absolute_time()) - start_time < timeout_ms) {
        if (pill_drop_flag) {
            printf("[Sensors] Pill detected (delayed)\n");
            return true;
        }
        sleep_ms(10);
    }

    printf("[Sensors] No pill detected (timeout)\n");
    return false;
}