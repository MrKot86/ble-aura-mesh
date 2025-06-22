#include "LEDManager.h"
#include <zephyr/kernel.h>
#include <string.h>
#include <stdlib.h>

// Internal LED state struct
struct led_entry {
    enum led_state state;
    const struct gpio_dt_spec *gpio;
};

static struct led_entry *leds = NULL;
static int led_count = 0;
static int cycle_interval = 0;

struct k_mutex led_mutex;

#define STACK_SIZE 512
#define THREAD_PRIORITY 5
K_THREAD_STACK_DEFINE(led_thread_stack, STACK_SIZE);
struct k_thread led_thread_data;

static void led_thread_function(void *arg1, void *arg2, void *arg3)
{
    while (1) {
        k_mutex_lock(&led_mutex, K_FOREVER);
        for (int i = 0; i < led_count; i++) {
            switch (leds[i].state) {
                case LED_OFF:
                    gpio_pin_set_dt(leds[i].gpio, GPIO_OUTPUT_INACTIVE);
                    break;
                case LED_ON:
                    gpio_pin_set_dt(leds[i].gpio, GPIO_OUTPUT_ACTIVE);
                    break;
                case LED_BLINKING:
                    gpio_pin_toggle_dt(leds[i].gpio);
                    break;
            }
        }
        k_mutex_unlock(&led_mutex);
        k_sleep(K_MSEC(cycle_interval));
    }
}

int init_led_manager(const struct gpio_dt_spec *const *led_array, int count, int interval_ms)
{
    leds = (struct led_entry *)malloc(count * sizeof(struct led_entry));
    if (!leds) return -1;
    led_count = count;
    cycle_interval = interval_ms;
    k_mutex_init(&led_mutex);
    for (int i = 0; i < count; ++i) {
        leds[i].gpio = led_array[i];
        leds[i].state = LED_OFF;
        if (!device_is_ready(led_array[i]->port)) return -2;
        if (gpio_pin_configure_dt(led_array[i], GPIO_OUTPUT_INACTIVE) < 0) return -3;
    }
    k_thread_create(&led_thread_data, led_thread_stack,
                    K_THREAD_STACK_SIZEOF(led_thread_stack),
                    led_thread_function, NULL, NULL, NULL,
                    THREAD_PRIORITY, 0, K_NO_WAIT);
    return 0;
}

int set_led_state(int led_idx, enum led_state state)
{
    if (led_idx < 0 || led_idx >= led_count) return -1;
    k_mutex_lock(&led_mutex, K_FOREVER);
    leds[led_idx].state = state;
    k_mutex_unlock(&led_mutex);
    return 0;
}

void run_led_thread(void)
{
    k_thread_name_set(&led_thread_data, "led_thread");
    k_thread_start(&led_thread_data);
}
