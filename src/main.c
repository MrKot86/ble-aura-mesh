/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * BLE Aura Mesh - Optimized Advertisement Protocol
 * 
 * Advertisement formats (nibble-packed for efficiency):
 * 
 * MESH (5 bytes): [0xCE][0xFA][mode|affinity][level|state][dynamic_rssi_threshold]
 *   - Reduces air time by 16.7% vs previous 6-byte format
 *   - mode/affinity/level/state packed in nibbles (4 bits each)
 *   - dynamic_rssi_threshold as signed byte (-128 to +127)
 * 
 * MASTER (12 bytes): [0xAB][0xAC][target_mac:6][device_info_t:4]
 *   - Used for remote device configuration
 * 
 * OVERSEER (10 bytes): [0xDE][0xAD][state_data:8]
 *   - Broadcasts calculated states for all device levels/affinities
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
#include <zephyr/sys/reboot.h>

#include "LEDManager.h"
#include "types.h"
#include "defines.h"

#define LED_NODE DT_ALIAS(led0)  // Maps to 'led0' in your board's device tree
#define LED_12_NODE DT_ALIAS(led12)  // Maps to 'led12' in your board's device tree
#define LED_13_NODE DT_ALIAS(led13)  // Maps to 'led13' in your board's device tree
#define LED_14_NODE DT_ALIAS(led14)  // Maps to 'led14' in your board's device tree
#define LED_15_NODE DT_ALIAS(led15)  // Maps to 'led15' in your board's device tree
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static const struct gpio_dt_spec led12 = GPIO_DT_SPEC_GET(LED_12_NODE, gpios);
static const struct gpio_dt_spec led13 = GPIO_DT_SPEC_GET(LED_13_NODE, gpios);
static const struct gpio_dt_spec led14 = GPIO_DT_SPEC_GET(LED_14_NODE, gpios);
static const struct gpio_dt_spec led15 = GPIO_DT_SPEC_GET(LED_15_NODE, gpios);


/******* Global Variables **************/

// Persistent storage for device state
static struct nvs_fs fs;
const struct device *flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

// Peer discovery and management
// Use a matrix for level counters: [hostile/friendly][level]
static uint8_t aura_level_count[2][LEVELS_PER_AFFINITY] = {{0}};
static peer_t peers[MAX_PEERS];
static uint8_t peer_count = 0; // Number of discovered peers


/* Custom advertising parameters */
static bt_addr_le_t static_addr;
static uint8_t adv_data[16]; // Buffer for dynamic advertisement data
static struct bt_data dynamic_ad[] = {
    BT_DATA(BT_DATA_MANUFACTURER_DATA, adv_data, MESH_ADV_LEN),
};
static struct bt_le_adv_param adv_params = {
    .id = BT_ID_DEFAULT,
    .sid = 0,
    .secondary_max_skip = 0,
    .options = BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_USE_NAME,
    .interval_min = BT_GAP_ADV_SLOW_INT_MIN,
    .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
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
    .mode = MODE_NONE,
    .affinity = AFFINITY_UNITY,
    .level = 0,
    .dynamic_rssi_threshold = 0 // 0 = disabled, use default RSSI_THRESHOLD
};

// LED Management
static struct led_entry led_array[LED_IDX_MAX] = {
    { .state = LED_OFF, .polarity = LED_NORMAL, .gpio = &led },       // On-board LED (normal)
    { .state = LED_OFF, .polarity = LED_NORMAL, .gpio = &led12 },     // LED 12 (inverted)
    { .state = LED_OFF, .polarity = LED_NORMAL, .gpio = &led13 },   // LED 13 (inverted)
    { .state = LED_OFF, .polarity = LED_NORMAL, .gpio = &led14 },     // LED 14 (normal)
    { .state = LED_OFF, .polarity = LED_NORMAL, .gpio = &led15 }      // LED 15 (normal)
};  

/******* Functions Declarations **************/
// --- Hash Table Functions ---
static uint8_t hash_mac(const uint8_t *mac);
static void count_peer(const uint8_t *mac, device_info_t *peer_info);
static bool peer_exists(const uint8_t *mac);
static void clear_peer_table(void);
static void age_peers(void);
static void age_overseer(void);
static void track_overseer(void);
static bool is_peer_valid_for_calculation(const peer_t *peer);
static void count_stable_peers_for_calculations(void);

// --- Utility and Helper Functions ---
static void prepare_mesh_adv_data(uint8_t state);
static void prepare_aura_mesh_adv_data(uint8_t state);
static void prepare_overseer_adv_data(void);
static bool check_dynamic_rssi_threshold(int8_t rssi);

static void count_stable_peers_for_calculations(void);
static void count_stable_peers_for_overseer_calculations(void);
static uint8_t split_unity_level(uint8_t level, affinity_t target_affinity);
#define TO_UNITY_LEVEL(magic_level, techno_level) \
    ((magic_level << 4) | (techno_level & 0x0F))

// --- Mode-specific initialization function declarations ---
static void init_mode_aura(void);
static void init_mode_device(void);
static void init_mode_lvlup_token(void);
static void init_mode_overseer(void);
static void init_mode_none(void);

