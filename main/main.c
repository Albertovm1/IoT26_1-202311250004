#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ds18b20.h"
#include "zigbee_handler.h"
#include "ezbee/zcl/zcl_common.h"

static const char *TAG = "MAIN";

#define SEND_INTERVAL_MS  (30 * 1000)

void app_main(void)
{
    ESP_LOGI(TAG, "=== Node de Monitoramento de Temperatura ===");
    ESP_LOGI(TAG, "Sensor: DS18B20 | Zigbee | Intervalo: %ds", SEND_INTERVAL_MS / 1000);

    zigbee_node_start();

    while (1) {
        int64_t t_start = esp_timer_get_time();

        float temperature = 0.0f;
        esp_err_t err = ds18b20_read_temperature(&temperature);

        if (err == ESP_OK) {
            err = zigbee_send_temperature(temperature);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Falha ao enviar: 0x%x", err);
            }
        } else {
            ESP_LOGE(TAG, "Falha DS18B20: 0x%x", err);
            int16_t invalid = (int16_t)0x8000;
            ezb_zcl_set_attr_value(
                ZIGBEE_ENDPOINT,
                ZIGBEE_TEMP_CLUSTER_ID,
                EZB_ZCL_CLUSTER_SERVER,
                ZIGBEE_TEMP_ATTR_ID,
                0x0000,
                (void *)&invalid,
                false
            );
        }

        int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
        int64_t sleep_ms   = SEND_INTERVAL_MS - elapsed_ms;
        if (sleep_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));
        }
    }
}
