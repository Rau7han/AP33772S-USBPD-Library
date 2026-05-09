/**
 * ble_manager.h  —  Spark Analyzer BLE GATT Server
 * ─────────────────────────────────────────────────────────────────────────────
 * Provides a BLE GATT server with two services:
 *
 *   Power Measurement Service  (UUID: 0x181A — Environmental Sensing)
 *     - Voltage Characteristic      (UUID: 0x2B18, notify)  — uint16_t mV
 *     - Current Characteristic      (UUID: 0x2AEE, notify)  — uint16_t mA
 *     - Power Characteristic        (UUID: 0x2B05, notify)  — uint32_t mW
 *     - Temperature Characteristic  (UUID: 0x2A6E, notify)  — int8_t °C
 *
 *   Power Control Service  (UUID: custom 128-bit)
 *     - Voltage Set Characteristic  (write)  — uint16_t mV
 *     - Current Set Characteristic  (write)  — uint16_t mA
 *     - Output Characteristic       (write)  — uint8_t  0=off, 1=on
 *     - Status Characteristic       (notify) — JSON string
 *
 * Usage:
 *   BLEManager ble("SparkAnalyzer");
 *   ble.begin();                   // Call once from setup()
 *   ble.updateMeasurements(v, i, p, t);   // Call from loop()
 *   ble.getRequestedVoltage_mV();  // Read pending voltage command
 * ─────────────────────────────────────────────────────────────────────────────
 */

#pragma once
#ifndef SPARK_BLE_MANAGER_H
#define SPARK_BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── Standard BLE service / characteristic UUIDs ──────────────────────────────
#define BLE_SVC_ENV_SENSING          "181A"
#define BLE_CHR_VOLTAGE              "2B18"
#define BLE_CHR_CURRENT              "2AEE"
#define BLE_CHR_POWER                "2B05"
#define BLE_CHR_TEMPERATURE          "2A6E"

// ── Custom Spark Analyzer control UUIDs (random 128-bit) ─────────────────────
#define BLE_SVC_POWER_CTRL           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHR_VOLTAGE_SET          "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHR_CURRENT_SET          "beb5483f-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHR_OUTPUT_CTRL          "beb54840-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHR_STATUS_JSON          "beb54841-36e1-4688-b7f5-ea07361b26a8"

// Notify interval for measurement characteristics
#define BLE_NOTIFY_INTERVAL_MS       500UL

class BLEManager : public BLEServerCallbacks {
public:
    explicit BLEManager(const char *deviceName = "SparkAnalyzer")
        : _deviceName(deviceName),
          _server(nullptr), _connected(false),
          _lastNotifyMs(0),
          _pendingVoltage_mV(0), _pendingCurrent_mA(0),
          _voltageRequested(false), _currentRequested(false),
          _outputRequested(false), _outputState(false) {}

    /** Initialise BLE stack and start advertising. Call once from setup(). */
    void begin() {
        BLEDevice::init(_deviceName);
        _server = BLEDevice::createServer();
        _server->setCallbacks(this);

        _buildMeasurementService();
        _buildControlService();

        BLEAdvertising *adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(BLE_SVC_ENV_SENSING);
        adv->addServiceUUID(BLE_SVC_POWER_CTRL);
        adv->setScanResponse(true);
        adv->setMinPreferred(0x06);
        BLEDevice::startAdvertising();

        Serial.printf("[BLE] Advertising as '%s'\n", _deviceName);
    }

    /**
     * Push latest measurements to connected BLE client.
     * Call every loop() — internally rate-limited to BLE_NOTIFY_INTERVAL_MS.
     */
    void updateMeasurements(uint16_t voltage_mV, uint16_t current_mA,
                            uint32_t power_mW,   int8_t   temp_C) {
        if (!_connected) return;
        if (millis() - _lastNotifyMs < BLE_NOTIFY_INTERVAL_MS) return;
        _lastNotifyMs = millis();

        // Voltage (uint16 LE, mV)
        _chrVoltage->setValue((uint8_t *)&voltage_mV, sizeof(voltage_mV));
        _chrVoltage->notify();

        // Current (uint16 LE, mA)
        _chrCurrent->setValue((uint8_t *)&current_mA, sizeof(current_mA));
        _chrCurrent->notify();

        // Power (uint32 LE, mW)
        _chrPower->setValue((uint8_t *)&power_mW, sizeof(power_mW));
        _chrPower->notify();

        // Temperature (int8, °C)
        _chrTemp->setValue((uint8_t *)&temp_C, sizeof(temp_C));
        _chrTemp->notify();

        // Status JSON
        char json[128];
        snprintf(json, sizeof(json),
                 "{\"v\":%u,\"i\":%u,\"p\":%u,\"t\":%d}",
                 voltage_mV, current_mA, power_mW, (int)temp_C);
        _chrStatus->setValue(json);
        _chrStatus->notify();
    }

    /** Returns true if a BLE client is connected. */
    bool isConnected() const { return _connected; }

    // ── Pending command accessors (call from loop after checking flag) ────────

    bool hasVoltageRequest() const { return _voltageRequested; }
    uint16_t getPendingVoltage_mV() {
        _voltageRequested = false;
        return _pendingVoltage_mV;
    }

