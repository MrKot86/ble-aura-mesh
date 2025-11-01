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

// LED polarity configuration
enum led_polarity {
    LED_NORMAL = 0,    // LED_OFF = LOW, LED_ON = HIGH (standard)
    LED_INVERTED = 1   // LED_OFF = HIGH, LED_ON = LOW (inverted)
};

// Internal LED state struct
struct led_entry {
    enum led_state state;
    enum led_polarity polarity;  // New field for polarity control
    const struct gpio_dt_spec *gpio;
};


// Initialize with array of pointers to const struct gpio_dt_spec, and count
int init_led_manager(struct led_entry *led_array, int count);
// Set state by index
int set_led_state(int led_idx, enum led_state state);
// Set polarity by index
int set_led_polarity(int led_idx, enum led_polarity polarity);

void operate_leds(int total_interval_ms, int blink_interval_ms);

#ifdef __cplusplus
}
#endif

#endif // LEDMANAGER_H