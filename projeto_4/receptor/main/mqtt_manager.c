#include "mqtt_manager.h"
#include "config.h"

#include <stdio.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "MQTT";

#define MQTT_CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t s_client = NULL;
static EventGroupHandle_t s_mqtt_event_group;

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado ao broker MQTT.");
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Desconectado do broker MQTT.");
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erro MQTT.");
        break;

    default:
        break;
    }
}

esp_err_t mqtt_manager_start(void)
{
    s_mqtt_event_group = xEventGroupCreate();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri  = MQTT_BROKER_URI,
        .broker.address.port = MQTT_PORT,
        .credentials.client_id = MQTT_CLIENT_ID,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "Falha ao criar cliente MQTT.");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client,
        ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    ESP_LOGI(TAG, "Conectando ao broker %s:%d ...", MQTT_BROKER_URI, MQTT_PORT);

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
        MQTT_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));

    if (bits & MQTT_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Timeout ao conectar ao broker MQTT.");
    return ESP_FAIL;
}

esp_err_t mqtt_publish_temperature(float temperature)
{
    if (!s_client) {
        ESP_LOGE(TAG, "Cliente MQTT não inicializado.");
        return ESP_ERR_INVALID_STATE;
    }

    char payload[32];
    snprintf(payload, sizeof(payload), "%.2f", temperature);

    int msg_id = esp_mqtt_client_publish(s_client,
        MQTT_TOPIC_TEMP, payload, 0, 1, 0);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Falha ao publicar no tópico %s", MQTT_TOPIC_TEMP);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Publicado: %s = %s °C (msg_id=%d)",
             MQTT_TOPIC_TEMP, payload, msg_id);
    return ESP_OK;
}
