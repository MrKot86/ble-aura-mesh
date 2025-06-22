/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>

#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/random/random.h>

#include "LEDManager.h"
#include "types.h"
#include "defines.h"

#define LED_NODE DT_ALIAS(led0)  // Maps to 'led0' in your board's device tree
#define LED_14_NODE DT_ALIAS(led14)  // Maps to 'led0' in your board's device tree
#define LED_15_NODE DT_ALIAS(led15)  // Maps to 'led0' in your board's device tree
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct gpio_dt_spec led14 = GPIO_DT_SPEC_GET(LED_14_NODE, gpios);
static const struct gpio_dt_spec led15 = GPIO_DT_SPEC_GET(LED_15_NODE, gpios);

// Use enum for LED indexes for readability
typedef enum {
    ON_BOARD_LED = 0,
    LED_14,
    LED_15,
    LED_IDX_MAX
} led_index_t;

static struct nvs_fs fs;
const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

static uint8_t adv_data[16]; // Buffer for dynamic advertisement data

static bt_addr_le_t static_addr;
char my_mac_str[MAC_STR_LEN + 1];


// Use a matrix for level counters: [affinity][level]
static uint8_t aura_level_count[3][4] = {{0}};
static peer_t peers[MAX_PEERS];
static uint8_t peer_count = 0; // Number of discovered peers


/* Custom advertising parameters with BT_LE_ADV_OPT_USE_IDENTITY */
static const struct bt_le_adv_param adv_params = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_USE_NAME,
    .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
    .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
    .peer = NULL,
};

device_info_t device_info = {
    .mode = MODE_NONE,
    .affinity = AFFINITY_UNITY,
    .level = 0
};
static bool mode_changed = false;

// Helper: Convert MAC to string (uppercase, no separators)
static void mac_to_str(const bt_addr_le_t *addr, char *out)
{
    // Converts MAC to uppercase hex string, no delimiters, e.g. "AABBCCDDEEFF"
    for (int i = 0; i < MAC_LEN; ++i) {
        sprintf(&out[i * 2], "%02X", addr->a.val[MAC_LEN - 1 - i]);
    }
    out[MAC_STR_LEN] = '\0';
}

// Helper: Detect advertisement type based on name prefix
static adv_name_type_t detect_adv_name(const uint8_t *data, uint8_t len) {
    if (len >= MASTER_PREFIX_LEN + MAC_STR_LEN &&
        memcmp(data, MASTER_PREFIX, MASTER_PREFIX_LEN) == 0 &&
        memcmp(data + MASTER_PREFIX_LEN, my_mac_str, MAC_STR_LEN) == 0) {
        return ADV_TYPE_MASTER;
    }
    if (len >= 6 && memcmp(data, "Zephyr", 6) == 0) {
        return ADV_TYPE_ZEPHYR;
    }
    return ADV_TYPE_UNKNOWN;
}

// Helper: Decode a single byte into device_info fields (2 bits per field)
static void decode_device_info(uint8_t val, device_info_t *info) {
    info->mode = (val >> 6) & 0x03;
    info->affinity = (val >> 4) & 0x03;
    info->level = (val >> 2) & 0x03;
    // bits 1:0 are reserved, ignored
}


// Helper to extract manufacturer data (first byte) from advertisement
static bool extract_mfg_data(const struct net_buf_simple *buf, uint8_t *out_data) {
    struct net_buf_simple temp = *buf;
    while (temp.len > 1) {
        uint8_t length = net_buf_simple_pull_u8(&temp);
        if (length == 0 || length > temp.len) {
            break;
        }
        uint8_t type = net_buf_simple_pull_u8(&temp);
        length -= 1;
        if (type == BT_DATA_MANUFACTURER_DATA && length >= 1) {
            *out_data = temp.data[0];
            return true;
        }
        temp.data += length;
        temp.len -= length;
    }
    return false;
}

