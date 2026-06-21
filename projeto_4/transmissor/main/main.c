#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h" 
#include "esp_log.h"

#include "ds18b20.h"
#include "zigbee_handler_emissor.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "   INICIALIZANDO FIRMWARE DO EMISSOR ZED  ");
    ESP_LOGI(TAG, "==========================================");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicializa o stack Zigbee
    zigbee_emissor_start();
    EventGroupHandle_t zb_events = zigbee_emissor_get_event_group();

    ESP_LOGI(TAG, "Aguardando conexao com o Coordenador Zigbee...");
    
    // Trava aqui até que o dispositivo se conecte com sucesso ao seu coordenador
    if (zb_events != NULL) {
        xEventGroupWaitBits(zb_events, ZB_JOINED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Dispositivo conectado! Iniciando loop de telemetria.");

    while (1) {
        float current_temp = 0.0f;
        esp_err_t err = ds18b20_read_temperature(&current_temp);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Leitura do Sensor: %.2f C", current_temp);
            zigbee_emissor_send_temperature(current_temp);
        } else {
            ESP_LOGE(TAG, "Erro ao ler sensor DS18B20 (Codigo: 0x%x)", err);
        }

        // Envia a cada 5 segundos
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}