// --- BLE Advertisement/Scan Handlers ---
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level, int8_t dynamic_threshold, int8_t rssi);
static void handle_zephyr_device(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_none(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_lvlup_token(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_zephyr_overseer(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi);
static void handle_overseer_adv(const bt_addr_le_t *addr, const uint8_t *data, int8_t rssi);

// --- End-of-Cycle Handlers ---
static void end_of_cycle_aura(void);
static void end_of_cycle_device(void);
static void end_of_cycle_lvlup_token(void);
static void end_of_cycle_overseer(void);
static void end_of_cycle_none(void);

// --- Mode/State Management ---
static void set_mode(operation_mode_t mode);

// --- BLE/Flash Initialization ---
static int init_flash(void);
static void generate_static_random_addr(bt_addr_le_t *addr);
static void system_restart(void);

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

// --- Hash Table Implementation ---

// XOR + shift hash function optimized for nRF51822
static uint8_t hash_mac(const uint8_t *mac) {
    uint8_t hash = 0;
    for (int i = 0; i < MAC_LEN; i++) {
        hash ^= mac[i];
        hash = (hash << 1) | (hash >> 7); // Rotate left by 1
    }
    return hash; // Use full 8-bit range for 256 slots
}

// Count peer and store its information into the hash table
// This function is called by the zephyr handlers to count unique peers and store their information
static void count_peer(const uint8_t *mac, device_info_t *peer_info) {
    if (peer_count >= MAX_PEERS) {
        return; // Peer table is full, ignore this advertisement
    }
    uint8_t slot = hash_mac(mac);
    uint8_t original_slot = slot;
    uint8_t first_deleted = MAX_PEERS;
    
    do {
        if (peers[slot].state == PEER_SLOT_EMPTY) {
            // Use empty slot or first deleted slot if available
            uint8_t target_slot = (first_deleted < MAX_PEERS) ? first_deleted : slot;
            peers[target_slot].state = PEER_SLOT_OCCUPIED;
            memcpy(peers[target_slot].mac, mac, MAC_LEN);
            peers[target_slot].affinity = peer_info->affinity;
            peers[target_slot].level = peer_info->level;
            peers[target_slot].stability_counter = 1; // First detection
            peers[target_slot].detected_this_cycle = 1;
            peers[target_slot].is_established = 0; // Not yet established
            peers[target_slot].reserved = 0;
            peer_count++;
            return;
        }
        
        if (peers[slot].state == PEER_SLOT_DELETED && first_deleted == MAX_PEERS) {
            first_deleted = slot; // Remember first deleted slot
        }
        
        if (peers[slot].state == PEER_SLOT_OCCUPIED &&
            memcmp(peers[slot].mac, mac, MAC_LEN) == 0) {
            // Update existing peer - only if not already detected this cycle
            if (!peers[slot].detected_this_cycle) {
                peers[slot].affinity = peer_info->affinity;
                peers[slot].level = peer_info->level;
                peers[slot].detected_this_cycle = 1; // Mark as detected this cycle
            }
            return;
        }
        
        // Linear probing with prime step
        slot = (slot + HASH_PROBE_STEP) % MAX_PEERS;
    } while (slot != original_slot);
}

// Check if a peer exists in the hash table
static bool peer_exists(const uint8_t *mac) {
    uint8_t slot = hash_mac(mac);
    uint8_t original_slot = slot;
    
    do {
        if (peers[slot].state == PEER_SLOT_EMPTY) {
            return false; // Not found
        }
        
        if (peers[slot].state == PEER_SLOT_OCCUPIED &&
            memcmp(peers[slot].mac, mac, MAC_LEN) == 0) {
            return true; // Found
        }
        
        // Continue probing through deleted slots
        slot = (slot + HASH_PROBE_STEP) % MAX_PEERS;
    } while (slot != original_slot);
    
    return false; // Not found
}

// Clear the entire peer table
static void clear_peer_table(void) {
    for (int i = 0; i < MAX_PEERS; i++) {
        peers[i].state = PEER_SLOT_EMPTY;
        peers[i].stability_counter = 0;
        peers[i].detected_this_cycle = 0;
        peers[i].is_established = 0;
        peers[i].reserved = 0;
    }
    peer_count = 0;
}

// Age peers based on detection flags and update stability counters
static void age_peers(void) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].state == PEER_SLOT_OCCUPIED) {
            if (peers[i].detected_this_cycle) {
                // Peer was detected this cycle
                if (peers[i].stability_counter < 0) {
                    peers[i].stability_counter = 1; // Reset to first detection after misses
                } else if (peers[i].stability_counter < PEER_DETECTION_THRESHOLD) {
                    peers[i].stability_counter++; // Increment consecutive detections
                    
                    // Mark as established once threshold is reached
                    if (peers[i].stability_counter >= PEER_DETECTION_THRESHOLD) {
                        peers[i].is_established = 1;
                    }
                }
                peers[i].detected_this_cycle = 0; // Reset flag for next cycle
            } else {
                // Peer was not detected this cycle
                if (peers[i].stability_counter > 0) {
                    peers[i].stability_counter = -1; // Reset to first miss after detections
                } else {
                    peers[i].stability_counter--; // Increment consecutive misses (negative)
                }
                
                // Remove peer if missed for PEER_MISS_THRESHOLD consecutive cycles
                if (peers[i].stability_counter <= -PEER_MISS_THRESHOLD) {
                    peers[i].state = PEER_SLOT_DELETED;
                    peer_count--;
                }
            }
        }
    }
}

