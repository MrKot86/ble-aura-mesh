#include "LEDManager.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>


static struct led_entry *leds = NULL;
static int led_count = 0;

int init_led_manager(struct led_entry *led_array, int count)
{
    leds = led_array;
    if (!leds) return -1;
    led_count = count;
    for (int i = 0; i < count; ++i) {
        leds[i].state = LED_OFF;
        if (!device_is_ready(leds[i].gpio->port)) return -2;
        if (gpio_pin_configure_dt(leds[i].gpio, GPIO_OUTPUT_INACTIVE) < 0) return -3;
        gpio_pin_set_dt(leds[i].gpio, 0);
    }
    return 0;
}

int set_led_state(int led_idx, enum led_state state)
{
    if (led_idx < 0 || led_idx >= led_count) return -1;
    leds[led_idx].state = state;
    return 0;
}

// Operate LEDs for a total interval, toggling blinking LEDs at the given blink interval
void operate_leds(int total_interval_ms, int blink_interval_ms)
{
    int elapsed = 0;
    int blink_state = 0;
    // Synchronize state to new_state once at the start
    for (int i = 0; i < led_count; i++) {
        // set ONs and OFFs immediately
        if (leds[i].state == LED_ON) {
            gpio_pin_set_dt(leds[i].gpio, 1);
        } else if (leds[i].state == LED_OFF) {
            gpio_pin_set_dt(leds[i].gpio, 0);
        }
    }

    while (elapsed < total_interval_ms) {
        for (int i = 0; i < led_count; i++) {
            if (leds[i].state == LED_BLINKING) {
                gpio_pin_toggle_dt(leds[i].gpio);
            }
        }
        k_sleep(K_MSEC(blink_interval_ms));
        elapsed += blink_interval_ms;
        blink_state = !blink_state;
    }
}