// Handle master advertisements that may change device_info
static void handle_master_adv(const bt_addr_le_t *addr, struct net_buf_simple *buf) {
    uint8_t mfg_byte;
    if (extract_mfg_data(buf, &mfg_byte)) {
        device_info_t new_info;
        decode_device_info(mfg_byte, &new_info);
        if (memcmp(&device_info, &new_info, sizeof(device_info_t)) != 0) {
            if (device_info.mode != new_info.mode) {
                mode_changed = true;
            }
            device_info = new_info;
            nvs_write(&fs, 1, &device_info, sizeof(device_info)); // Store new device_info in flash (ID 1)
        }
    }
}

static void handle_zephyr_adv(const bt_addr_le_t *addr, struct net_buf_simple *buf) {
    if (peer_count >= MAX_PEERS) {
        return; // Peer list is full, ignore this advertisement
    }

    // Only process peers advertising MODE_AURA
    uint8_t mfg_data = 0;
    if (!extract_mfg_data(buf, &mfg_data)) {
        return; // wrong advertisement data, ignore
    }
    
    device_info_t peer_info;
    decode_device_info(mfg_data, &peer_info);
    if (peer_info.mode != MODE_AURA) {
        return; // Only interested in AURA mode
    }
    // Check if MAC is already in peers array
    for (uint8_t i = 0; i < peer_count; ++i) {
        if (memcmp(addr->a.val, peers[i].mac, 6) == 0) {
            return; // MAC already exists, ignore this advertisement
        }
    }

    peers[peer_count].data = mfg_data;
    memcpy(peers[peer_count].mac, addr->a.val, 6);
    peer_count++;

    // Maintain counts of levels for each affinity using matrix
    if (peer_info.affinity <= AFFINITY_TECHNO && peer_info.level < 4) {
        aura_level_count[peer_info.affinity][peer_info.level]++;
    }
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    if (rssi < -60) {
        return; // Ignore weak signals
    }
    struct net_buf_simple temp = *buf;
    
    while (temp.len > 1) {
        uint8_t length = net_buf_simple_pull_u8(&temp);
        if (length == 0 || length > temp.len) {
            break;
        }
        uint8_t type = net_buf_simple_pull_u8(&temp);
        length -= 1;
        if (type == BT_DATA_NAME_COMPLETE || type == BT_DATA_NAME_SHORTENED) {
            adv_name_type_t adv_type = detect_adv_name(temp.data, length);
            if (adv_type == ADV_TYPE_MASTER) {
                handle_master_adv(addr, buf);
                break;
            } else if (adv_type == ADV_TYPE_ZEPHYR) {
                handle_zephyr_adv(addr, buf);
                break;
            }
        }
        temp.data += length;
        temp.len -= length;
    }

    
}

static int init_flash(void) 
{
    int err;

    if (!device_is_ready(flash_dev)) {
        return 1;
    }

    struct flash_pages_info info;
    err = flash_get_page_info_by_offs(flash_dev, fs.offset, &info);
    if (err) {
        printk("Failed to get flash page info (err %d)\n", err);
        return 1;
    }

    /* Initialize the NVS file system */
    fs.offset = FLASH_AREA_OFFSET(storage_partition);
    fs.sector_size = info.size; // Use the flash page size
    fs.sector_count = 3; // Adjust as needed
    fs.flash_device = flash_dev;

    err = nvs_mount(&fs);
    if (err) {
        printk("Failed to mount NVS file system (err %d)\n", err);
        return err;
    }

    return 0;
}

static void generate_static_random_addr(bt_addr_le_t *addr)
{
    addr->type = BT_ADDR_LE_RANDOM;
    for (int i = 0; i < 6; ++i) {
        addr->a.val[i] = sys_rand32_get() & 0xFF;
    }
    addr->a.val[5] |= 0xC0;
    addr->a.val[5] &= 0xC3;
}

// Helper: Encode device_info into a single byte (2 bits per field)
static uint8_t encode_device_info(const device_info_t *info) {
    // mode: 2 bits (0-3), affinity: 2 bits (0-3), level: 2 bits (0-3)
    // [7:6]=mode, [5:4]=affinity, [3:2]=level, [1:0]=reserved(0)
    uint8_t val = 0;
    val |= ((info->mode & 0x03) << 6);
    val |= ((info->affinity & 0x03) << 4);
    val |= ((info->level & 0x03) << 2);
    // bits 1:0 left as 0 (reserved)
    return val;
}