// Check if peer should be included in calculations
// Peer is valid once it has been established (reached PEER_DETECTION_THRESHOLD)
static bool is_peer_valid_for_calculation(const peer_t *peer) {
    return (peer->state == PEER_SLOT_OCCUPIED && peer->is_established);
}

// --- End Hash Table Implementation ---

// Helper function to check if RSSI passes dynamic threshold for device mode
static bool check_dynamic_rssi_threshold(int8_t rssi) {
    // If dynamic threshold is 0, it's disabled - use default behavior
    if (device_info.dynamic_rssi_threshold == 0) {
        return true; // Dynamic threshold disabled
    }
    
    // Apply dynamic threshold
    return rssi >= device_info.dynamic_rssi_threshold;
}

// Split unity level into magic and techno components
// For Unity, it returns the biggest part
static uint8_t split_unity_level(uint8_t level, affinity_t target_affinity) {
    switch (target_affinity)
    {
    case AFFINITY_MAGIC:
        return (level >> 4) & 0x0F;
    case AFFINITY_TECHNO:
        return level & 0x0F;
    case AFFINITY_UNITY:
    default:
        break;
    }
    uint8_t magic_level = (level >> 4) & 0x0F; // Magic part
    uint8_t techno_level = level & 0x0F; // Techno part
    return magic_level > techno_level ? magic_level : techno_level;
}

// --- MODE_AURA handlers ---
static void init_mode_aura(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    mode_state.aura.is_active = 1; // Example: set aura as active by default
    // Set other aura state fields as needed

    prepare_aura_mesh_adv_data(mode_state.aura.is_active);
    set_led_state(GREEN_LED_PIN, LED_ON); // Set LEDs to ON initially
    set_led_state(RED_LED_PIN, LED_OFF); // Set problem LED off initially
    // Use slower intervals for high peer density environments
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
}

static void handle_zephyr_aura(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi) {
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA || ! state) {
        return; // Only interested in AURA mode
    }
    if (unlikely(peer_info->level == HOSTILE_ENVIRONMENT_LEVEL &&
        peer_info->affinity != device_info.affinity && 
        device_info.affinity != AFFINITY_UNITY)) {
        mode_state.aura.is_in_hostile_environment = 1;
        return;
    }
}

static void end_of_cycle_aura(void) {
    if (mode_state.aura.is_in_hostile_environment) {
        if ( mode_state.aura.hostility_counter < HOSTILE_ENVIRONMENT_TRESHOLD ) {
            // If in hostile environment, increase hostility counter
            mode_state.aura.hostility_counter++;
            set_led_state(GREEN_LED_PIN, mode_state.aura.is_active);
            set_led_state(RED_LED_PIN, mode_state.aura.is_active ? LED_BLINK_ONCE : LED_ON);
        }
        // Aura mode: check if hostility counter is high, if so, blink LEDs
        if (mode_state.aura.hostility_counter >= HOSTILE_ENVIRONMENT_TRESHOLD) {
            // Blink LEDs to indicate active aura mode
            set_led_state(GREEN_LED_PIN, LED_OFF);
            set_led_state(RED_LED_PIN, LED_ON);
            mode_state.aura.is_active = 0; // Disable aura
            prepare_aura_mesh_adv_data(mode_state.aura.is_active);
        }
        mode_state.aura.is_in_hostile_environment = 0; // Reset hostile environment state
    } else if (mode_state.aura.hostility_counter > 0) {
        mode_state.aura.hostility_counter--;
        // If hostility counter is zero, SET LEDs back to ON
        if (mode_state.aura.hostility_counter == 0) {
            set_led_state(GREEN_LED_PIN, LED_ON);
            set_led_state(RED_LED_PIN, LED_OFF);
            mode_state.aura.is_active = 1; // Enable aura
            prepare_aura_mesh_adv_data(mode_state.aura.is_active);
        } else {
            set_led_state(GREEN_LED_PIN, LED_BLINK_ONCE);
            set_led_state(RED_LED_PIN, LED_ON);
        }
    }
}

// --- MODE_DEVICE handlers ---
static void init_mode_device(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    mode_state.device.is_on = device_info.level ? 0 : 1; // Example: device starts off
    // Clear overseer tracking
    memset(mode_state.device.overseer_mac, 0, MAC_LEN);
    mode_state.device.overseer_rssi = -127; // Minimum RSSI
    mode_state.device.overseer_stability_counter = 0;
    mode_state.device.overseer_detected_this_cycle = 0;
    mode_state.device.overseer_state = 0;
    mode_state.device.use_overseer = 0;

    set_led_state(GREEN_LED_PIN, 
        mode_state.device.is_on ? LED_ON : LED_BLINK_ONCE);
    set_led_state(DEVICE_OUTPUT_PIN, mode_state.device.is_on);

    prepare_mesh_adv_data(mode_state.device.is_on);
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
}

static void handle_zephyr_device(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi){
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA || ! state ) {
        return; // Only interested in AURA mode
    }

    // Apply dynamic RSSI threshold if enabled
    if (!check_dynamic_rssi_threshold(rssi)) {
        return; // Signal too weak according to dynamic threshold
    }

    count_peer(addr->a.val, peer_info);
}

