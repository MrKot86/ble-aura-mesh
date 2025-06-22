#ifndef MODE_DEFS_H
#define MODE_DEFS_H

// Flash
#define FLASH_AREA_OFFSET(storage_partition) 0x00038000
#define FLASH_AREA_SIZE(storage_partition) 0x4000
#define NVS_ID_STATIC_ADDR 2

// BLE/peer
#define MASTER_PREFIX "master_"
#define MASTER_PREFIX_LEN 7
#define MAC_STR_LEN 12
#define MAC_LEN 6
#define MAX_PEERS 255

// LED/bitmask
#define LED_STATE_MASK 0x01

// BLE scan/adv timing
#define SCAN_INTERVAL_MS 701  // prime number, ~0.7s
#define ADV_INTERVAL_MS 307   // prime number, ~0.3s
#define SCAN_JITTER_MS 50     // up to +/-50ms random jitter
#define ADV_JITTER_MS 30      // up to +/-30ms random jitter

#endif // MODE_DEFS_H
