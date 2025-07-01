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


/******* Global Variables **************/

// Persistent storage for device state
static struct nvs_fs fs;
const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

// Peer discovery and management
// Use a matrix for level counters: [affinity][level]
static uint8_t aura_level_count[3][4] = {{0}};
static peer_t peers[MAX_PEERS];
static uint8_t peer_count = 0; // Number of discovered peers


/* Custom advertising parameters */
static bt_addr_le_t static_addr;
static uint8_t adv_data[16]; // Buffer for dynamic advertisement data
static struct bt_le_adv_param adv_params = {
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

// Device information structures

static mode_state_t mode_state;
static bool mode_changed = false;
device_info_t device_info = {
    .mode = MODE_AURA,
    .affinity = AFFINITY_UNITY,
    .level = 0
};

// LED Management
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
static void count_peer(const bt_addr_le_t *addr, device_info_t *peer_info);
// --- LED Management ---
static void set_affinity_leds_state(enum led_state state);

// --- Mode-specific initialization function declarations ---
static void init_mode_aura(void);
static void init_mode_device(void);
static void init_mode_lvlup_token(void);
static void init_mode_none(void);

// --- BLE Advertisement/Scan Handlers ---
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level, int8_t rssi);
static void handle_zephyr_device(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_none(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_lvlup_token(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);

// --- End-of-Cycle Handlers ---
static void end_of_cycle_aura(void);
static void end_of_cycle_device(void);
static void end_of_cycle_lvlup_token(void);
static void end_of_cycle_none(void);

// --- Mode/State Management ---
static void set_mode(operation_mode_t mode);

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
// Update zephyr_adv_handler_t typedef to include rssi
typedef void (*zephyr_adv_handler_t)(const bt_addr_le_t *, device_info_t*, uint8_t, int8_t);
typedef void (*end_of_cycle_handler_t)(void);

// Function pointers for current mode
static zephyr_adv_handler_t current_zephyr_handler = handle_zephyr_none;
static end_of_cycle_handler_t current_end_of_cycle = end_of_cycle_none;


// --- MODE_AURA handlers ---
static void init_mode_aura(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    mode_state.aura.is_active = 1; // Example: set aura as active by default
    // Set other aura state fields as needed

    prepare_mesh_adv_data(mode_state.aura.hostility_counter);
    adv_params.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
    adv_params.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
}

static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi) {
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA) {
        return; // Only interested in AURA mode
    }
    if ( peer_info->affinity != device_info.affinity && 
         peer_info->affinity != AFFINITY_UNITY &&
         peer_info->level == HOSTILE_ENVIRONMENT_LEVEL ) {
        mode_state.aura.is_in_hostile_environment = 1;
        return;
    }
}

static void end_of_cycle_aura(void) {
    if (mode_state.aura.is_in_hostile_environment) {
        if ( mode_state.aura.hostility_counter < HOSTILE_ENVIRONMENT_TRESHOLD ) {
            // If in hostile environment, increase hostility counter
            mode_state.aura.hostility_counter++;
        }
        // Aura mode: check if hostility counter is high, if so, BLink LEDs
        if (mode_state.aura.hostility_counter >= HOSTILE_ENVIRONMENT_TRESHOLD) {
            // Blink LEDs to indicate active aura mode
            set_affinity_leds_state(LED_BLINKING);
        }
        mode_state.aura.is_in_hostile_environment = 0; // Reset hostile environment state
    } else if (mode_state.aura.hostility_counter > 0) {
        mode_state.aura.hostility_counter--;
        // If hostility counter is zero, SET LEDs back to ON
        if (mode_state.aura.hostility_counter == 0) {
            set_affinity_leds_state(LED_ON);
        }
    }
}

// --- MODE_DEVICE handlers ---
static void init_mode_device(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    mode_state.device.is_on = 0; // Example: device starts off
    // Set other device state fields as needed

    prepare_mesh_adv_data(mode_state.device.is_on);
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
}

static void handle_zephyr_device(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi){
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA) {
        return; // Only interested in AURA mode
    }

    count_peer(addr, peer_info);
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

    if (new_device_state != mode_state.device.is_on) {
        mode_state.device.is_on = new_device_state;
        // Store new state in flash (ID 1)
        set_led_state(ON_BOARD_LED, mode_state.device.is_on ? LED_ON : LED_OFF);
        prepare_mesh_adv_data(mode_state.device.is_on);
    }    
}