static void age_overseer()
{
    if (mode_state.device.overseer_stability_counter > 0)
    {
        mode_state.device.overseer_stability_counter = -1; // Reset to first miss
    }
    else
    {
        mode_state.device.overseer_stability_counter--; // Increment consecutive misses
    }

    // Stop using overseer if missed for too long
    if (mode_state.device.overseer_stability_counter <= -OVERSEER_MISS_THRESHOLD)
    {
        mode_state.device.use_overseer = 0;
        memset(mode_state.device.tracked_mac, 0, MAC_LEN);
        mode_state.device.overseer_rssi = -127;
    }
}

static void track_overseer() {
    // Age overseer tracking
    if ( ! mode_state.device.overseer_detected_this_cycle) {
        age_overseer();
        return; // Overseer was not detected this cycle
    }
    // Overseer was detected this cycle
    mode_state.device.overseer_detected_this_cycle = 0; // Reset flag
    if ( ! mode_state.device.use_overseer ) {
        // If overseer is not used, we track the strongest one
        memcpy(mode_state.device.tracked_mac, mode_state.device.overseer_mac, MAC_LEN);
        mode_state.device.overseer_stability_counter = 1; // Set to first detection
        return;
    }


    if ( ! memcmp(mode_state.device.overseer_mac, mode_state.device.tracked_mac, MAC_LEN) ) {
        // If overseer is our tracked one, update stability counter
        if (mode_state.device.overseer_stability_counter < 0) {
            mode_state.device.overseer_stability_counter = 1; // Reset to first detection
        } else if (mode_state.device.overseer_stability_counter < OVERSEER_DETECTION_THRESHOLD) {
            mode_state.device.overseer_stability_counter++; // Increment consecutive detections
            
            // Start using overseer once threshold is reached
            if (mode_state.device.overseer_stability_counter >= OVERSEER_DETECTION_THRESHOLD) {
                mode_state.device.use_overseer = 1;
                memcpy(mode_state.device.tracked_mac, mode_state.device.overseer_mac, MAC_LEN);
            }
        }
        return; 
    }
    // If overseer is not our tracked one, age it
    age_overseer();
    // If not tracking overseer after aging, start tracking the new one
    if ( ! mode_state.device.use_overseer ) {
        memcpy(mode_state.device.tracked_mac, mode_state.device.overseer_mac, MAC_LEN);
        mode_state.device.overseer_stability_counter = 1; // Set to first detection
    }
      
}

