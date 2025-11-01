#include "LEDManager.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>


static struct led_entry *leds = NULL;
static int led_count = 0;

// Helper function to set GPIO pin based on LED polarity
static void set_gpio_for_led(const struct gpio_dt_spec *gpio, enum led_polarity polarity, bool logical_on) {
    gpio_pin_set_dt(gpio, polarity == LED_NORMAL ? logical_on : ! logical_on);
}

int init_led_manager(struct led_entry *led_array, int count)
{
    leds = led_array;
    if (!leds) return -1;
    led_count = count;
    for (int i = 0; i < count; ++i) {
        leds[i].state = LED_OFF;
        if (!device_is_ready(leds[i].gpio->port)) return -2;
        if (gpio_pin_configure_dt(leds[i].gpio, GPIO_OUTPUT_INACTIVE) < 0) return -3;
        // Set initial state respecting polarity (LED_OFF)
        set_gpio_for_led(leds[i].gpio, leds[i].polarity, false);
    }
    return 0;
}

int set_led_state(int led_idx, enum led_state state)
{
    if (led_idx < 0 || led_idx >= led_count) return -1;
    leds[led_idx].state = state;
    return 0;
}

int set_led_polarity(int led_idx, enum led_polarity polarity)
{
    if (led_idx < 0 || led_idx >= led_count) return -1;
    leds[led_idx].polarity = polarity;
    
    // Update the current pin state to reflect the new polarity
    bool led_should_be_on = (leds[led_idx].state == LED_ON);
    set_gpio_for_led(leds[led_idx].gpio, polarity, led_should_be_on);
    
    return 0;
}

// Operate LEDs for a total interval, toggling blinking LEDs at the given blink interval
void operate_leds(int total_interval_ms, int blink_interval_ms)
{
    int elapsed = 0;
    bool blink_state = false;
    // Synchronize state to new_state once at the start
    for (int i = 0; i < led_count; i++) {
        // set ONs and OFFs immediately
        if (leds[i].state == LED_ON) {
            set_gpio_for_led(leds[i].gpio, leds[i].polarity, true);
        } else if (leds[i].state == LED_OFF) {
            set_gpio_for_led(leds[i].gpio, leds[i].polarity, false);
        }
    }

    while (elapsed < total_interval_ms) {
        blink_state = !blink_state;
        for (int i = 0; i < led_count; i++) {
            if (leds[i].state == LED_BLINK_FAST) {
                set_gpio_for_led(leds[i].gpio, leds[i].polarity, blink_state);
            }
            if (leds[i].state == LED_BLINK_ONCE) {
                if (elapsed == 0) {
                    set_gpio_for_led(leds[i].gpio, leds[i].polarity, true);
                } else {
                    set_gpio_for_led(leds[i].gpio, leds[i].polarity, false);
                }
            }
        }
        k_sleep(K_MSEC(blink_interval_ms));
        elapsed += blink_interval_ms;
    }
}
