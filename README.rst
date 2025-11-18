BLE Aura Mesh Firmware
=======================

Overview
--------
Advanced BLE mesh firmware for modified Holyiot YJ15044 BLE dongles (nRF51822-based) designed for Live Action Role-Playing (LARP) games. The system creates an interactive environment where devices communicate via optimized BLE advertisements, enabling dynamic gameplay based on character affinities and levels.

The firmware supports high-density deployments (120+ devices) with optimized peer tracking, nibble-packed advertisement protocol, and sophisticated state management for immersive LARP experiences.

Key Features
------------
- **Optimized BLE Protocol**: Nibble-packed advertisements (5 bytes)
- **High Peer Density Support**: Handles 120-130 simultaneous peers with hash table-based tracking
- **Multiple Operation Modes**: Aura pendants, interactive devices, level-up tokens, overseer mode
- **Dynamic Configuration**: Remote device configuration via master advertisements
- **Smart Peer Tracking**: Consecutive detection/miss logic with stability counters
- **Flexible LED Control**: Support for normal and inverted LED polarity
- **Power Management**: Optimized scan/advertise cycles for battery efficiency
- **Persistent Storage**: NVS-based flash storage for device configuration

Advertisement Protocol
----------------------
The firmware uses three optimized advertisement formats with nibble-packing to minimize air time 
and reduce RF congestion. The MESH advertisement was reduced from 6 bytes to 5 bytes (16.7% reduction) 
through efficient bit-packing.

**Nibble-Packing Details**:
    - **Mode field**: 4 bits (upper nibble of byte 3)
    - **Affinity field**: 4 bits (lower nibble of byte 3)
    - **Level field**: 4 bits (upper nibble of byte 4)
        - Magic/Techno: Stores level 0-4 directly (level 4 = hostile environment)
        - Unity: Unity level is capped at 3, thus both Magic and Techno levels are stored as upper 4 bits (0-3)
          bits (0-1) store magic level, bits (2-3) store techno level
    - **State field**: 4 bits (lower nibble of byte 4)
    - Macros: ``PACK_MODE_AFFINITY()``, ``PACK_LEVEL_STATE()``, ``UNPACK_*()``

**MESH Advertisement (5 bytes)**
    Format: ``[0xCE][0xFA][mode|affinity][level|state][dynamic_rssi_threshold]``
    
    - Nibble-packed mode, affinity, level, and state fields (4 bits each)
    - Dynamic RSSI threshold for signal filtering (-128 to +127)
    - Used for peer discovery and state broadcasting

**MASTER Advertisement (12 bytes)**
    Format: ``[0xAB][0xAC][target_mac:6][device_info_t:4]``
    
    - Remote device configuration and mode changes
    - Targeted to specific device MAC addresses
    - Updates mode, affinity, level, and dynamic RSSI threshold
    - Validates that Unity affinity cannot be set to level 4

**OVERSEER Advertisement (10 bytes)**
    Format: ``[0xDE][0xAD][state_data:8]``
    
    - Broadcasts calculated states for all device levels and affinities
    - Enables centralized control in large deployments
    - Provides state commands for both Magic and Techno affinities

Operation Modes
---------------

**MODE_AURA** - Character Aura Pendants
    Broadcasts the character's aura based on affinity (Magic/Techno/Unity) and level (0-3).
    Detects hostile environments and can disable aura in response.

**MODE_DEVICE** - Interactive Environment Props
    Reacts to nearby aura pendants by calculating friendly vs hostile aura balance.
    Supports dynamic RSSI thresholds for signal strength filtering.
    Can be controlled by overseer devices for centralized management.

**MODE_LVLUP_TOKEN** - Character Progression Items
    Enables level-up mechanics by detecting and upgrading nearby aura pendants.
    Supports affinity conversion (Magic/Techno ↔ Unity).
    Single-use tokens (except level 1) that discharge after use.

**MODE_OVERSEER** - Centralized Control Nodes
    Monitors all nearby auras and calculates actual device states.
    Broadcasts state commands based on affinity balance at each level.
    Ideal for large installations with 50+ devices.

**MODE_NONE** - Standby/Configuration Mode
    Default mode for unconfigured devices.
    Awaits master advertisement for configuration.

Affinity System
---------------
- **AFFINITY_UNITY**: Neutral affinity, friendly to all auras
- **AFFINITY_MAGIC**: One of two opposing affinities
- **AFFINITY_TECHNO**: Opposing affinity to Magic

