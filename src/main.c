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

/******* Type Definitions **************/
// Use enum for LED indexes for readability
typedef enum {
    ON_BOARD_LED = 0,
    LED_14,
    LED_15,
    LED_IDX_MAX
} led_index_t;

/******* Global Variables **************/
static struct nvs_fs fs;
const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
static uint8_t adv_data[16]; // Buffer for dynamic advertisement data

static bt_addr_le_t static_addr;

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

static struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_PASSIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_INTERVAL_MIN,
    .window = BT_GAP_SCAN_FAST_WINDOW,
};

device_info_t device_info = {
    .mode = MODE_AURA,
    .affinity = AFFINITY_UNITY,
    .level = 0
};
static bool mode_changed = false;

mode_device_state_t mode_device_state = {
    .is_on = false
};

static struct led_entry led_array[LED_IDX_MAX] = {
    { .state = LED_OFF, .gpio = &led },
    { .state = LED_OFF, .gpio = &led14 },
    { .state = LED_OFF, .gpio = &led15 }
};  

/******* Functions Declarations **************/
// --- Utility and Helper Functions ---
static void decode_device_info(uint8_t val, device_info_t *info);
static uint8_t encode_device_info(const device_info_t *info);
static void prepare_mesh_adv_data(uint8_t state);

// --- BLE Advertisement/Scan Handlers ---
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level);
static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state);
static void handle_zephyr_none(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state);
static void handle_zephyr_lvlup_token(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state);

// --- End-of-Cycle Handlers ---
static void end_of_cycle_aura(void);
static void end_of_cycle_device(void);
static void end_of_cycle_lvlup_token(void);
static void end_of_cycle_none(void);

// --- Mode/State Management ---
static void set_mode_handlers(operation_mode_t mode);

// --- BLE/Flash Initialization ---
static int init_flash(void);
static void generate_static_random_addr(bt_addr_le_t *addr);

// --- Main Loop and Entry Point ---
static void main_loop(void);
int main(void);

// --- BLE Scan Callback ---
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *buf);

/******* End Functions Declarations **************/


// Function pointer types for mode-specific handlers
typedef void (*zephyr_adv_handler_t)(const bt_addr_le_t *, device_info_t, uint8_t);
typedef void (*end_of_cycle_handler_t)(void);

// Function pointers for current mode
static zephyr_adv_handler_t current_zephyr_handler = handle_zephyr_none;
static end_of_cycle_handler_t current_end_of_cycle = end_of_cycle_none;


// Helper: Decode a single byte into device_info fields (2 bits per field)
static void decode_device_info(uint8_t val, device_info_t *info) {
    info->mode = (val >> 6) & 0x03;
    info->affinity = (val >> 4) & 0x03;
    info->level = (val >> 2) & 0x03;
    // bits 1:0 are reserved, ignored
}

// Handle master advertisements that may change device_info
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level) {
    // Ignore if target_mac does not match this device's MAC
    if (memcmp(target_mac, static_addr.a.val, MAC_LEN) != 0) {
        return;
    }
    device_info_t new_info;
    new_info.mode = mode;
    new_info.affinity = affinity;
    new_info.level = level;
    if (memcmp(&device_info, &new_info, sizeof(device_info_t)) != 0) {
        if (device_info.mode != new_info.mode) {
            mode_changed = true;
        }
        device_info = new_info;
        nvs_write(&fs, 1, &device_info, sizeof(device_info)); // Store new device_info in flash (ID 1)
    }
}

// Update handler signatures for new mesh/master adv formats
static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state) {
    if (peer_count >= MAX_PEERS) {
        return; // Peer list is full, ignore this advertisement
    }
    // Only process peers advertising MODE_AURA
    if (peer_info.mode != MODE_AURA) {
        return; // Only interested in AURA mode
    }
    // Check if MAC is already in peers array
    for (uint8_t i = 0; i < peer_count; ++i) {
        if (memcmp(addr->a.val, peers[i].mac, MAC_LEN) == 0) {
            return; // MAC already exists, ignore this advertisement
        }
    }
    // Store peer info and state in peers array
    memcpy(peers[peer_count].mac, addr->a.val, MAC_LEN);
    peers[peer_count].data = encode_device_info(&peer_info); // Store compressed peer_info in data field
    peer_count++;
    // Maintain counts of levels for each affinity using matrix
    if (peer_info.affinity <= AFFINITY_TECHNO && peer_info.level < LEVELS_PER_AFFINITY) {
        aura_level_count[peer_info.affinity][peer_info.level]++;
    }
}

