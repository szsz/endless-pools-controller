#pragma once
#include <Arduino.h>

// Centralized network configuration and globals

// Define network pins/creds BEFORE including ConnectionManager.h so its defaults are overridden

// ---------- Ethernet (W5500 over SPI) ----------
#define ETH_TYPE     ETH_PHY_W5500
#define ETH_ADDR     1
#define ETH_CS       14
#define ETH_IRQ      10
#define ETH_RST      9
#define ETH_SPI_SCK  13
#define ETH_SPI_MISO 12
#define ETH_SPI_MOSI 11


// SoftAP fallback (distinct creds)
#define SOFT_AP_SSID "swimmachine"
#define SOFT_AP_PASS "12345678"

// SoftAP IP/mask (used by ConnectionManager via SOFT_AP_*_OCTETS)
#define SOFT_AP_IP_OCTETS   192, 168, 4, 1
#define SOFT_AP_MASK_OCTETS 255, 255, 255, 0

// ---------- Unified hostname (prefer Ethernet) ----------
static const char* HOSTNAME = "swimmachine";

#include "ConnectionManager.h"

/* Global managers (defined in NetworkSetup.cpp) */
extern ConnectionManager g_conn;

/* Network utility functions */
void applyWifiConfigFromFile();

void saveWifiConfigToFile(const String& ssid, const String& password);
// Apply Wi-Fi credentials immediately (can be called anytime)
void setNewWifiCredentials(const String& ssid, const String& password);
