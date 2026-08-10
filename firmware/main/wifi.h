// Optional WiFi station connection (main/Kconfig.projbuild -> "USB GPIO
// Extender" -> WiFi SSID/password, set via `idf.py menuconfig`).
//
// This is strictly additive: the USB serial protocol is the only control
// path and is fully independent of WiFi. WiFi (when configured) only adds
// a read-only status page and a status LED - never a second way to drive
// pins, and never something that can block or break USB operation if it
// fails, is slow, or isn't configured at all.
#pragma once

// Starts connecting to the configured network, or does nothing if
// USBGPIO_WIFI_SSID is empty. Returns immediately - connection (and
// everything gated on it: mDNS, the status HTTP server, the status LED)
// happens in the background via the event loop. Call once, after
// nvs_flash_init().
void wifi_start(void);
