#ifndef MODE_DEFS_H
#define MODE_DEFS_H

// Flash
#define FLASH_AREA_OFFSET(storage_partition) 0x00038000
#define FLASH_AREA_SIZE(storage_partition) 0x4000
#define NVS_ID_DEVICE_INFO 1 // Device info ID in NVS
#define NVS_ID_STATIC_ADDR 2

// BLE/peer
#define MAC_LEN 6
#define MAX_PEERS 255

#define RSSI_THRESHOLD -70 // RSSI threshold for peer discovery
#define LVLUP_TOKEN_RSSI_THRESHOLD -45 // RSSI threshold for level-up token discovery (really close)

// --- Protocol/Format Length Defines ---
#define MESH_ADV_LEN 6
#define MASTER_ADV_LEN (2 + MAC_LEN + 3) // 2 prefix + MAC + 3 fields

// Timings
#define STARTUP_DELAY_MS 5000 // 5 seconds for startup timeout
#define CYCLE_DURATION_MS 1500 // 1 second cycle duration
#define BLINK_INTERVAL_MS 250 // 250ms blink interval for LEDs
#define SCAN_INTERVAL_MS 701  // prime number, ~0.7s
#define ADV_INTERVAL_MS 307   // prime number, ~0.3s
#define SCAN_JITTER_MS 50     // up to +/-50ms random jitter
#define ADV_JITTER_MS 30      // up to +/-30ms random jitter
#define LVLUP_TOKEN_BROADCAST_COUNTDOWN 3 // Broadcast countdown for level-up token

// --- Aura levels and stuff ---
#define HOSTILE_AURAS_IDX 0 // Index for hostile auras in aura_level_count
#define FRIENDLY_AURAS_IDX 1 // Index for Unity auras in aura_level_count
#define MAX_AURA_LEVEL 3 // Maximum aura level (0 to 3)
#define LEVELS_PER_AFFINITY 5 // Levels per affinity type (0, 1, 2, 3, 4 = hostile environment)
#define HOSTILE_ENVIRONMENT_LEVEL 4 // Level for hostile environment
#define HOSTILE_ENVIRONMENT_TRESHOLD 20 // Threshold for staying in hostile environment before becoming affected. 


#endif // MODE_DEFS_H
