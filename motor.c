#include "dispenser.h"

static const uint MOTOR_PINS[4] = {
    MOTOR_PIN_1, MOTOR_PIN_2, MOTOR_PIN_3, MOTOR_PIN_4
};

static const uint8_t step_sequence[8][4] = {
    {1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0},
    {0, 0, 1, 0}, {0, 0, 1, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}
};

#define STEPS_PER_REV   4096
#define STEPS_PER_SLOT  (STEPS_PER_REV / 8) // one slot steps

static int current_step_index = 0;

// move one step
static void step_one(int direction) {
    if (direction > 0) {
        current_step_index = (current_step_index + 1) % 8;
    } else {
        current_step_index = (current_step_index - 1 + 8) % 8;
    }

    for (int i = 0; i < 4; i++) {
        gpio_put(MOTOR_PINS[i], step_sequence[current_step_index][i]);
    }

    sleep_ms(2);
}

void motor_init(void) {
    for (int i = 0; i < 4; i++) {
        gpio_init(MOTOR_PINS[i]);
        gpio_set_dir(MOTOR_PINS[i], GPIO_OUT);
        gpio_put(MOTOR_PINS[i], 0);
    }
}

void motor_off(void) {
    for (int i = 0; i < 4; i++) {
        gpio_put(MOTOR_PINS[i], 0);
    }
}

void motor_calibrate(void) {
    for (int i = 0; i < STEPS_PER_REV + 200; i++) {
        step_one(1);
        if (i % 10 == 0) watchdog_update();
    }
    int safety_counter = 0;
    while (!opto_is_aligned()) {
        step_one(1);
        if (safety_counter++ % 10 == 0) watchdog_update(); // update watchdog

        if (safety_counter > STEPS_PER_REV * 3) {
            printf("[Motor] ERROR: Sensor not found !\n");
            break;
        }
    }
    motor_off();
}

// rotate one pill slot (1/8 revolution)
void motor_rotate_next(void) {
    for (int i = 0; i < STEPS_PER_SLOT; i++) {
        step_one(1);
        if (i % 10 == 0) watchdog_update(); // update watchdog
    }

    motor_off();
}