// --- MODE_LVLUP_TOKEN handlers ---
static void init_mode_lvlup_token(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    // Set lvlup_token state fields as needed

    prepare_mesh_adv_data(1);
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
    // indicate that the level-up token is in "charged" state
    set_led_state(ON_BOARD_LED, LED_ON); 
    set_led_state(LED_14, LED_ON);
}

static void handle_zephyr_lvlup_token(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi) {
    if ( rssi < LVLUP_TOKEN_RSSI_THRESHOLD) {
        return; // Ignore weak signals
    }
    if ( peer_count > 0 ) {
        return; // Already found a peer, ignore this advertisement
    }
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA) {
        return;
    }
    // Check if level is 0 and affinity matches
    if (peer_info->level == 0 && 
        peer_info->affinity == device_info.affinity) {
        // Save the peer's MAC address and set countdown
        memcpy(mode_state.lvlup_token.mac, addr->a.val, MAC_LEN);
        peer_count = 1; // Mark that we found a peer
        mode_state.lvlup_token.broadcast_countdown = LVLUP_TOKEN_BROADCAST_COUNTDOWN;
    }
}

static void end_of_cycle_lvlup_token(void) {
    if ( peer_count == 0 || mode_state.lvlup_token.broadcast_countdown == 0 ) {
        return; // No peers to process
    }

    if ( mode_state.lvlup_token.broadcast_countdown == 3 ) {
        adv_data[0] = 0xAB;
        adv_data[1] = 0xAC;
        memcpy(&adv_data[2], mode_state.lvlup_token.mac, MAC_LEN); // Copy target MAC
        adv_data[2 + MAC_LEN] = MODE_AURA;
        adv_data[3 + MAC_LEN] = device_info.affinity; // keep affinity
        adv_data[4 + MAC_LEN] = 1; // Set level to 1 for target token
        adv_data[5 + MAC_LEN] = 0;
        // Blink LEDs indicate broadcast
        set_led_state(ON_BOARD_LED, LED_BLINKING); 
        set_led_state(LED_14, LED_BLINKING);
        adv_params.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
        adv_params.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
    } else if ( mode_state.lvlup_token.broadcast_countdown == 1 ) {
        // broadcast device state and after that the MAC the tocken we gave level-up to
        prepare_mesh_adv_data(0); // Set state to 0 (used)
        memcpy(adv_data + MESH_ADV_LEN, mode_state.lvlup_token.mac, MAC_LEN); // Copy target MAC
        // indicate that the level-up token is in "discharged" state
        set_led_state(ON_BOARD_LED, LED_OFF); 
        set_led_state(LED_14, LED_OFF);
        adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
        adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
    } else {
        mode_state.lvlup_token.broadcast_countdown--;
    }
}

// --- MODE_NONE handlers ---
static void init_mode_none(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    // No state to set for none

    prepare_mesh_adv_data(0);
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
}

static void handle_zephyr_none(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi) {
    // Do nothing
}

static void end_of_cycle_none(void) {
    // None mode: do nothing
}

