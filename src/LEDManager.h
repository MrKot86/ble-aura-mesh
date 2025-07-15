#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/drivers/gpio.h>

enum led_state {
    LED_OFF,
    LED_ON,
    LED_BLINK_FAST,
    LED_BLINK_ONCE
};

// Internal LED state struct
struct led_entry {
    enum led_state state;
    const struct gpio_dt_spec *gpio;
};


// Initialize with array of pointers to const struct gpio_dt_spec, and count
int init_led_manager(struct led_entry *led_array, int count);
// Set state by index
int set_led_state(int led_idx, enum led_state state);

void operate_leds(int total_interval_ms, int blink_interval_ms);

#ifdef __cplusplus
}
#endif

#endif // LEDMANAGER_H