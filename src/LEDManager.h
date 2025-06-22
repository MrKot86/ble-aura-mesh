#ifndef LEDMANAGER_H
#define LEDMANAGER_H

#ifdef __cplusplus
extern "C" {
#endif
#include <zephyr/drivers/gpio.h>

enum led_state {
    LED_OFF,
    LED_ON,
    LED_BLINKING
};

// Initialize with array of pointers to const struct gpio_dt_spec, and count
int init_led_manager(const struct gpio_dt_spec *const *leds, int led_count, int interval_ms);
// Set state by index
int set_led_state(int led_idx, enum led_state state);

void run_led_thread(void);

#ifdef __cplusplus
}
#endif

#endif // LEDMANAGER_H