// --- Common/utility handlers ---
// Helper: Decode a single byte into device_info fields (2 bits per field)
static void decode_device_info(uint8_t val, device_info_t *info) {
    info->mode = (val >> 6) & 0x03;
    info->affinity = (val >> 4) & 0x03;
    info->level = (val >> 2) & 0x03;
    // bits 1:0 are reserved, ignored
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


// Handle master advertisements that may change device_info
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level, int8_t rssi) {
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

// Count peer and store its information
// This function is called by the zephyr handlers to count unique peers and store their information
static void count_peer(const bt_addr_le_t *addr, device_info_t *peer_info) {
    if (peer_count >= MAX_PEERS) {
        return; // Peer list is full, ignore this advertisement
    }

    // Check if MAC is already in peers array
    for (uint8_t i = 0; i < peer_count; ++i) {
        if (memcmp(addr->a.val, peers[i].mac, MAC_LEN) == 0) {
            return; // MAC already exists, ignore this advertisement
        }
    }
    // Store peer info in peers array
    memcpy(peers[peer_count].mac, addr->a.val, MAC_LEN);
    peers[peer_count].data = encode_device_info(peer_info); // Store compressed peer_info in data field
    peer_count++;
    // Maintain counts of levels for each affinity using matrix
    if (peer_info->affinity <= AFFINITY_TECHNO && peer_info->level < LEVELS_PER_AFFINITY) {
        aura_level_count[peer_info->affinity][peer_info->level]++;
    }
}

// Set handlers based on mode
static void set_mode(operation_mode_t mode) {
    set_led_state(ON_BOARD_LED, LED_BLINKING); // Start with blinking LED
    operate_leds(STARTUP_DELAY_MS, BLINK_INTERVAL_MS); // Operate LEDs for 5 second, blink every 250ms
    set_led_state(ON_BOARD_LED, LED_OFF);
    switch (mode) {
        case MODE_AURA:
            current_zephyr_handler = handle_zephyr_aura;
            current_end_of_cycle = end_of_cycle_aura;
            init_mode_aura();
            break;
        case MODE_DEVICE:
            current_zephyr_handler = handle_zephyr_device;
            current_end_of_cycle = end_of_cycle_device;
            init_mode_device();
            break;
        case MODE_LVLUP_TOKEN:
            current_zephyr_handler = handle_zephyr_lvlup_token;
            current_end_of_cycle = end_of_cycle_lvlup_token;
            init_mode_lvlup_token();
            break;
        case MODE_NONE:
        default:
            current_zephyr_handler = handle_zephyr_none;
            current_end_of_cycle = end_of_cycle_none;
            init_mode_none();
            break;
    }
    mode_changed = false;
    // Reset peer count and aura level counts and LED states
    peer_count = 0;
    memset(aura_level_count, 0, sizeof(aura_level_count));
    // Reset LED states
    set_led_state(ON_BOARD_LED, LED_OFF);
    set_led_state(LED_14, LED_OFF);
    set_led_state(LED_15, LED_OFF);
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
        // Call mesh handler (pass addr, peer_info, state, rssi)
        handle_zephyr_aura(addr, &peer_info, state, rssi);
    } else if (mfg_len >= MASTER_ADV_LEN && mfg[0] == 0xAB && mfg[1] == 0xAC) {
        // Master advertisement
        uint8_t *target_mac = &mfg[2];
        uint8_t mode = mfg[2 + MAC_LEN];
        uint8_t affinity = mfg[3 + MAC_LEN];
        uint8_t level = mfg[4 + MAC_LEN];
        // Call master handler (pass addr, target_mac, mode, affinity, level, rssi)
        handle_master_adv(addr, target_mac, mode, affinity, level, rssi);
    }
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

// Generate a static random MAC address
static void generate_static_random_addr(bt_addr_le_t *addr)
{
    addr->type = BT_ADDR_LE_RANDOM;
    for (int i = 0; i < 6; ++i) {
        addr->a.val[i] = sys_rand32_get() & 0xFF;
    }
    addr->a.val[5] |= 0xC0;
    addr->a.val[5] &= 0xC3;
}

// --- Unified main loop ---
static void main_loop(void)
{
    set_mode(device_info.mode);
    while (1) {
        // --- Scanning phase ---

        int err;
        err = bt_le_scan_start(&scan_param, scan_cb);
        if (err) {
            printk("Scan start failed: %d\n", err);
        }
        // --- Advertising phase ---
        struct bt_data dynamic_ad[] = {
            BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, MESH_ADV_LEN),
        };

        err = bt_le_adv_start(&adv_params, dynamic_ad, ARRAY_SIZE(dynamic_ad), NULL, 0);
        if (err) {
            printk("Adv start failed: %d\n", err);
        }
        operate_leds(CYCLE_DURATION_MS, BLINK_INTERVAL_MS); // Operate LEDs for 1.5 seconds, blink every 250ms
        bt_le_scan_stop();
        bt_le_adv_stop();
        k_sleep(K_MSEC(100)); // Sleep for 100ms to allow any pending operations to complete

        // --- End of cycle handler ---
        current_end_of_cycle();

        // Check for mode change
        if (mode_changed) {
            set_mode(device_info.mode);
        }
    }
}

// Set the state of affinity LEDs based on device_info.affinity
static void set_affinity_leds_state(enum led_state state) {
    switch (device_info.affinity) {
        case AFFINITY_MAGIC:
            set_led_state(LED_14, state);
            break;
        case AFFINITY_TECHNO:
            set_led_state(LED_15, state);
            break;
        case AFFINITY_UNITY:
            set_led_state(LED_14, state);
            set_led_state(LED_15, state);
            break;
        default:
            // Invalid affinity, do nothing
            break;
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


