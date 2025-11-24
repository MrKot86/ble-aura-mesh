/* errors.h - Error code definitions */

/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ERRORS_H
#define ERRORS_H

// Error codes for BLE Aura Mesh system
// Negative values indicate errors, 0 indicates success

#define ERROR_SUCCESS                       0
#define ERROR_FLASH_NOT_READY              -1
#define ERROR_FLASH_PAGE_INFO              -2
#define ERROR_NVS_MOUNT                    -3
#define ERROR_BT_ENABLE                    -4
#define ERROR_BT_ID_GET                    -5
#define ERROR_ADV_START                    -6
#define ERROR_SCAN_START                   -7
#define ERROR_LED_INIT                     -8
#define ERROR_GPIO_NOT_READY               -9

#endif /* ERRORS_H */