// Set handlers based on mode
static void set_mode_handlers(operation_mode_t mode) {
    switch (mode) {
        case MODE_AURA:
            current_zephyr_handler = handle_zephyr_aura;
            current_end_of_cycle = end_of_cycle_device;
            break;
        case MODE_DEVICE:
            current_zephyr_handler = handle_zephyr_aura;
            current_end_of_cycle = end_of_cycle_device;
            break;
        case MODE_LVLUP_TOKEN:
            current_zephyr_handler = handle_zephyr_lvlup_token;
            current_end_of_cycle = end_of_cycle_lvlup_token;
            break;
        case MODE_NONE:
        default:
            current_zephyr_handler = handle_zephyr_none;
            current_end_of_cycle = end_of_cycle_none;
            break;
    }
}

// --- Mode-specific end-of-cycle handlers (implement as needed) ---
static void end_of_cycle_aura(void) {
    // Aura mode: implement any end-of-cycle logic
}
static void end_of_cycle_device(void) {
    // Count max level and number of peers at max level for each affinity
    uint8_t max_level[3] = {0};
    uint8_t peers_at_max_level[3] = {0};
    uint8_t highest_level = 0;
    uint8_t new_device_state = 1; // Default to ON
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

    uint8_t my_affinity = device_info.affinity;
    uint8_t my_peers_at_highest = aura_level_count[my_affinity][highest_level];

    // Got any?
    if (peers_at_max_level[AFFINITY_MAGIC] == 0 && peers_at_max_level[AFFINITY_TECHNO] == 0 && peers_at_max_level[AFFINITY_UNITY] == 0) {
        new_device_state = 0;
    } else if (my_peers_at_highest == 0) {
        new_device_state = 0;
    } else {
        for (int aff = 0; aff < 3; ++aff) {
            // If my affinity has peers at max level, but not the most peers, turn off LED_1
            if (aff != my_affinity && aura_level_count[aff][highest_level] > my_peers_at_highest) {
                new_device_state = 0;
                break;
            }
        }
    }
   
    // Reset peer and level counts for this cycle
    peer_count = 0;
    memset(aura_level_count, 0, sizeof(aura_level_count));

    if (new_device_state != mode_device_state.is_on) {
        mode_device_state.is_on = new_device_state;
        // Store new state in flash (ID 1)
        set_led_state(ON_BOARD_LED, mode_device_state.is_on ? LED_ON : LED_OFF);
    }    
}

static void end_of_cycle_lvlup_token(void) {
    // Token mode: implement any end-of-cycle logic
}
static void end_of_cycle_none(void) {
    // None mode: implement any end-of-cycle logic
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


// --- Refactor scan_cb to use current_zephyr_handler ---
static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    if (rssi < RSSI_THRESHOLD) {
        return; // Ignore weak signals
    }
    uint8_t mfg[16] = {0};
    int mfg_len = 0;
    struct net_buf_simple temp = *buf;
    while (temp.len > 1) {
        uint8_t length = net_buf_simple_pull_u8(&temp);
        if (length == 0 || length > temp.len) {
            break;
        }
        uint8_t type = net_buf_simple_pull_u8(&temp);
        length -= 1;
        if (type == BT_DATA_MANUFACTURER_DATA && length >= 2) {
            memcpy(mfg, temp.data, length > 16 ? 16 : length);
            mfg_len = length;
            break;
        }
        temp.data += length;
        temp.len -= length;
    }
    if (mfg_len >= MESH_ADV_LEN && mfg[0] == 0xCE && mfg[1] == 0xFA) {
        // Mesh device advertisement
        device_info_t peer_info;
        peer_info.mode = mfg[2];
        peer_info.affinity = mfg[3];
        peer_info.level = mfg[4];
        uint8_t state = mfg[5];
        // Call mesh handler (pass addr, peer_info, state)
        handle_zephyr_aura(addr, peer_info, state);
    } else if (mfg_len >= MASTER_ADV_LEN && mfg[0] == 0xAB && mfg[1] == 0xAC) {
        // Master advertisement
        uint8_t *target_mac = &mfg[2];
        uint8_t mode = mfg[2 + MAC_LEN];
        uint8_t affinity = mfg[3 + MAC_LEN];
        uint8_t level = mfg[4 + MAC_LEN];
        // Call master handler (pass addr, target_mac, mode, affinity, level)
        handle_master_adv(addr, target_mac, mode, affinity, level);
    }
}