static void end_of_cycle_device(void) {
    // Age all peers (increment miss counters, remove old peers)
    age_peers();
    
    track_overseer();
    
    uint8_t new_device_state;
    uint8_t is_suppressed = 0;
    
    if (mode_state.device.use_overseer) {
        // Use overseer-commanded state
        new_device_state = mode_state.device.overseer_state;
    } else {
        // Count only stable peers (detected 3+ consecutive times) for calculations
        count_stable_peers_for_calculations();
        
        // Count max level and number of peers at max level for each affinity
        new_device_state = device_info.level ? 0 : 1; // Default to OFF except if level is 0
        for ( int level = HOSTILE_ENVIRONMENT_LEVEL ; level >= device_info.level; --level ) {
            if (aura_level_count[HOSTILE_AURAS_IDX][level] == 0 &&
                aura_level_count[FRIENDLY_AURAS_IDX][level] == 0) {
                continue; // No peers at this level, skip
            }
            // Check if there more or equal friendly auras than hostile auras at this level
            if (aura_level_count[FRIENDLY_AURAS_IDX][level] >= aura_level_count[HOSTILE_AURAS_IDX][level]) {
                // If friendly auras are equal or more than hostile, keep device ON
                new_device_state = 1;
                break; // Found a level where device can stay ON
            } else {
                // If hostile auras are more, turn device OFF
                new_device_state = 0;
                is_suppressed = 1;
                break;
            }
        }
    }

    if (new_device_state != mode_state.device.is_on) {
        mode_state.device.is_on = new_device_state;
        set_led_state(GREEN_LED_PIN, 
            mode_state.device.is_on ? LED_ON : LED_BLINK_ONCE);
        set_led_state(DEVICE_OUTPUT_PIN, 
            mode_state.device.is_on);
        if ( is_suppressed ) {
            set_led_state(RED_LED_PIN, LED_ON); // Indicate suppression
        } else {
            set_led_state(RED_LED_PIN, LED_OFF); // No suppression
        }
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
    set_led_state(GREEN_LED_PIN, LED_ON);
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

    if (device_info.affinity == AFFINITY_UNITY && 
        peer_info->affinity != AFFINITY_UNITY) {
        // Convert the peer affinity to Unity
        mode_state.lvlup_token.device_info.affinity = AFFINITY_UNITY;
        mode_state.lvlup_token.device_info.mode = MODE_AURA;
        mode_state.lvlup_token.device_info.dynamic_rssi_threshold = 0; // Default: no dynamic threshold
        if (peer_info->level == HOSTILE_ENVIRONMENT_LEVEL ) {
            // Unity token cannot be hostile - set to max friendly level
            peer_info->level = HOSTILE_ENVIRONMENT_LEVEL - 1 ;
        }
        if (peer_info->affinity == AFFINITY_MAGIC) {
            mode_state.lvlup_token.device_info.level = TO_UNITY_LEVEL(peer_info->level, 0);
        } else if (peer_info->affinity == AFFINITY_TECHNO) {
            mode_state.lvlup_token.device_info.level = TO_UNITY_LEVEL(0, peer_info->level);
        }
        // Save the peer's MAC address and set countdown
        memcpy(mode_state.lvlup_token.mac, addr->a.val, MAC_LEN);
        peer_count = 1; // Mark that we found a peer
        mode_state.lvlup_token.broadcast_countdown = LVLUP_TOKEN_BROADCAST_COUNTDOWN;
        return;
    }

    uint8_t current_level = peer_info->level;
    if ( peer_info->affinity == AFFINITY_UNITY ) {
        current_level = split_unity_level(peer_info->level, device_info.affinity);
    } else if ( peer_info->affinity != device_info.affinity ) {
        // If peer's affinity is not friendly, ignore it
        return;
    }

    // Check if level is less by 1 and affinity matches
    if (current_level != device_info.level - 1) {
        return; // Not valid to get a level-up
    }

    // Save the peer's MAC address and set countdown
    memcpy(mode_state.lvlup_token.mac, addr->a.val, MAC_LEN);
    peer_count = 1; // Mark that we found a peer
    mode_state.lvlup_token.broadcast_countdown = LVLUP_TOKEN_BROADCAST_COUNTDOWN;

    if ( peer_info->affinity == AFFINITY_UNITY ) {
        mode_state.lvlup_token.device_info.affinity = AFFINITY_UNITY;
        mode_state.lvlup_token.device_info.mode = MODE_AURA;
        mode_state.lvlup_token.device_info.dynamic_rssi_threshold = 0; // Default: no dynamic threshold
        if (device_info.affinity == AFFINITY_MAGIC) {
            mode_state.lvlup_token.device_info.level 
                = TO_UNITY_LEVEL(device_info.level, split_unity_level(peer_info->level, AFFINITY_TECHNO));
        } else if (device_info.affinity == AFFINITY_TECHNO) {
            mode_state.lvlup_token.device_info.level 
                = TO_UNITY_LEVEL(split_unity_level(peer_info->level, AFFINITY_MAGIC), device_info.level);
        }
    } else {
        // If the peer's affinity is not Unity, keep the same affinity
        mode_state.lvlup_token.device_info.affinity = peer_info->affinity;
        mode_state.lvlup_token.device_info.mode = MODE_AURA;
        mode_state.lvlup_token.device_info.level = device_info.level; // Give level-up to the target token
        mode_state.lvlup_token.device_info.dynamic_rssi_threshold = 0; // Default: no dynamic threshold
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
        memcpy(&adv_data[2 + MAC_LEN], &mode_state.lvlup_token.device_info, sizeof(device_info_t)); // Copy device info
        dynamic_ad[0].data_len = MASTER_ADV_LEN; // Set data length for dynamic advertisement
        // Blink LEDs indicate broadcast
        set_led_state(GREEN_LED_PIN, LED_BLINK_FAST);
        adv_params.interval_min = BT_GAP_ADV_FAST_INT_MIN_2;
        adv_params.interval_max = BT_GAP_ADV_FAST_INT_MAX_2;
    } else if ( mode_state.lvlup_token.broadcast_countdown == 1 ) {
        
        // broadcast device state and after that the MAC the tocken we gave level-up to
        if (device_info.level == 1) {
            prepare_mesh_adv_data(1); // Set state to 1 (active), lvl 1 tokens do not expire
            set_led_state(GREEN_LED_PIN, LED_ON);
            peer_count = 0; // Reset peer count after broadcasting
        } else {
            // For other levels, prepare as used
            prepare_mesh_adv_data(0); // Set state to 0 (used)
            // indicate that the level-up token is in "discharged" state
            set_led_state(GREEN_LED_PIN, LED_OFF);
            set_led_state(RED_LED_PIN, LED_BLINK_ONCE);
        }
        memcpy(adv_data + MESH_ADV_LEN, mode_state.lvlup_token.mac, MAC_LEN); // Copy target MAC
        dynamic_ad[0].data_len = MESH_ADV_LEN + MAC_LEN; // Set data length for dynamic advertisement
        adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
        adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
    } else {
        mode_state.lvlup_token.broadcast_countdown--;
    }
}

// --- MODE_OVERSEER handlers ---
static void init_mode_overseer(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    mode_state.overseer.broadcast_countdown = OVERSEER_BROADCAST_COUNTDOWN;
    // Overseer needs to see all auras as neutral to count them properly
    // Keep original level and affinity for proper peer classification
    
    prepare_overseer_adv_data();
    set_led_state(GREEN_LED_PIN, LED_BLINK_ONCE);
    adv_params.interval_min = BT_GAP_ADV_SLOW_INT_MIN;
    adv_params.interval_max = BT_GAP_ADV_SLOW_INT_MAX;
}

static void handle_zephyr_overseer(const bt_addr_le_t *addr, device_info_t *peer_info, uint8_t state, int8_t rssi) {
    // Only process peers advertising MODE_AURA
    if (peer_info->mode != MODE_AURA || !state) {
        return; // Only interested in active AURA mode
    }

    count_peer(addr->a.val, peer_info);
}

static void end_of_cycle_overseer(void) {
    // Age all peers (increment miss counters, remove old peers)
    age_peers();
    
    // Note: Peer counting for overseer calculations is done within prepare_overseer_adv_data()
    // for each affinity perspective separately
    
    if (mode_state.overseer.broadcast_countdown > 0) {
        mode_state.overseer.broadcast_countdown--;
        if (mode_state.overseer.broadcast_countdown == 0) {
            // Reset countdown for next cycle
            mode_state.overseer.broadcast_countdown = OVERSEER_BROADCAST_COUNTDOWN;
            
            // Prepare overseer advertisement data
            prepare_overseer_adv_data();
        }
    }
}

// --- MODE_NONE handlers ---
static void init_mode_none(void) {
    memset(&mode_state, 0, sizeof(mode_state));
    // No state to set for none

    set_led_state(GREEN_LED_PIN, LED_BLINK_ONCE); // Set LEDs to blink once initially
    set_led_state(RED_LED_PIN, LED_BLINK_ONCE);
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

// Handle master advertisements that may change device_info and dynamic threshold
static void handle_master_adv(const bt_addr_le_t *addr, const uint8_t *target_mac, uint8_t mode, uint8_t affinity, uint8_t level, int8_t dynamic_threshold, int8_t rssi) {
    // Ignore if target_mac does not match this device's MAC
    if (memcmp(target_mac, static_addr.a.val, MAC_LEN) != 0) {
        return;
    }
    
    device_info_t new_info;
    new_info.mode = mode;
    new_info.affinity = affinity;
    new_info.level = level;
    new_info.dynamic_rssi_threshold = dynamic_threshold;

    if ( new_info.affinity == AFFINITY_UNITY ) {
        if ( new_info.mode == MODE_DEVICE && (new_info.level >= 4) ) {
            // Unity device mode can only have a single level (0-3)
            return;
        } else if ( new_info.mode == MODE_AURA ) {
            // validate Unity aura levels
            uint8_t magic_level = split_unity_level(new_info.level, AFFINITY_MAGIC);
            uint8_t techno_level = split_unity_level(new_info.level, AFFINITY_TECHNO);
            if ( magic_level > 3 || techno_level > 3 ) {
                return; // Invalid level for Unity affinity
            }
        }
    }
    
    if (memcmp(&device_info, &new_info, sizeof(device_info_t)) != 0) {
        mode_changed = true;
        device_info = new_info;
        nvs_write(&fs, 1, &device_info, sizeof(device_info)); // Store new device_info in flash (ID 1)
    }
}

// Handle overseer advertisements in device mode
static void handle_overseer_adv(const bt_addr_le_t *addr, const uint8_t *data, int8_t rssi) {
    // Only process in device mode
    if (device_info.mode != MODE_DEVICE) {
        return;
    }
    
    // Apply dynamic RSSI threshold if enabled
    if (!check_dynamic_rssi_threshold(rssi)) {
        return; // Signal too weak according to dynamic threshold
    }
    
    // Check if this overseer is stronger than current one or if no overseer tracked
    if (rssi > mode_state.device.overseer_rssi || 
        memcmp(mode_state.device.overseer_mac, addr->a.val, MAC_LEN) == 0) {
        
        // Update overseer tracking
        memcpy(mode_state.device.overseer_mac, addr->a.val, MAC_LEN);
        mode_state.device.overseer_rssi = rssi;
        mode_state.device.overseer_detected_this_cycle = 1;
        
        // Extract state for this device's affinity and level
        uint8_t commanded_state = 0;
        if (device_info.affinity == AFFINITY_MAGIC && device_info.level >= 0 && device_info.level <= 3) {
            commanded_state = data[device_info.level]; // Magic levels at positions 0, 1, 2, 3 in data (after header)
        } else if (device_info.affinity == AFFINITY_TECHNO && device_info.level >= 0 && device_info.level <= 3) {
            commanded_state = data[device_info.level + 4]; // Techno levels at positions 4, 5, 6, 7 in data
        } else if (device_info.affinity == AFFINITY_UNITY && device_info.level >= 0 && device_info.level <= 3) {
            // For Unity, use the better of magic or techno state for this level
            uint8_t magic_state = data[device_info.level];
            uint8_t techno_state = data[device_info.level + 4];
            commanded_state = (magic_state || techno_state) ? 1 : 0;
        }
        
        mode_state.device.overseer_state = commanded_state;
    }
}

// Count stable peers for device state calculations
// Only includes peers detected for PEER_DETECTION_THRESHOLD consecutive cycles
static void count_stable_peers_for_calculations(void) {
    // Reset level counts
    memset(aura_level_count, 0, sizeof(aura_level_count));
    
    for (int i = 0; i < MAX_PEERS; i++) {
        if (is_peer_valid_for_calculation(&peers[i])) {
            // Maintain counts of levels for each affinity type using matrix
            if (peers[i].affinity == AFFINITY_UNITY) {
                // Unity is the only affinity that can be friendly to all levels
                aura_level_count[FRIENDLY_AURAS_IDX][split_unity_level(peers[i].level, device_info.affinity)]++;
            } else if (peers[i].affinity == device_info.affinity &&
                peers[i].level <= MAX_AURA_LEVEL) { 
                aura_level_count[FRIENDLY_AURAS_IDX][peers[i].level]++;
            } else if (device_info.affinity != AFFINITY_UNITY) {
                // Unity is the only affinity that has no hostile auras
                // If the peer's affinity is not friendly, count it as hostile
                aura_level_count[HOSTILE_AURAS_IDX][peers[i].level]++;
            }
        }
    }
}

// Count stable peers for overseer calculations from a specific affinity perspective
// This allows overseer to calculate states for each affinity independently
static void count_stable_peers_for_overseer_calculations(void) {
    // Reset level counts
    memset(aura_level_count, 0, sizeof(aura_level_count));
    
    for (int i = 0; i < MAX_PEERS; i++) {
        if (is_peer_valid_for_calculation(&peers[i])) {
            // Maintain counts of levels for each affinity type using matrix
            if (peers[i].affinity == AFFINITY_MAGIC) {
                aura_level_count[MAGIC_AURAS_IDX][peers[i].level]++;
            } else if (peers[i].affinity == AFFINITY_TECHNO) { 
                aura_level_count[TECHNO_AURAS_IDX][peers[i].level]++;
            } else {
                // Then it must be Unity
                aura_level_count[MAGIC_AURAS_IDX][split_unity_level(peers[i].level, AFFINITY_MAGIC)]++;
                aura_level_count[TECHNO_AURAS_IDX][split_unity_level(peers[i].level, AFFINITY_TECHNO)]++;
            }
        }
    }
}

// Set handlers based on mode
static void set_mode(operation_mode_t mode) {
    set_led_state(ON_BOARD_LED, LED_BLINK_FAST); // Start with blinking LED
    set_led_state(RED_LED_PIN, LED_BLINK_FAST);
    set_led_state(GREEN_LED_PIN, LED_BLINK_FAST);
    operate_leds(STARTUP_DELAY_MS, BLINK_INTERVAL_MS); // Operate LEDs for 5 second, blink every 250ms
    set_led_state(ON_BOARD_LED, LED_OFF);
    set_led_state(GREEN_LED_PIN, LED_OFF);
    set_led_state(RED_LED_PIN, LED_OFF);
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
        case MODE_OVERSEER:
            current_zephyr_handler = handle_zephyr_overseer;
            current_end_of_cycle = end_of_cycle_overseer;
            init_mode_overseer();
            break;
        case MODE_NONE:
        default:
            current_zephyr_handler = handle_zephyr_none;
            current_end_of_cycle = end_of_cycle_none;
            init_mode_none();
            break;
    }
    mode_changed = false;
    // Reset peer table and aura level counts and LED states
    clear_peer_table();
    memset(aura_level_count, 0, sizeof(aura_level_count));
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf)
{
    if (rssi < RSSI_THRESHOLD) {
        return; // Ignore weak signals
    }
    uint8_t mfg[16] = {0};
    int mfg_len = 0;
    struct net_buf_simple temp = *buf;
    device_info_t peer_info = {0};
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
        // Mesh device advertisement with nibble-packed format
        peer_info.mode = UNPACK_MODE(mfg[2]);
        peer_info.affinity = UNPACK_AFFINITY(mfg[2]);
        peer_info.level = UNPACK_LEVEL(mfg[3], peer_info.affinity);
        peer_info.dynamic_rssi_threshold = (int8_t)mfg[4];
        uint8_t state = UNPACK_STATE(mfg[3]);
        // Call mesh handler (pass addr, peer_info, state, rssi)
        current_zephyr_handler(addr, &peer_info, state, rssi);
    } else if (mfg_len >= MASTER_ADV_LEN && mfg[0] == 0xAB && mfg[1] == 0xAC) {
        // Master advertisement - format: [0xAB, 0xAC, target_mac[6], device_info_t]
        uint8_t *target_mac = &mfg[2];
        device_info_t new_device_info;
        memcpy(&new_device_info, &mfg[2 + MAC_LEN], sizeof(device_info_t));
        // Call master handler (pass addr, target_mac, new_device_info, rssi)
        handle_master_adv(addr, target_mac, new_device_info.mode, new_device_info.affinity, 
                         new_device_info.level, new_device_info.dynamic_rssi_threshold, rssi);
    } else if (mfg_len >= OVERSEER_ADV_LEN && mfg[0] == 0xDE && mfg[1] == 0xAD) {
        // Overseer advertisement
        handle_overseer_adv(addr, &mfg[2], rssi);
    }
}


