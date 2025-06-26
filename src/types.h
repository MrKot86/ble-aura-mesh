#ifndef MODE_TYPES_H
#define MODE_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    ADV_TYPE_MASTER,
    ADV_TYPE_ZEPHYR,
    ADV_TYPE_UNKNOWN
} adv_name_type_t;

typedef enum {
    MODE_NONE,
    MODE_AURA,
    MODE_DEVICE,
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
    uint8_t level; // 0, 1 or 2
} device_info_t;

typedef struct peer {
    uint8_t mac[6]; // MAC address
    uint8_t data; // 1st byte of manufacturer data 
} peer_t;

typedef struct {
    uint8_t is_on;
} mode_device_state_t;


#ifdef __cplusplus
}
#endif

#endif // MODE_TYPES_H