// --- Unified main loop ---
static void main_loop(void)
{
    operation_mode_t last_mode = device_info.mode;
    set_mode_handlers(device_info.mode);
    mode_changed = false;
    while (1) {
        // --- Scanning phase ---

        int err;
        err = bt_le_scan_start(&scan_param, scan_cb);
        if (err) {
            printk("Scan start failed: %d\n", err);
        }
        // --- Advertising phase ---
        // For mesh device:
        prepare_mesh_adv_data(/* state: 1=on, 0=off, etc. */ (device_info.mode == MODE_DEVICE ? /* your logic here */ 1 : 0));
        struct bt_data dynamic_ad[] = {
            BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, MESH_ADV_LEN),
        };

        err = bt_le_adv_start(&adv_params, dynamic_ad, ARRAY_SIZE(dynamic_ad), NULL, 0);
        if (err) {
            printk("Adv start failed: %d\n", err);
        }
        //k_sleep(K_MSEC(CYCLE_DURATION_MS));
        operate_leds(CYCLE_DURATION_MS, BLINK_INTERVAL_MS); // Operate LEDs for 1.5 seconds, blink every 250ms
        bt_le_scan_stop();
        bt_le_adv_stop();
        k_sleep(K_MSEC(100)); // Sleep for 100ms to allow any pending operations to complete

        // --- End of cycle handler ---
        current_end_of_cycle();

        // Check for mode change
        if (device_info.mode != last_mode) {
            set_mode_handlers(device_info.mode);
            mode_changed = false;
            last_mode = device_info.mode;
            // Reset peer count and aura level counts and LED states
            peer_count = 0;
            memset(aura_level_count, 0, sizeof(aura_level_count));
            // Reset LED states
            set_led_state(ON_BOARD_LED, LED_OFF);
            set_led_state(LED_14, LED_OFF);
            set_led_state(LED_15, LED_OFF);
        }
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

int main(void)
{
    int err;

    // Array of pointers to LED gpio_dt_spec
    init_led_manager(led_array, LED_IDX_MAX); // 1 second interval, 3 LEDs

    if (init_flash() ) {
        return 1;
    }

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
    err = nvs_read(&fs, NVS_ID_DEVICE_INFO, &device_info, sizeof(device_info));
    if (err < 0) {
        // Not found, use default (already initialized)
        printk("No device_info in flash, using default\n");
    } else {
        printk("Loaded device_info from flash: mode=%d affinity=%d level=%d\n", device_info.mode, device_info.affinity, device_info.level);
    }
    
    set_led_state(ON_BOARD_LED, LED_BLINKING); // Start with blinking LED
    operate_leds(STARTUP_DELAY_MS, BLINK_INTERVAL_MS); // Operate LEDs for 5 second, blink every 250ms
    set_led_state(ON_BOARD_LED, LED_OFF);

    main_loop();
    return 0;
}

// Stub for handle_zephyr_none
static void handle_zephyr_none(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state) {
    // Do nothing
}

// Stub for handle_zephyr_lvlup_token
static void handle_zephyr_lvlup_token(const bt_addr_le_t *addr, device_info_t peer_info, uint8_t state) {
    // Implement as needed
}

// Prepares mesh advertisement data: [0xCE, 0xFA, mode, affinity, level, state]
static void prepare_mesh_adv_data(uint8_t state) {
    adv_data[0] = 0xCE;
    adv_data[1] = 0xFA;
    adv_data[2] = device_info.mode;
    adv_data[3] = device_info.affinity;
    adv_data[4] = device_info.level;
    adv_data[5] = state;
    // The rest of adv_data is unused for mesh adv (MESH_ADV_LEN bytes only)
}

