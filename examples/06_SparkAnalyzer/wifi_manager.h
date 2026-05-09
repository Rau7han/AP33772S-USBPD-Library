/**
 * wifi_manager.h  —  Spark Analyzer Wi-Fi Manager
 * ─────────────────────────────────────────────────────────────────────────────
 * Handles Wi-Fi station connection with AP-mode fallback for provisioning.
 * Credentials are persisted in NVS via the Arduino Preferences library.
 *
 * Usage:
 *   WifiManager wifi("SparkAnalyzer", "spark1234");
 *   wifi.begin();          // Call once in setup()
 *   wifi.loop();           // Call every loop() iteration
 *   if (wifi.isConnected()) { ... }
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_WIFI_MANAGER_H
#define SPARK_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// How long to wait for STA connection before falling back to AP mode
#define WIFI_CONNECT_TIMEOUT_MS  15000UL
// How often to retry STA connection when in AP mode
#define WIFI_RETRY_INTERVAL_MS   30000UL

class WifiManager {
public:
    /**
     * @param apSSID     SSID for the configuration access point
     * @param apPassword Password for the configuration AP (8+ chars, or "" for open)
     */
    WifiManager(const char *apSSID = "SparkAnalyzer", const char *apPassword = "spark1234")
        : _apSSID(apSSID), _apPassword(apPassword),
          _connected(false), _apMode(false),
          _lastConnectAttempt(0), _connectStart(0) {}

    /** Call once from setup(). Loads saved credentials and starts connection. */
    void begin() {
        _prefs.begin("spark_wifi", false);
        _staSSID = _prefs.getString("ssid", "");
        _staPass = _prefs.getString("pass", "");
        _prefs.end();

        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);

        if (_staSSID.length() > 0) {
            _startSTA();
        } else {
            Serial.println(F("[WiFi] No saved credentials — starting AP for provisioning"));
            _startAP();
        }
    }

    /** Call every loop() iteration. Manages reconnection and state transitions. */
    void loop() {
        if (_apMode) {
            // Periodically try to reconnect to saved STA credentials
            if (_staSSID.length() > 0 &&
                millis() - _lastConnectAttempt >= WIFI_RETRY_INTERVAL_MS) {
                Serial.println(F("[WiFi] Retrying STA connection..."));
                _startSTA();
            }
            return;
        }

        // STA mode — check connection status
        if (WiFi.status() == WL_CONNECTED) {
            if (!_connected) {
                _connected = true;
                _apMode    = false;
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            }
        } else {
            if (_connected) {
                _connected = false;
                Serial.println(F("[WiFi] Disconnected — waiting for auto-reconnect..."));
            }
            // Fallback to AP if STA hasn't connected within timeout
            if (!_connected && millis() - _connectStart >= WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println(F("[WiFi] STA timeout — switching to AP mode"));
                _startAP();
            }
        }
    }

    /** Save new credentials and reconnect. */
    void setCredentials(const String &ssid, const String &password) {
        _prefs.begin("spark_wifi", false);
        _prefs.putString("ssid", ssid);
        _prefs.putString("pass", password);
        _prefs.end();
        _staSSID = ssid;
        _staPass = password;
        Serial.printf("[WiFi] Credentials saved for SSID: %s\n", ssid.c_str());
        _startSTA();
    }

    /** Returns true when connected to a Wi-Fi network (STA mode). */
    bool isConnected() const { return _connected && (WiFi.status() == WL_CONNECTED); }

    /** Returns true when running as an access point for provisioning. */
    bool isAPMode() const { return _apMode; }

    /** Returns the current IP address (STA or AP). */
    String getIP() const {
        return _apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    }

    /** Returns current SSID (or AP SSID in AP mode). */
    String getSSID() const {
        return _apMode ? String(_apSSID) : WiFi.SSID();
    }

    /** Returns RSSI in dBm, or 0 if not connected. */
    int getRSSI() const {
        return isConnected() ? WiFi.RSSI() : 0;
    }

private:
    const char *_apSSID;
    const char *_apPassword;
    String      _staSSID;
    String      _staPass;
    bool        _connected;
    bool        _apMode;
    uint32_t    _lastConnectAttempt;
    uint32_t    _connectStart;
    Preferences _prefs;

    void _startSTA() {
        _apMode = false;
        _connected = false;
        _connectStart = millis();
        _lastConnectAttempt = millis();
        WiFi.mode(WIFI_STA);
        WiFi.begin(_staSSID.c_str(), _staPass.c_str());
        Serial.printf("[WiFi] Connecting to '%s'...\n", _staSSID.c_str());
    }

    void _startAP() {
        _apMode = true;
        _connected = false;
        WiFi.mode(WIFI_AP);
        bool ok = (strlen(_apPassword) >= 8)
                  ? WiFi.softAP(_apSSID, _apPassword)
                  : WiFi.softAP(_apSSID);
        if (ok) {
            Serial.printf("[WiFi] AP mode: SSID='%s'  IP=%s\n",
                          _apSSID, WiFi.softAPIP().toString().c_str());
            Serial.println(F("[WiFi] Connect to the AP and POST /api/wifi with {\"ssid\":\"...\",\"pass\":\"...\"}"));
        } else {
            Serial.println(F("[WiFi] Failed to start AP!"));
        }
    }
};

#endif // SPARK_WIFI_MANAGER_H
