/*
 * api_server.c  —  Spark Analyzer HTTP REST API Server (ESP-IDF)
 */

#include "api_server.h"
#include "ucpd_controller.h"
#include "wifi_manager.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "APIServer";

static httpd_handle_t     s_server   = NULL;
static spark_measurements_t *s_meas  = NULL;
static SemaphoreHandle_t  s_meas_mutex;

/* ── Dashboard HTML (minimal embedded page — full version in web_dashboard.h) ─ */
static const char DASHBOARD_HTML[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Spark Analyzer</title>"
    "<style>body{font-family:sans-serif;background:#0f1117;color:#e0e0e0;padding:20px}"
    "h1{color:#4f8ef7}.card{background:#1a1d27;border-radius:12px;padding:20px;margin:10px 0}"
    ".metric{font-size:2rem;font-weight:700}.muted{color:#888;font-size:.85rem}"
    "button{background:#4f8ef7;color:#fff;border:none;border-radius:8px;"
    "padding:10px 16px;cursor:pointer;margin:4px}"
    "input{background:#2a2d3a;border:1px solid #3a3d4a;color:#e0e0e0;"
    "padding:8px;border-radius:8px;width:200px}</style></head>"
    "<body><h1>⚡ Spark Analyzer</h1>"
    "<div class='card'><div class='muted'>Voltage</div>"
    "<div class='metric' id='v'>—</div></div>"
    "<div class='card'><div class='muted'>Current</div>"
    "<div class='metric' id='i'>—</div></div>"
    "<div class='card'><div class='muted'>Power</div>"
    "<div class='metric' id='p'>—</div></div>"
    "<div class='card'><div class='muted'>Set Voltage (mV)</div>"
    "<input id='mv' type='number' value='12000' step='100'>"
    "<input id='ma' type='number' value='2000' step='50'>"
    "<button onclick=\"fetch('/api/voltage',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({voltage_mv:+document.getElementById('mv').value,"
    "current_ma:+document.getElementById('ma').value})}).then(r=>r.json())"
    ".then(d=>alert(d.message))\">Apply</button>"
    "<button onclick=\"fetch('/api/output',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:'{\"on\":true}'}).then(r=>r.json()).then(d=>alert(d.message))\">Output ON</button>"
    "<button onclick=\"fetch('/api/output',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:'{\"on\":false}'}).then(r=>r.json()).then(d=>alert(d.message))\">Output OFF</button>"
    "</div>"
    "<script>function refresh(){"
    "fetch('/api/status').then(r=>r.json()).then(d=>{"
    "document.getElementById('v').textContent=d.voltage_mv+' mV';"
    "document.getElementById('i').textContent=d.current_ma+' mA';"
    "document.getElementById('p').textContent=d.power_mw+' mW';})}"
    "refresh();setInterval(refresh,500);</script></body></html>";

/* ── CORS headers ────────────────────────────────────────────────────────── */
static void _set_cors(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

/* ── Read request body ──────────────────────────────────────────────────── */
static int _read_body(httpd_req_t *req, char *buf, size_t buf_size) {
    int total_len = req->content_len;
    if (total_len == 0 || total_len >= (int)buf_size) return -1;
    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) return -1;
        received += ret;
    }
    buf[received] = '\0';
    return received;
}

