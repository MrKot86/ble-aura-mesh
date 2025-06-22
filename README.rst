BLE Aura Mesh Firmware
=======================

Overview
--------
This project provides firmware for modified Holyiot YJ15044 BLE dongles (nRF51822-based boards) with additional LEDs connected to available GPIO pins. The firmware is designed for use in Live Action Role-Playing (LARP) games to simulate a character's "aura" and enable interactive environments that respond to these auras.

Key Features
------------
- Supports modified Holyiot YJ15044 BLE dongles (nRF51822)
- Controls extra LEDs attached to GPIO pins
- BLE-based communication for mesh-like interaction between devices
- Designed for immersive LARP experiences and interactive installations

Firmware Functionality
----------------------
The firmware enables each dongle to:
- Broadcast its presence and aura state via BLE
- Scan for nearby devices and react to their auras
- Control multiple LEDs to visually represent aura changes
- TBD: Support for multiple operation modes (aura pendants, interactive props, etc.)

Operation Modes
---------------
- Default Mode: Basic aura broadcasting and LED visualization
- TBD: Additional modes for advanced gameplay and interactivity

Hardware Modification
---------------------
- Add extra LEDs to available GPIO pins on the Holyiot YJ15044 dongle
- Ensure proper current limiting and wiring for safe operation

Usage
-----
1. Build the firmware using NRF Connect.
2. Flash the firmware to the modified Holyiot YJ15044 dongle.
3. Deploy devices to players or props in the LARP environment.

For more details on hardware modification and gameplay integration, see project documentation and source code comments.
