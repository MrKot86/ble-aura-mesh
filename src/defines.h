#ifndef MODE_DEFS_H
#define MODE_DEFS_H

// Flash
#define FLASH_AREA_OFFSET(storage_partition) 0x00038000
#define FLASH_AREA_SIZE(storage_partition) 0x4000
#define NVS_ID_DEVICE_INFO 1 // Device info ID in NVS
#define NVS_ID_STATIC_ADDR 2

// BLE/peer
#define MAC_LEN 6
#define MAX_PEERS 255  // Maximize peer capacity within RAM constraints
#define HASH_PROBE_STEP 7  // Prime number for linear probing step

#define RSSI_THRESHOLD -70 // RSSI threshold for peer discovery
#define LVLUP_TOKEN_RSSI_THRESHOLD -45 // RSSI threshold for level-up token discovery (really close)

// Dynamic RSSI threshold feature:
// - Stored in device_info_t.dynamic_rssi_threshold
// - Can be set via master advertisement for any device mode
// - Value 0 = disabled (use default RSSI_THRESHOLD)
// - If set higher than RSSI_THRESHOLD, effectively no additional filtering
// - Applied to aura and overseer advertisements in device mode, extensible to other modes

// Peer tracking thresholds
#define PEER_DETECTION_THRESHOLD 2  // Consecutive detections needed to include peer in calculations
#define PEER_MISS_THRESHOLD 2       // Consecutive misses before excluding peer from calculations
#define OVERSEER_DETECTION_THRESHOLD 3  // Consecutive detections needed to trust overseer
#define OVERSEER_MISS_THRESHOLD 6       // Consecutive misses before ignoring overseer

// --- Bit-packing Helper Macros ---
// Advertisement data is nibble-packed to reduce air time and RF congestion
#define PACK_MODE_AFFINITY(mode, affinity) (((mode) << 4) | ((affinity) & 0x0F))
#define PACK_LEVEL_STATE(level, state) (((level) << 4) | ((state) & 0x0F) )
#define PACK_AURA_LEVEL_STATE(level, state, affinity) ((affinity != AFFINITY_UNITY ?  ((level) << 4) | ((state) & 0x0F) : (((level & 0x03) << 4) | ((level & 0x30) << 2))) | ((state) & 0x0F))
#define UNPACK_MODE(byte) (((byte) >> 4) & 0x0F)
#define UNPACK_AFFINITY(byte) ((byte) & 0x0F)
#define UNPACK_LEVEL(byte, affinity) (affinity != AFFINITY_UNITY ? ((byte) >> 4) & 0x0F : (((byte) >> 4) & 0x03) || (((byte) >> 2) & 0x30) )
#define UNPACK_STATE(byte) ((byte) & 0x0F)

// --- Protocol/Format Length Defines ---
#define MESH_ADV_LEN 5 // Optimized: [header:2][mode|affinity:1][level|state:1][dynamic_rssi:1]
#define MASTER_ADV_LEN (2 + MAC_LEN + sizeof(device_info_t)) // 2 prefix + MAC + device_info_t structure
#define OVERSEER_ADV_LEN 10 // 2 prefix + 8 bytes for state data (4 levels × 2 affinities)

// Timings - Optimized for 120-130 peer density with responsive device state changes
#define STARTUP_DELAY_MS 5000 // 5 seconds for startup timeout
#define CYCLE_DURATION_MS 3500 // 3.5 second cycle duration - balanced responsiveness/discovery
#define BLINK_INTERVAL_MS 250 // 250ms blink interval for LEDs
#define SCAN_INTERVAL_MS 701  // prime number, ~0.7s
#define ADV_INTERVAL_MS 307   // prime number, ~0.3s
#define SCAN_JITTER_MS 50     // up to +/-50ms random jitter
#define ADV_JITTER_MS 30      // up to +/-30ms random jitter
#define PEER_DISCOVERY_JITTER_MS 120 // Optimal jitter for 120-130 peers (reduced from 200ms)
#define LVLUP_TOKEN_BROADCAST_COUNTDOWN 3 // Broadcast countdown for level-up token
#define OVERSEER_BROADCAST_COUNTDOWN 10 // Broadcast countdown for overseer mode

// --- Aura levels and stuff ---
#define HOSTILE_AURAS_IDX 0 // Index for hostile auras in aura_level_count
#define FRIENDLY_AURAS_IDX 1 // Index for Unity auras in aura_level_count
#define MAGIC_AURAS_IDX 0 // Index for Magic auras in aura_level_count (used by overseer)
#define TECHNO_AURAS_IDX 1 // Index for Techno auras in aura_level_count (used by overseer)
#define MAX_AURA_LEVEL 3 // Maximum aura level (0 to 3)
#define LEVELS_PER_AFFINITY 5 // Levels per affinity type (0, 1, 2, 3, 4 = hostile environment)
#define HOSTILE_ENVIRONMENT_LEVEL 4 // Level for hostile environment
#define HOSTILE_ENVIRONMENT_TRESHOLD 20 // Threshold for staying in hostile environment before becoming affected. 

// --- PINs assignments ---
#define GREEN_LED_PIN LED_12
#define RED_LED_PIN LED_13
#define DEVICE_OUTPUT_PIN LED_15 // Pin for device output signal


#endif // MODE_DEFS_H
