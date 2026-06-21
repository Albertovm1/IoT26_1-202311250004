#include "esp_log.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "zigbee_coordinator.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== Coordinator Zigbee + Wi-Fi + MQTT ===");

    /* NVS necessário para Wi-Fi e Zigbee */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 1. Conecta ao Wi-Fi */
    ESP_LOGI(TAG, "Iniciando Wi-Fi...");
    ESP_ERROR_CHECK(wifi_manager_start());

    /* 2. Conecta ao broker MQTT (NÃO aborta se falhar; reconecta sozinho depois) */
    ESP_LOGI(TAG, "Iniciando MQTT...");
    esp_err_t mqtt_err = mqtt_manager_start();
    if (mqtt_err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT não conectou agora (%s). Zigbee sobe mesmo assim; reconecta depois.",
                 esp_err_to_name(mqtt_err));
    }

    /* 3. Inicia Zigbee Coordinator (sempre, independente do MQTT) */
    ESP_LOGI(TAG, "Iniciando Zigbee Coordinator...");
    zigbee_coordinator_start();

    ESP_LOGI(TAG, "Sistema pronto. Aguardando dados dos nodes...");
}