// Stub functions for other modes
static void mode_aura_loop(void) {
    // Existing logic for MODE_AURA
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = 0x0010,
        .window = 0x0010,
    };
    int err;
    while (!mode_changed) {
        // --- Scanning phase ---
        uint32_t scan_jitter = sys_rand32_get() % (2 * SCAN_JITTER_MS + 1) - SCAN_JITTER_MS;
        uint32_t scan_time = SCAN_INTERVAL_MS + scan_jitter;
        err = bt_le_scan_start(&scan_param, scan_cb);
        if (err) {
            printk("Scan start failed: %d\n", err);
        }
        k_sleep(K_MSEC(scan_time));
        bt_le_scan_stop();

        // --- Advertising phase ---
        adv_data[0] = encode_device_info(&device_info); // device_info byte as first byte
        adv_data[1] = 0; // second byte can be reserved or used for future extension
        struct bt_data dynamic_ad[] = {
            BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, 2),
        };
        uint32_t adv_jitter = sys_rand32_get() % (2 * ADV_JITTER_MS + 1) - ADV_JITTER_MS;
        uint32_t adv_time = ADV_INTERVAL_MS + adv_jitter;
        err = bt_le_adv_start(&adv_params, dynamic_ad, ARRAY_SIZE(dynamic_ad), NULL, 0);
        if (err) {
            printk("Adv start failed: %d\n", err);
        }
        k_sleep(K_MSEC(adv_time));
        bt_le_adv_stop();
    }
}

static void mode_device_loop(void) {
    // Implements the flow chart logic for device mode using led_manager
    set_led_state(LED_14, 0); // Device OFF (LED_1 OFF)
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = 0x0010,
        .window = 0x0010,
    };
    int err;
    while (!mode_changed) {
        // Reset peer and level counts for this cycle
        peer_count = 0;
        memset(aura_level_count, 0, sizeof(aura_level_count));

        // Scan for the duration of the cycle interval
        uint32_t cycle_jitter = sys_rand32_get() % (2 * SCAN_JITTER_MS + 1) - SCAN_JITTER_MS;
        uint32_t cycle_time = SCAN_INTERVAL_MS + cycle_jitter;
        err = bt_le_scan_start(&scan_param, scan_cb);
        if (err) {
            printk("Scan start failed: %d\n", err);
        }
        k_sleep(K_MSEC(cycle_time));
        bt_le_scan_stop();

        // Count max level and number of peers at max level for each affinity
        uint8_t max_level[3] = {0};
        uint8_t peers_at_max_level[3] = {0};
        uint8_t highest_level = 0;
        for (int aff = 0; aff < 3; ++aff) {
            for (int lvl = highest_level; lvl < 4; ++lvl) {
                if (aura_level_count[aff][lvl] > 0) {
                    max_level[aff] = lvl;
                    peers_at_max_level[aff] = aura_level_count[aff][lvl];
                    if (lvl > highest_level) {
                        highest_level = lvl; // Update highest level found
                    }
                }
            }
        }

        // Got any?
        if (peers_at_max_level[AFFINITY_MAGIC] == 0 && peers_at_max_level[AFFINITY_TECHNO] == 0 && peers_at_max_level[AFFINITY_UNITY] == 0) {
            set_led_state(LED_14, 0); // Keep OFF (LED_1 OFF)
            continue;
        }

        uint8_t my_affinity = device_info.affinity;
        uint8_t my_peers_at_highest = aura_level_count[my_affinity][highest_level];
        // If my affinity has no peers at max level, turn off LED_1
        if (my_peers_at_highest == 0) {
            set_led_state(LED_14, 0); // Device OFF (LED_1 OFF)
            continue;
        }

        for (int aff = 0; aff < 3; ++aff) {
            // If my affinity has peers at max level, but not the most peers, turn off LED_1
            if (aff != my_affinity && aura_level_count[aff][highest_level] > my_peers_at_highest) {
                set_led_state(LED_14, 0); // Device OFF (LED_1 OFF)
                continue;
            }
        }

        // If we reach here, it means my affinity has the most peers at max level
        // Turn on LED_1 to indicate device is active
        set_led_state(LED_14, 1);
        
    }
    // On exit, turn off LED_1
    set_led_state(LED_14, 0);
}