    bool hasCurrentRequest() const { return _currentRequested; }
    uint16_t getPendingCurrent_mA() {
        _currentRequested = false;
        return _pendingCurrent_mA;
    }

    bool hasOutputRequest() const { return _outputRequested; }
    bool getPendingOutputState() {
        _outputRequested = false;
        return _outputState;
    }

    // ── BLEServerCallbacks ────────────────────────────────────────────────────
    void onConnect(BLEServer *) override {
        _connected = true;
        Serial.println(F("[BLE] Client connected"));
    }

    void onDisconnect(BLEServer *) override {
        _connected = false;
        Serial.println(F("[BLE] Client disconnected — restarting advertising"));
        BLEDevice::startAdvertising();
    }

private:
    const char     *_deviceName;
    BLEServer      *_server;
    bool            _connected;
    uint32_t        _lastNotifyMs;

    // Pending commands from BLE client
    uint16_t _pendingVoltage_mV;
    uint16_t _pendingCurrent_mA;
    bool     _voltageRequested;
    bool     _currentRequested;
    bool     _outputRequested;
    bool     _outputState;

    // Characteristic handles
    BLECharacteristic *_chrVoltage  = nullptr;
    BLECharacteristic *_chrCurrent  = nullptr;
    BLECharacteristic *_chrPower    = nullptr;
    BLECharacteristic *_chrTemp     = nullptr;
    BLECharacteristic *_chrStatus   = nullptr;
    BLECharacteristic *_chrVoltSet  = nullptr;
    BLECharacteristic *_chrCurrSet  = nullptr;
    BLECharacteristic *_chrOutCtrl  = nullptr;

    // ── Inner callback classes ────────────────────────────────────────────────

    class VoltageSetCallback : public BLECharacteristicCallbacks {
    public:
        explicit VoltageSetCallback(BLEManager *mgr) : _mgr(mgr) {}
        void onWrite(BLECharacteristic *chr) override {
            if (chr->getLength() >= 2) {
                uint8_t *data = chr->getData();
                _mgr->_pendingVoltage_mV = (uint16_t)(data[0] | (data[1] << 8));
                _mgr->_voltageRequested  = true;
                Serial.printf("[BLE] Voltage request: %u mV\n", _mgr->_pendingVoltage_mV);
            }
        }
    private:
        BLEManager *_mgr;
    };

    class CurrentSetCallback : public BLECharacteristicCallbacks {
    public:
        explicit CurrentSetCallback(BLEManager *mgr) : _mgr(mgr) {}
        void onWrite(BLECharacteristic *chr) override {
            if (chr->getLength() >= 2) {
                uint8_t *data = chr->getData();
                _mgr->_pendingCurrent_mA = (uint16_t)(data[0] | (data[1] << 8));
                _mgr->_currentRequested  = true;
                Serial.printf("[BLE] Current limit request: %u mA\n", _mgr->_pendingCurrent_mA);
            }
        }
    private:
        BLEManager *_mgr;
    };

    class OutputCtrlCallback : public BLECharacteristicCallbacks {
    public:
        explicit OutputCtrlCallback(BLEManager *mgr) : _mgr(mgr) {}
        void onWrite(BLECharacteristic *chr) override {
            if (chr->getLength() >= 1) {
                _mgr->_outputState    = (chr->getData()[0] != 0);
                _mgr->_outputRequested = true;
                Serial.printf("[BLE] Output request: %s\n",
                              _mgr->_outputState ? "ON" : "OFF");
            }
        }
    private:
        BLEManager *_mgr;
    };

    // ── Service builders ──────────────────────────────────────────────────────

    void _buildMeasurementService() {
        BLEService *svc = _server->createService(BLEUUID(BLE_SVC_ENV_SENSING));

        auto mkNotify = [&](const char *uuid) -> BLECharacteristic * {
            auto *chr = svc->createCharacteristic(
                uuid,
                BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
            chr->addDescriptor(new BLE2902());
            return chr;
        };

        _chrVoltage = mkNotify(BLE_CHR_VOLTAGE);
        _chrCurrent = mkNotify(BLE_CHR_CURRENT);
        _chrPower   = mkNotify(BLE_CHR_POWER);
        _chrTemp    = mkNotify(BLE_CHR_TEMPERATURE);
        svc->start();
    }

    void _buildControlService() {
        BLEService *svc = _server->createService(BLEUUID(BLE_SVC_POWER_CTRL));

        auto mkWrite = [&](const char *uuid) -> BLECharacteristic * {
            return svc->createCharacteristic(
                uuid,
                BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        };

        _chrVoltSet = mkWrite(BLE_CHR_VOLTAGE_SET);
        _chrVoltSet->setCallbacks(new VoltageSetCallback(this));

        _chrCurrSet = mkWrite(BLE_CHR_CURRENT_SET);
        _chrCurrSet->setCallbacks(new CurrentSetCallback(this));

        _chrOutCtrl = mkWrite(BLE_CHR_OUTPUT_CTRL);
        _chrOutCtrl->setCallbacks(new OutputCtrlCallback(this));

        _chrStatus = svc->createCharacteristic(
            BLE_CHR_STATUS_JSON,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
        _chrStatus->addDescriptor(new BLE2902());
        _chrStatus->setValue("{}");

        svc->start();
    }
};

#endif // SPARK_BLE_MANAGER_H
