/**
 * api_server.h  —  Spark Analyzer REST API Server
 * ─────────────────────────────────────────────────────────────────────────────
 * Implements an HTTP REST API on port 80 using the ESP32 WebServer library.
 * Serves the live web dashboard on GET / and provides JSON endpoints.
 *
 * Endpoints:
 *   GET  /               — Embedded web dashboard (HTML)
 *   GET  /api/status     — Current measurements & connection info (JSON)
 *   GET  /api/pdos       — All discovered PDOs (JSON)
 *   POST /api/voltage    — Set voltage {"voltage_mv":N,"current_ma":N}
 *   POST /api/pps        — Request PPS {"voltage_mv":N,"current_ma":N}
 *   POST /api/avs        — Request AVS {"voltage_mv":N,"current_ma":N}
 *   POST /api/output     — Control VOUT switch {"on":true|false}
 *   POST /api/reset      — Issue PD hard reset
 *   POST /api/wifi       — Save Wi-Fi credentials {"ssid":"...","pass":"..."}
 *
 * All POST endpoints accept Content-Type: application/json.
 * All endpoints respond within 100 ms under normal conditions.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_API_SERVER_H
#define SPARK_API_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "AP33772S.h"
#include "web_dashboard.h"

// Forward declaration
class WifiManager;

// ── Minimal JSON parse helpers (no external library required) ─────────────────
static int32_t _jsonInt(const String &body, const char *key, int32_t fallback = 0) {
    String search = String("\"") + key + "\"";
    int idx = body.indexOf(search);
    if (idx < 0) return fallback;
    int colon = body.indexOf(':', idx + search.length());
    if (colon < 0) return fallback;
    // Skip whitespace
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
    return body.substring(start).toInt();
}

static bool _jsonBool(const String &body, const char *key, bool fallback = false) {
    String search = String("\"") + key + "\"";
    int idx = body.indexOf(search);
    if (idx < 0) return fallback;
    int colon = body.indexOf(':', idx + search.length());
    if (colon < 0) return fallback;
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
    return body.substring(start).startsWith("true");
}

static String _jsonStr(const String &body, const char *key, const String &fallback = "") {
    String search = String("\"") + key + "\"";
    int idx = body.indexOf(search);
    if (idx < 0) return fallback;
    int colon = body.indexOf(':', idx + search.length());
    if (colon < 0) return fallback;
    int q1 = body.indexOf('"', colon + 1);
    if (q1 < 0) return fallback;
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return fallback;
    return body.substring(q1 + 1, q2);
}

// ─────────────────────────────────────────────────────────────────────────────

class APIServer {
public:
    /**
     * @param pd         Reference to the AP33772S object
     * @param wifiMgr    Pointer to WifiManager (for Wi-Fi provisioning endpoint)
     * @param port       TCP port (default 80)
     */
    APIServer(AP33772S &pd, WifiManager *wifiMgr = nullptr, uint16_t port = 80)
        : _pd(pd), _wifiMgr(wifiMgr), _server(port),
          _outputOn(true), _pendingHardReset(false) {}

    /** Register routes and start the HTTP server. Call once from setup(). */
    void begin() {
        _server.on("/",          HTTP_GET,  [this]() { _handleDashboard(); });
        _server.on("/api/status",HTTP_GET,  [this]() { _handleStatus();    });
        _server.on("/api/pdos",  HTTP_GET,  [this]() { _handlePDOs();      });
        _server.on("/api/voltage",HTTP_POST,[this]() { _handleSetVoltage(); });
        _server.on("/api/pps",   HTTP_POST, [this]() { _handleSetPPS();    });
        _server.on("/api/avs",   HTTP_POST, [this]() { _handleSetAVS();    });
        _server.on("/api/output",HTTP_POST, [this]() { _handleOutput();    });
        _server.on("/api/reset", HTTP_POST, [this]() { _handleReset();     });
        _server.on("/api/wifi",  HTTP_POST, [this]() { _handleWifi();      });
        _server.onNotFound(      [this]() {
            _server.send(404, "application/json", "{\"error\":\"Not found\"}");
        });
        _server.begin();
        Serial.println(F("[API] HTTP server started on port 80"));
        Serial.println(F("[API] Dashboard: http://<IP>/"));
    }

    /** Handle incoming HTTP requests. Call every loop() iteration. */
    void loop() {
        _server.handleClient();
        if (_pendingHardReset) {
            _pendingHardReset = false;
            _pd.issueHardReset();
        }
    }

    /** Returns current state of the VOUT switch. */
    bool isOutputOn() const { return _outputOn; }

