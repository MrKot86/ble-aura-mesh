#ifndef MODE_TYPES_H
#define MODE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Use enum for LED indexes for readability
typedef enum {
    ON_BOARD_LED = 0,
    LED_14,
    LED_15,
    LED_IDX_MAX
} led_index_t;

typedef enum {
    // MASTER advertisement can change device mode
    ADV_TYPE_MASTER,
    // MESH advertisement is used for advertising device state
    ADV_TYPE_ZEPHYR,
    ADV_TYPE_UNKNOWN
} adv_name_type_t;

typedef enum {
    // Uninitialized or no operation
    MODE_NONE,
    // Aura pendant mode, affects devices around
    MODE_AURA,
    // Device mode, reacts to aura pendants around
    MODE_DEVICE,
    // Level-up token mode, used to level up aura pendants
    MODE_LVLUP_TOKEN,
    // Overseer mode, broadcasts device states to surrounding devices
    MODE_OVERSEER
} operation_mode_t;

typedef enum {
    AFFINITY_UNITY, // Unity device
    AFFINITY_MAGIC, // Magic device
    AFFINITY_TECHNO // Techno device
} affinity_t;

typedef struct {
    uint8_t mode; // operation_mode_t
    uint8_t affinity; // affinity_t
    uint8_t level; // 0 to 3, 4 = hostile environment
} device_info_t;

typedef enum {
    PEER_SLOT_EMPTY = 0,   // Never used
    PEER_SLOT_OCCUPIED,    // Currently used
    PEER_SLOT_DELETED      // Previously used, now deleted
} peer_slot_state_t;

typedef struct peer {
    peer_slot_state_t state; // Slot state for open addressing
    uint8_t mac[6]; // MAC address
    uint8_t affinity; // affinity_t (removed mode to save memory)
    uint8_t level; // 0 to 3, 4 = hostile environment
    int8_t stability_counter; // Positive: consecutive detections, Negative: consecutive misses
    uint8_t detected_this_cycle : 1; // Flag set if detected in current cycle
    uint8_t is_established : 1; // Flag set once peer reaches PEER_DETECTION_THRESHOLD
    uint8_t reserved : 6; // Reserved bits for future use
} peer_t;

typedef struct {
    uint8_t is_on;
    // Overseer tracking
    uint8_t overseer_mac[6]; // MAC of strongest overseer
    uint8_t tracked_mac[6]; // MAC of currently tracked overseer
    int8_t overseer_rssi; // RSSI of strongest overseer
    int8_t overseer_stability_counter; // Stability counter for overseer signal
    uint8_t overseer_detected_this_cycle : 1; // Overseer detected this cycle
    uint8_t overseer_state : 1; // State commanded by overseer
    uint8_t use_overseer : 1; // Use overseer state instead of internal calculation
    uint8_t reserved : 5; // Reserved bits
} mode_device_state_t;

typedef struct {
    uint8_t is_active; // Is the aura pendant active?
    uint8_t is_in_hostile_environment; // Is the aura pendant in hostile environment?
    uint8_t hostility_counter; // How long staying in hostile environment
} mode_aura_state_t;

typedef struct {
    uint8_t mac[6]; // MAC address of receiving aura pendant
    device_info_t device_info; // Device info to broadcast
    uint8_t broadcast_countdown; // Countdown for broadcasting
} mode_lvlup_token_state_t;

typedef struct {
    uint8_t broadcast_countdown; // Countdown for broadcasting device states
} mode_overseer_state_t;

typedef union {
    mode_device_state_t device;
    mode_aura_state_t aura;
    mode_lvlup_token_state_t lvlup_token;
    mode_overseer_state_t overseer;
} mode_state_t;


#ifdef __cplusplus
}
#endif

#endif // MODE_TYPES_H