// Prepares mesh advertisement data with nibble-packed format
// Format: [0xCE, 0xFA, mode|affinity, level|state, dynamic_rssi_threshold]
static void prepare_mesh_adv_data(uint8_t state) {
    adv_data[0] = 0xCE;
    adv_data[1] = 0xFA;
    adv_data[2] = PACK_MODE_AFFINITY(device_info.mode, device_info.affinity);
    adv_data[3] = PACK_LEVEL_STATE(device_info.level, state);
    adv_data[4] = (uint8_t)device_info.dynamic_rssi_threshold;
    dynamic_ad[0].data_len = MESH_ADV_LEN;
}

// Prepares aura mesh advertisement data with nibble-packed format
// Format: [0xCE, 0xFA, mode|affinity, level|state, dynamic_rssi_threshold]
static void prepare_aura_mesh_adv_data(uint8_t state) {
    adv_data[0] = 0xCE;
    adv_data[1] = 0xFA;
    adv_data[2] = PACK_MODE_AFFINITY(device_info.mode, device_info.affinity);
    adv_data[3] = PACK_AURA_LEVEL_STATE(device_info.level, state, device_info.affinity);
    adv_data[4] = (uint8_t)device_info.dynamic_rssi_threshold;
    dynamic_ad[0].data_len = MESH_ADV_LEN;
}

