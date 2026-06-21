#include "zigbee_handler.h"

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

/* Headers nativos da lib v2 */
#include "ezbee/core.h"
#include "ezbee/af.h"
#include "ezbee/bdb.h"
#include "ezbee/app_signals.h"
#include "ezbee/zcl.h"
#include "ezbee/zcl/zcl_common.h"
#include "ezbee/zcl/zcl_general_cmd.h"
/* compat/esp_zigbee_core.h REMOVIDO — dispara #error propositalmente na v2 */

static const char *TAG = "ZIGBEE";

#define ZIGBEE_JOINED_BIT BIT0
static EventGroupHandle_t s_zb_event_group = NULL;

/* ─── App signal handler ─────────────────────────────────────────────────
 * Na v2:
 *   - ezb_app_signal_get_type()          → tipo do sinal
 *   - ezb_bdb_get_commissioning_status() → status do último commissioning
 *   - A lib usa esp_err_t / ESP_OK (não existe EZB_OK)
 */
static bool zigbee_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Stack iniciado. Buscando rede...");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        break;

    case EZB_BDB_SIGNAL_STEERING: {
        esp_err_t status = ezb_bdb_get_commissioning_status();
        if (status == ESP_OK) {
            ESP_LOGI(TAG, "Conectado! PAN ID: 0x%04hx  Canal: %d",
                     ezb_get_panid(), ezb_get_current_channel());
            xEventGroupSetBits(s_zb_event_group, ZIGBEE_JOINED_BIT);
        } else {
            ESP_LOGW(TAG, "Steering falhou (0x%x). Tentando novamente...", status);
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        }
        break;
    }

    case EZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Saiu da rede. Reconectando...");
        xEventGroupClearBits(s_zb_event_group, ZIGBEE_JOINED_BIT);
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        break;

    default:
        ESP_LOGD(TAG, "Signal: %d", signal_type);
        break;
    }
    return true;
}

/* ─── ZCL action handler ─────────────────────────────────────────────────*/
static void zigbee_zcl_handler(ezb_zcl_core_action_callback_id_t cb_id, void *msg)
{
    ESP_LOGD(TAG, "ZCL action: 0x%lx", (unsigned long)cb_id);
}

/* ─── Tarefa do stack ────────────────────────────────────────────────────*/
static void zigbee_task(void *arg)
{
    /* 1. Inicializa core */
    ESP_ERROR_CHECK(ezb_core_init());

    /* 2. Cria endpoint e o registra no device
     *    Na v2 o endpoint é criado antes dos clusters.               */
    ezb_af_ep_config_t ep_cfg = {
        .ep_id              = ZIGBEE_ENDPOINT,
        .app_profile_id     = ZIGBEE_HA_PROFILE_ID,
        .app_device_id      = ZIGBEE_DEVICE_ID,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_cfg);

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
    ezb_af_device_desc_register(dev_desc);

    /* 3. Inicializa clusters no endpoint
     *    Assinatura real: void ezb_zcl_*_cluster_server_init(uint8_t ep_id)
     *    Retorna void e registra internamente — não há descriptor para capturar.
     *    Atributos iniciais são configurados via ezb_zcl_set_attr_value após o init. */
    ezb_zcl_basic_cluster_server_init(ZIGBEE_ENDPOINT);
    ezb_zcl_identify_cluster_server_init(ZIGBEE_ENDPOINT);
    ezb_zcl_temperature_measurement_cluster_server_init(ZIGBEE_ENDPOINT);

    /* 4. Configura valores iniciais dos atributos do cluster de temperatura */
    int16_t temp_invalid    = (int16_t)0x8000;   /* valor "não disponível" */
    int16_t temp_min        = -5500;              /* -55.00 °C */
    int16_t temp_max        =  12500;             /* 125.00 °C */

    ezb_zcl_set_attr_value(ZIGBEE_ENDPOINT, ZIGBEE_TEMP_CLUSTER_ID,
        EZB_ZCL_CLUSTER_SERVER, 0x0000 /* MeasuredValue */,
        0x0000, (void *)&temp_invalid, false);

    ezb_zcl_set_attr_value(ZIGBEE_ENDPOINT, ZIGBEE_TEMP_CLUSTER_ID,
        EZB_ZCL_CLUSTER_SERVER, 0x0001 /* MinMeasuredValue */,
        0x0000, (void *)&temp_min, false);

    ezb_zcl_set_attr_value(ZIGBEE_ENDPOINT, ZIGBEE_TEMP_CLUSTER_ID,
        EZB_ZCL_CLUSTER_SERVER, 0x0002 /* MaxMeasuredValue */,
        0x0000, (void *)&temp_max, false);

    /* 5. Registra handlers */
    ezb_zcl_core_action_handler_register(zigbee_zcl_handler);
    ezb_app_signal_add_handler(zigbee_signal_handler);

    /* 6. Canal e start (bloqueante) */
    ezb_bdb_set_primary_channel_set(ZIGBEE_CHANNEL_MASK);
    ESP_ERROR_CHECK(ezb_dev_start(true));

    vTaskDelete(NULL);
}

/* ─── API pública ────────────────────────────────────────────────────────*/
void zigbee_node_start(void)
{
    s_zb_event_group = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreatePinnedToCore(zigbee_task, "zigbee_task", 4096, NULL, 5, NULL, 0);

    ESP_LOGI(TAG, "Aguardando associação à rede Zigbee...");
    xEventGroupWaitBits(s_zb_event_group, ZIGBEE_JOINED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Pronto para enviar dados.");
}

esp_err_t zigbee_send_temperature(float temperature)
{
    int16_t zcl_value = (int16_t)(temperature * 100.0f);
    ESP_LOGI(TAG, "Enviando: %.2f C (ZCL: %d)", temperature, zcl_value);

    ezb_zcl_set_attr_value(
        ZIGBEE_ENDPOINT,
        ZIGBEE_TEMP_CLUSTER_ID,
        EZB_ZCL_CLUSTER_SERVER,
        ZIGBEE_TEMP_ATTR_ID,
        0x0000,
        (void *)&zcl_value,
        false
    );

    ezb_zcl_report_attr_cmd_t report_cmd = {0};
    report_cmd.payload.attr_id = ZIGBEE_TEMP_ATTR_ID;

    return (esp_err_t)ezb_zcl_report_attr_cmd_req(&report_cmd);
}