private:
    AP33772S    &_pd;
    WifiManager *_wifiMgr;
    WebServer    _server;
    bool         _outputOn;
    bool         _pendingHardReset;

    // ── CORS helper ──────────────────────────────────────────────────────────
    void _cors() {
        _server.sendHeader("Access-Control-Allow-Origin",  "*");
        _server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        _server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    }

    // ── GET / ────────────────────────────────────────────────────────────────
    void _handleDashboard() {
        _cors();
        _server.send_P(200, "text/html", SPARK_DASHBOARD_HTML);
    }

    // ── GET /api/status ──────────────────────────────────────────────────────
    void _handleStatus() {
        _cors();
        uint16_t v   = _pd.getVoltage_mV();
        uint16_t i   = _pd.getCurrent_mA();
        uint32_t p   = _pd.getPower_mW();
        int8_t   t   = _pd.getTemperature_C();
        uint16_t vr  = _pd.getRequestedVoltage_mV();
        uint16_t ir  = _pd.getRequestedCurrent_mA();
        bool pd_ok   = _pd.isPDConnected();
        bool legacy  = _pd.isLegacyConnected();
        bool flip    = _pd.isCableFlipped();
        bool derate  = _pd.isDerating();
        String fault = _pd.getFaultString();

        char buf[512];
        snprintf(buf, sizeof(buf),
            "{"
            "\"voltage_mv\":%u,"
            "\"current_ma\":%u,"
            "\"power_mw\":%lu,"
            "\"temp_c\":%d,"
            "\"vreq_mv\":%u,"
            "\"ireq_ma\":%u,"
            "\"pd_connected\":%s,"
            "\"legacy\":%s,"
            "\"cc_flip\":%s,"
            "\"derating\":%s,"
            "\"fault\":\"%s\","
            "\"output_on\":%s"
            "}",
            v, i, (unsigned long)p, (int)t,
            vr, ir,
            pd_ok  ? "true" : "false",
            legacy ? "true" : "false",
            flip   ? "true" : "false",
            derate ? "true" : "false",
            fault.c_str(),
            _outputOn ? "true" : "false"
        );
        _server.send(200, "application/json", buf);
    }

    // ── GET /api/pdos ────────────────────────────────────────────────────────
    void _handlePDOs() {
        _cors();
        String json = "{\"pdos\":[";
        bool first = true;
        for (uint8_t i = 1; i <= 13; i++) {
            const AP33772S_PDO &pdo = _pd.getPDO(i);
            if (!pdo.valid) continue;
            if (!first) json += ',';
            first = false;
            char entry[128];
            snprintf(entry, sizeof(entry),
                "{\"index\":%u,\"type\":%u,\"min_mv\":%u,\"max_mv\":%u,"
                "\"max_ma\":%u,\"epr\":%s}",
                pdo.index, pdo.type,
                pdo.minVoltage_mV, pdo.maxVoltage_mV,
                pdo.maxCurrent_mA,
                pdo.isEPR ? "true" : "false");
            json += entry;
        }
        json += "]}";
        _server.send(200, "application/json", json);
    }

    // ── POST /api/voltage ────────────────────────────────────────────────────
    void _handleSetVoltage() {
        _cors();
        String body = _server.arg("plain");
        uint16_t mv = (uint16_t)_jsonInt(body, "voltage_mv", 5000);
        uint16_t ma = (uint16_t)_jsonInt(body, "current_ma", 1000);
        uint8_t idx = _pd.setVoltage(mv, ma);
        if (idx) {
            String res = _pd.getNegotiationResultString(3000);
            char buf[128];
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"message\":\"PDO %u selected. %s\",\"pdo_index\":%u}",
                idx, res.c_str(), idx);
            _server.send(200, "application/json", buf);
            Serial.printf("[API] Voltage set: %u mV / %u mA → PDO %u\n", mv, ma, idx);
        } else {
            char buf[96];
            snprintf(buf, sizeof(buf),
                "{\"ok\":false,\"message\":\"No suitable PDO for %u mV / %u mA\"}", mv, ma);
            _server.send(200, "application/json", buf);
        }
    }

    // ── POST /api/pps ────────────────────────────────────────────────────────
    void _handleSetPPS() {
        _cors();
        String body = _server.arg("plain");
        uint16_t mv = (uint16_t)_jsonInt(body, "voltage_mv", 5000);
        uint16_t ma = (uint16_t)_jsonInt(body, "current_ma", 3000);
        uint8_t idx = _pd.setPPS(mv, ma);
        if (idx) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"message\":\"PPS PDO %u at %u mV / %u mA\",\"pdo_index\":%u}",
                idx, mv, ma, idx);
            _server.send(200, "application/json", buf);
            Serial.printf("[API] PPS set: %u mV / %u mA → PDO %u\n", mv, ma, idx);
        } else {
            _server.send(200, "application/json",
                "{\"ok\":false,\"message\":\"PPS not available from this source\"}");
        }
    }

    // ── POST /api/avs ────────────────────────────────────────────────────────
    void _handleSetAVS() {
        _cors();
        String body = _server.arg("plain");
        uint16_t mv = (uint16_t)_jsonInt(body, "voltage_mv", 20000);
        uint16_t ma = (uint16_t)_jsonInt(body, "current_ma", 3000);
        uint8_t idx = _pd.setAVS(mv, ma);
        if (idx) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"message\":\"AVS PDO %u at %u mV / %u mA\",\"pdo_index\":%u}",
                idx, mv, ma, idx);
            _server.send(200, "application/json", buf);
            Serial.printf("[API] AVS set: %u mV / %u mA → PDO %u\n", mv, ma, idx);
        } else {
            _server.send(200, "application/json",
                "{\"ok\":false,\"message\":\"AVS not available from this source\"}");
        }
    }

    // ── POST /api/output ────────────────────────────────────────────────────
    void _handleOutput() {
        _cors();
        String body = _server.arg("plain");
        bool on = _jsonBool(body, "on", true);
        _outputOn = on;
        _pd.setOutput(on);
        char buf[64];
        snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"message\":\"Output %s\",\"on\":%s}",
            on ? "ON" : "OFF", on ? "true" : "false");
        _server.send(200, "application/json", buf);
        Serial.printf("[API] Output: %s\n", on ? "ON" : "OFF");
    }

    // ── POST /api/reset ──────────────────────────────────────────────────────
    void _handleReset() {
        _cors();
        _pendingHardReset = true;  // Defer to avoid blocking HTTP response
        _server.send(200, "application/json",
            "{\"ok\":true,\"message\":\"Hard reset issued\"}");
        Serial.println(F("[API] Hard reset requested"));
    }

    // ── POST /api/wifi ───────────────────────────────────────────────────────
    void _handleWifi() {
        _cors();
        if (!_wifiMgr) {
            _server.send(501, "application/json",
                "{\"ok\":false,\"message\":\"Wi-Fi manager not configured\"}");
            return;
        }
        String body = _server.arg("plain");
        String ssid = _jsonStr(body, "ssid");
        String pass = _jsonStr(body, "pass");
        if (ssid.length() == 0) {
            _server.send(400, "application/json",
                "{\"ok\":false,\"message\":\"ssid field required\"}");
            return;
        }
        _server.send(200, "application/json",
            "{\"ok\":true,\"message\":\"Credentials saved — reconnecting\"}");
        delay(100);  // Allow HTTP response to send before Wi-Fi mode change
        _wifiMgr->setCredentials(ssid, pass);
    }
};

#endif // SPARK_API_SERVER_H
