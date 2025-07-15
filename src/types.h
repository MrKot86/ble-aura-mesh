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
    MODE_LVLUP_TOKEN
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

typedef struct peer {
    uint8_t mac[6]; // MAC address
    device_info_t peer_info; // Peer's parameters 
} peer_t;

typedef struct {
    uint8_t is_on;
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

typedef union {
    mode_device_state_t device;
    mode_aura_state_t aura;
    mode_lvlup_token_state_t lvlup_token;
} mode_state_t;


#ifdef __cplusplus
}
#endif

#endif // MODE_TYPES_H