**Unity Level Packing**:
        Unity affinity aura tokens use a shortened 4-bit level representation that stores the Magic and Techno levels in upper 4 bits of byte 3 of MESH advertisement. 
    This design ensures compatibility with the nibble-packed 
    advertisement protocol where only 4 bits are available for the level field.
    
    - Maximum Unity level of aura token is 3 (level 4 is irrelevant as unity cannot be hostile to anyone)
    - When converting a level 4 Magic/Techno aura token to Unity, the level is capped at 3
    - Unity aura tokens cannot be configured with level 4 (rejected by master advertisement validation)
    - Unity devices are friendly to all affinities and treat all auras neutrally

Performance Characteristics
---------------------------
- **Peer Capacity**: 255 peers (hash table with open addressing)
- **Scan Cycle**: 3.5 seconds with random jitter (120ms) for optimal peer discovery
- **Advertisement Intervals**: Slow intervals (1000ms) for reduced RF congestion
- **Peer Detection Threshold**: 2 consecutive detections to establish peer
- **Peer Miss Threshold**: 2 consecutive misses before removing peer
- **Overseer Detection**: 3 consecutive detections for stable tracking
- **RAM Utilization**: ~15KB (94% of nRF51822's 16KB RAM)

Hardware Requirements
---------------------
**Base Platform**: Holyiot YJ15044 BLE dongle (nRF51822)

**Modified GPIO Connections**:
    - Onboard LED: Status indicator
    - P0.12: Green LED (normal polarity)
    - P0.13: Red LED (normal polarity)
    - P0.14: Unused
    - P0.15: Device output signal (normal polarity)

**LED Polarity Support**:
    The firmware supports both normal (active-high) and inverted (active-low) LED connections.
    Configure polarity in the ``led_array`` initialization in ``main.c``.

Building and Flashing
----------------------
1. **Build the firmware**:
   
   Use nRF Connect SDK with Zephyr RTOS. The project targets the nRF51822 SoC.

2. **Configure device**:
   
   Edit ``device_info`` initialization in ``main.c`` or use master advertisements for runtime configuration.

3. **Flash the firmware**:
   
   Use nRF Command Line Tools, J-Link, or the provided ``flash-remote`` task for WSL-based flashing.

4. **Deploy devices**:
   
   Place aura pendants on players and interactive devices in the environment.

Configuration
-------------
Device configuration is stored in NVS (Non-Volatile Storage) and persists across power cycles:

- **Mode**: Operation mode (AURA, DEVICE, LVLUP_TOKEN, OVERSEER, NONE)
- **Affinity**: UNITY, MAGIC, or TECHNO
- **Level**: 0-3 for normal levels, 4 for hostile environment detection
- **Dynamic RSSI Threshold**: Optional signal strength filtering (0 = disabled)

Configuration can be changed via:
    1. Initial flash with default values in ``main.c``
    2. Master advertisement from another device or central controller
    3. Manual NVS write during development

Advanced Features
-----------------

**Dynamic RSSI Threshold**
    Allows runtime adjustment of signal strength filtering for device mode.
    Useful for adapting to different environments or gameplay scenarios.
    Value of 0 disables dynamic filtering (uses default -70 dBm).

**LED Polarity Control**
    Each LED can be configured as normal (active-high) or inverted (active-low).
    Supports mixed LED configurations for different hardware designs.

**Overseer Mode**
    Centralized control for large installations.
    Calculates device states based on global aura balance.
    Reduces computational load on individual devices.

**Hash Table Peer Tracking**
    Efficient peer storage with open addressing and prime number probing.
    Supports 255 concurrent peers in 16KB RAM.
    Consecutive detection/miss logic prevents flickering from RF noise.

Technical Details
-----------------
- **Compiler**: ARM GCC via nRF Connect SDK
- **RTOS**: Zephyr RTOS
- **BLE Stack**: Zephyr Bluetooth LE subsystem
- **Flash Storage**: NVS (Non-Volatile Storage) filesystem
- **Random Number Generation**: Hardware RNG for static MAC generation

For source code details, see comments in:
    - ``main.c``: Core application logic and mode handlers
    - ``types.h``: Data structure definitions
    - ``defines.h``: System constants and macros
    - ``LEDManager.c/h``: LED control with polarity support

License
-------
Original Zephyr code: Apache 2.0 License (Copyright 2015-2016 Intel Corporation)

Project-specific code: See individual file headers for licensing information.

Contributing
------------
This firmware is designed for specific LARP gameplay mechanics. Modifications should consider:
    - RAM constraints (16KB total on nRF51822)
    - BLE protocol backward compatibility
    - Power consumption in battery-powered deployments
    - RF congestion in high-density environments (120+ devices)
