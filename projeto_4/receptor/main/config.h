#pragma once

/* ── Wi-Fi ──────────────────────────────────────────────────────────────── */
#define WIFI_SSID        "brisa-2433572"
#define WIFI_PASSWORD    "tnvtord7"
#define WIFI_MAX_RETRIES  10

/* ── MQTT ───────────────────────────────────────────────────────────────── */
#define MQTT_BROKER_URI "mqtt://192.168.1.108"  /* <- altere para o IP da BBB */
#define MQTT_PORT         1883
#define MQTT_TOPIC_TEMP  "zigbee/temperatura"      /* <- altere se quiser */
#define MQTT_CLIENT_ID   "esp32c6_coordinator"

/* ── Zigbee ─────────────────────────────────────────────────────────────── */
#define ZIGBEE_CHANNEL_MASK   (1 << 15)   /* canal 15; altere se necessário */
#define ZIGBEE_ENDPOINT       1
#define ZIGBEE_TEMP_CLUSTER   0x0402      /* Temperature Measurement */
#define ZIGBEE_TEMP_ATTR      0x0000      /* MeasuredValue */