// Prepare overseer advertisement data: [0xDE, 0xAD, states_for_each_level_and_affinity]
// Format: [header] [magic_lvl0] [magic_lvl1] [magic_lvl2] [magic_lvl3] [techno_lvl0] [techno_lvl1] [techno_lvl2] [techno_lvl3]
// Each byte contains states for that level/affinity combination using same logic as device mode
static void prepare_overseer_adv_data(void) {
    adv_data[0] = 0xDE;
    adv_data[1] = 0xAD;

    // set default states for all levels
    memset(adv_data + 2, 0, 8); // Magic and Techno levels
    adv_data[2] = 1; // Magic level 0 ON
    adv_data[6] = 1; // Techno level 0 ON

    dynamic_ad[0].data_len = OVERSEER_ADV_LEN;
    
    // Calculate device states for Magic affinity devices (levels 0-3)
    count_stable_peers_for_overseer_calculations();

    int deciding_level = HOSTILE_ENVIRONMENT_LEVEL;

    for ( ; deciding_level > 0; --deciding_level) {
        if (aura_level_count[MAGIC_AURAS_IDX][deciding_level] == 0 &&
            aura_level_count[TECHNO_AURAS_IDX][deciding_level] == 0) {
            continue; // No peers at this level, skip
        }
        break; // Found a level with peers
    }

    if (deciding_level <= 0) {
        // No peers at any level, leave default states
        return;
    }

    if (deciding_level == HOSTILE_ENVIRONMENT_LEVEL) {
        if ( aura_level_count[MAGIC_AURAS_IDX][HOSTILE_ENVIRONMENT_LEVEL] ) {
            // If there are magic auras at hostile level, turn all techno devices OFF
            adv_data[6] = 0; // Techno levels OFF
        }
        if ( aura_level_count[TECHNO_AURAS_IDX][HOSTILE_ENVIRONMENT_LEVEL] ) {
            // If there are techno auras at hostile level, turn all magic devices OFF
            adv_data[2] = 0; // Magic levels OFF
        }
        return;
    }

    // Calculate which affinity has more peers at the deciding level
    for ( int i = deciding_level; i >= 0; --i) {
        // Check values for both Magic and Techno auras at deciding_level
        if (aura_level_count[MAGIC_AURAS_IDX][deciding_level] > aura_level_count[TECHNO_AURAS_IDX][deciding_level]) {
            // If magic auras are more, turn magic devices ON and techno devices OFF
            adv_data[2 + i] = 1; // Magic levels OFF
            adv_data[6 + i] = 0; // Techno levels OFF
        } else if (aura_level_count[TECHNO_AURAS_IDX][deciding_level] > aura_level_count[MAGIC_AURAS_IDX][deciding_level]) {
            // If techno auras are more, turn techno devices ON and magic devices OFF
            adv_data[2 + i] = 0; // Magic levels OFF
            adv_data[6 + i] = 1; // Techno levels OFF
        } else {
            // If equal, turn both ON
            adv_data[2 + i] = 1; // Magic levels ON
            adv_data[6 + i] = 1; // Techno levels ON
        }
    }
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

// Trigger system restart (similar to power cycle)
static void system_restart(void)
{
    sys_reboot(SYS_REBOOT_COLD); // Cold reset - most similar to power cycle
}

// --- Unified main loop ---
// Optimized for high peer density (120-130 peers) with:
// - 5 second scan cycles (vs 1.5s)
// - Slow advertisement intervals (1000ms vs 20-30ms)
// - Random jitter between scan start and advertisement start to maximize scanning window
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
        
        // Add random jitter to maximize scanning window before advertising
        // This allows more time to discover peers before adding RF noise
        uint32_t jitter_ms = sys_rand32_get() % PEER_DISCOVERY_JITTER_MS;
        operate_leds(jitter_ms, jitter_ms); // Random delay using LED operation
        
        // --- Advertising phase ---
        err = bt_le_adv_start(&adv_params, dynamic_ad, ARRAY_SIZE(dynamic_ad), NULL, 0);
        if (err) {
            printk("Adv start failed: %d\n", err);
        }
        
        // Continue scanning and advertising for the remaining cycle time
        operate_leds(CYCLE_DURATION_MS - jitter_ms, BLINK_INTERVAL_MS);
        bt_le_scan_stop();
        bt_le_adv_stop();
        operate_leds(100, BLINK_INTERVAL_MS); // 100ms delay to allow pending operations to complete

        // --- End of cycle handler ---
        current_end_of_cycle();

        // Check for mode change
        if (mode_changed) {
            set_mode(device_info.mode);
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


int main(void)
{
    int err;

    // Array of pointers to LED gpio_dt_spec
    init_led_manager(led_array, LED_IDX_MAX); // 1 second interval, 3 LEDs

    if (init_flash() ) {
        return 1;
    }

    // Initialize peer hash table
    clear_peer_table();

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

    main_loop();
    return 0;
}



