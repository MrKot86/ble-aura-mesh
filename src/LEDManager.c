#include "LEDManager.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>


static struct led_entry *leds = NULL;
static int led_count = 0;
static uint8_t led_brightness = 50; // Default 50% brightness

// Helper function to set PWM brightness
static void set_pwm_for_led(const struct pwm_dt_spec *pwm, bool on, uint8_t brightness) {
    if (!pwm || !pwm->dev) {
        return; // No PWM configured
    }
    
    uint32_t period = pwm->period;
    uint32_t pulse = on ? (period * brightness / 100) : 0;
    
    pwm_set_dt(pwm, period, pulse);
}

int init_led_manager(struct led_entry *led_array, int count)
{
    leds = led_array;
    if (!leds) return -1;
    led_count = count;
    for (int i = 0; i < count; ++i) {
        leds[i].state = LED_OFF;
        
        if (!leds[i].pwm || !leds[i].pwm->dev) {
            return -2; // PWM is required
        }
        if (!device_is_ready(leds[i].pwm->dev)) {
            return -3;
        }
        // Set LED OFF initially
        set_pwm_for_led(leds[i].pwm, false, 0);
    }
    return 0;
}

int set_led_state(int led_idx, enum led_state state)
{
    if (led_idx < 0 || led_idx >= led_count) return -1;
    leds[led_idx].state = state;
    return 0;
}

int set_led_brightness(int led_idx, uint8_t brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    
    if (led_idx < 0) {
        // Set global brightness for all LEDs
        led_brightness = brightness_percent;
        return 0;
    }
    
    if (led_idx >= led_count) return -1;
    
    // Update specific LED if it's currently ON
    if (leds[led_idx].state == LED_ON) {
        set_pwm_for_led(leds[led_idx].pwm, true, brightness_percent);
    }
    
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
            set_pwm_for_led(leds[i].pwm, true, led_brightness);
        } else if (leds[i].state == LED_OFF) {
            set_pwm_for_led(leds[i].pwm, false, 0);
        }
    }

    while (elapsed < total_interval_ms) {
        blink_state = !blink_state;
        for (int i = 0; i < led_count; i++) {
            if (leds[i].state == LED_BLINK_FAST) {
                set_pwm_for_led(leds[i].pwm, blink_state, led_brightness);
            }
            if (leds[i].state == LED_BLINK_ONCE) {
                if (elapsed == 0) {
                    set_pwm_for_led(leds[i].pwm, true, led_brightness);
                } else {
                    set_pwm_for_led(leds[i].pwm, false, 0);
                }
            }
        }
        k_sleep(K_MSEC(blink_interval_ms));
        elapsed += blink_interval_ms;
    }
}