/* ── GET / ──────────────────────────────────────────────────────────────── */
static esp_err_t _handle_root(httpd_req_t *req) {
    _set_cors(req);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

/* ── GET /api/status ────────────────────────────────────────────────────── */
static esp_err_t _handle_status(httpd_req_t *req) {
    _set_cors(req);
    spark_measurements_t m = {0};
    if (xSemaphoreTake(s_meas_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        m = *s_meas;
        xSemaphoreGive(s_meas_mutex);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "voltage_mv",   m.voltage_mv);
    cJSON_AddNumberToObject(root, "current_ma",   m.current_ma);
    cJSON_AddNumberToObject(root, "power_mw",     m.power_mw);
    cJSON_AddNumberToObject(root, "temp_c",       m.temp_c);
    cJSON_AddNumberToObject(root, "vreq_mv",      m.vreq_mv);
    cJSON_AddNumberToObject(root, "ireq_ma",      m.ireq_ma);
    cJSON_AddBoolToObject(root,   "pd_connected", ucpd_is_pd_connected());
    cJSON_AddBoolToObject(root,   "legacy",       ucpd_is_legacy_connected());
    cJSON_AddBoolToObject(root,   "cc_flip",      ucpd_is_cable_flipped());
    cJSON_AddBoolToObject(root,   "derating",     ucpd_is_derating());
    cJSON_AddBoolToObject(root,   "fault",        ucpd_is_fault());
    cJSON_AddStringToObject(root, "wifi_ip",      wifi_manager_get_ip());
    cJSON_AddBoolToObject(root,   "wifi_ok",      wifi_manager_is_connected());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* ── GET /api/pdos ──────────────────────────────────────────────────────── */
static esp_err_t _handle_pdos(httpd_req_t *req) {
    _set_cors(req);
    cJSON *root  = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    for (uint8_t i = 1; i <= 13; i++) {
        const ucpd_pdo_t *p = ucpd_get_pdo(i);
        if (!p) continue;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(obj, "index",   p->index);
        cJSON_AddNumberToObject(obj, "type",    p->type);
        cJSON_AddNumberToObject(obj, "min_mv",  p->min_mv);
        cJSON_AddNumberToObject(obj, "max_mv",  p->max_mv);
        cJSON_AddNumberToObject(obj, "max_ma",  p->max_ma);
        cJSON_AddBoolToObject(obj,   "epr",     p->is_epr);
        cJSON_AddItemToArray(array, obj);
    }
    cJSON_AddItemToObject(root, "pdos", array);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* ── POST /api/voltage ──────────────────────────────────────────────────── */
static esp_err_t _handle_set_voltage(httpd_req_t *req) {
    _set_cors(req);
    char body[128] = {0};
    if (_read_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    uint16_t mv = root ? (uint16_t)cJSON_GetObjectItem(root, "voltage_mv")->valueint : 5000;
    uint16_t ma = root ? (uint16_t)cJSON_GetObjectItem(root, "current_ma")->valueint : 1000;
    cJSON_Delete(root);

    uint8_t idx = ucpd_set_voltage(mv, ma);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", idx != 0);
    char msg[64];
    if (idx) {
        ucpd_wait_negotiation(3000);
        snprintf(msg, sizeof(msg), "PDO %u selected for %u mV / %u mA", idx, mv, ma);
    } else {
        snprintf(msg, sizeof(msg), "No suitable PDO for %u mV / %u mA", mv, ma);
    }
    cJSON_AddStringToObject(resp, "message", msg);
    cJSON_AddNumberToObject(resp, "pdo_index", idx);

    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    ESP_LOGI(TAG, "Voltage request: %u mV / %u mA → PDO %u", mv, ma, idx);
    return ESP_OK;
}

/* ── POST /api/pps ──────────────────────────────────────────────────────── */
static esp_err_t _handle_set_pps(httpd_req_t *req) {
    _set_cors(req);
    char body[128] = {0};
    _read_body(req, body, sizeof(body));
    cJSON *root = cJSON_Parse(body);
    uint16_t mv = root ? (uint16_t)cJSON_GetObjectItem(root, "voltage_mv")->valueint : 5000;
    uint16_t ma = root ? (uint16_t)cJSON_GetObjectItem(root, "current_ma")->valueint : 3000;
    cJSON_Delete(root);

    uint8_t idx = ucpd_set_pps(mv, ma);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", idx != 0);
    char msg[64];
    snprintf(msg, sizeof(msg), idx ? "PPS PDO %u at %u mV / %u mA"
                                   : "PPS not available", idx, mv, ma);
    cJSON_AddStringToObject(resp, "message", msg);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* ── POST /api/avs ──────────────────────────────────────────────────────── */
static esp_err_t _handle_set_avs(httpd_req_t *req) {
    _set_cors(req);
    char body[128] = {0};
    _read_body(req, body, sizeof(body));
    cJSON *root = cJSON_Parse(body);
    uint16_t mv = root ? (uint16_t)cJSON_GetObjectItem(root, "voltage_mv")->valueint : 20000;
    uint16_t ma = root ? (uint16_t)cJSON_GetObjectItem(root, "current_ma")->valueint : 3000;
    cJSON_Delete(root);

    uint8_t idx = ucpd_set_avs(mv, ma);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", idx != 0);
    char msg[64];
    snprintf(msg, sizeof(msg), idx ? "AVS PDO %u at %u mV / %u mA"
                                   : "AVS not available", idx, mv, ma);
    cJSON_AddStringToObject(resp, "message", msg);
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

/* ── POST /api/output ───────────────────────────────────────────────────── */
static esp_err_t _handle_output(httpd_req_t *req) {
    _set_cors(req);
    char body[64] = {0};
    _read_body(req, body, sizeof(body));
    cJSON *root = cJSON_Parse(body);
    bool on = root ? cJSON_IsTrue(cJSON_GetObjectItem(root, "on")) : true;
    cJSON_Delete(root);

    ucpd_set_output(on);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "on", on);
    cJSON_AddStringToObject(resp, "message", on ? "Output ON" : "Output OFF");
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    free(json);
    ESP_LOGI(TAG, "Output: %s", on ? "ON" : "OFF");
    return ESP_OK;
}

/* ── POST /api/reset ────────────────────────────────────────────────────── */
static esp_err_t _handle_reset(httpd_req_t *req) {
    _set_cors(req);
    ucpd_hard_reset();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Hard reset issued\"}", -1);
    ESP_LOGI(TAG, "Hard reset issued");
    return ESP_OK;
}

/* ── POST /api/wifi ─────────────────────────────────────────────────────── */
static esp_err_t _handle_wifi(httpd_req_t *req) {
    _set_cors(req);
    char body[256] = {0};
    _read_body(req, body, sizeof(body));
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "ssid"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pass"));
    if (!ssid) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid field required");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true,\"message\":\"Credentials saved — reconnecting\"}", -1);
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_manager_set_credentials(ssid, pass ? pass : "");
    cJSON_Delete(root);
    return ESP_OK;
}

/* ── Server start / stop ────────────────────────────────────────────────── */

esp_err_t api_server_start(spark_measurements_t *meas, SemaphoreHandle_t meas_mutex) {
    s_meas       = meas;
    s_meas_mutex = meas_mutex;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 8192;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t routes[] = {
        {"/",            HTTP_GET,  _handle_root,        NULL},
        {"/api/status",  HTTP_GET,  _handle_status,      NULL},
        {"/api/pdos",    HTTP_GET,  _handle_pdos,        NULL},
        {"/api/voltage", HTTP_POST, _handle_set_voltage, NULL},
        {"/api/pps",     HTTP_POST, _handle_set_pps,     NULL},
        {"/api/avs",     HTTP_POST, _handle_set_avs,     NULL},
        {"/api/output",  HTTP_POST, _handle_output,      NULL},
        {"/api/reset",   HTTP_POST, _handle_reset,       NULL},
        {"/api/wifi",    HTTP_POST, _handle_wifi,        NULL},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP REST API started on port 80");
    ESP_LOGI(TAG, "Dashboard: http://spark.local/");
    return ESP_OK;
}

esp_err_t api_server_stop(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}