static void mode_lvlup_token_loop(void) {
    // TODO: Implement logic for MODE_LVLUP_TOKEN
    while (!mode_changed) {
        k_sleep(K_MSEC(100));
    }
}

static void mode_none_loop(void) {
    // Only scan for master advertisements that may change device_info
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        .interval = 0x0010,
        .window = 0x0010,
    };
    int err;
    while (!mode_changed) {
        err = bt_le_scan_start(&scan_param, scan_cb);
        if (err) {
            printk("Scan start failed: %d\n", err);
        }
        // Scan for a while, then stop to allow mode change check
        k_sleep(K_MSEC(500));
        bt_le_scan_stop();
        k_sleep(K_MSEC(100));
    }
}

static void main_loop(void)
{
    operation_mode_t last_mode = device_info.mode;
    mode_changed = false;
    while (1) {
        switch (device_info.mode) {
            case MODE_AURA:
                mode_aura_loop();
                break;
            case MODE_DEVICE:
                mode_device_loop();
                break;
            case MODE_LVLUP_TOKEN:
                mode_lvlup_token_loop();
                break;
            case MODE_NONE:
            default:
                mode_none_loop();
                break;
        }
        // Check for mode change
        if (device_info.mode != last_mode) {
            mode_changed = true;
            last_mode = device_info.mode;
            mode_changed = false; // Reset for next loop
        }
    }
}

int main(void)
{
    int err;

    // Array of pointers to LED gpio_dt_spec
    static const struct gpio_dt_spec *led_array[LED_IDX_MAX] = { &led, &led14, &led15 };
    init_led_manager(led_array, LED_IDX_MAX, 1000); // 1 second interval, 3 LEDs

    if (init_flash() ) {
        int flash_error_bp = 1;
        return 1;
    }

    if (!device_is_ready(led.port)) {
        printk("LED device not ready\n");
        return 0;
    }
    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0) {
        printk("Failed to configure LED GPIO\n");
        return 0;
    }

    /* Set a custom Bluetooth device name */
    bt_set_name("ble_test");

    /* Try to read static address from flash */
    err = nvs_read(&fs, NVS_ID_STATIC_ADDR, &static_addr, sizeof(static_addr));
    if (err < 0) {
        // Not found, generate and store
        generate_static_random_addr(&static_addr);
        BT_ADDR_SET_STATIC(&(static_addr.a));
        err = nvs_write(&fs, NVS_ID_STATIC_ADDR, &static_addr, sizeof(static_addr));
        if (err < 0) {
            printk("Failed to write static address to flash (err %d)\n", err);
            return 0;
        }
        printk("Generated and stored new static address\n");
    } else {
        printk("Loaded static address from flash\n");
    }

    mac_to_str(&static_addr, my_mac_str);

    err = bt_id_create(&static_addr, NULL);
    if (err) {
        printk("Failed to set static random address (err %d)\n", err);
        return 0;
    }

    /* Initialize the Bluetooth Subsystem */
    err = bt_enable(NULL);
    if (err) {
        return 0;
    }

    /* Load device_info from flash (ID 1) */
    err = nvs_read(&fs, 1, &device_info, sizeof(device_info));
    if (err < 0) {
        // Not found, use default (already initialized)
        printk("No device_info in flash, using default\n");
    } else {
        printk("Loaded device_info from flash: mode=%d affinity=%d level=%d\n", device_info.mode, device_info.affinity, device_info.level);
    }
    
    run_led_thread();
    main_loop();
    return 0;
}

