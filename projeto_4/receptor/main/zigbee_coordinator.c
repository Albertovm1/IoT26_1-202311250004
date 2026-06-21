#include "zigbee_coordinator.h"
#include "config.h"
#include "mqtt_manager.h"

#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_zigbee_core.h"

static const char *TAG = "ZB_COORD";

/* ─── Callback de atributo recebido ─────────────────────────────────────── */
static esp_err_t zigbee_zcl_handler(esp_zb_core_action_callback_id_t cb_id, const void *message)
{
    esp_err_t ret = ESP_OK;

    // Captura relatórios de atributos enviados pelo Sensor de Temperatura
    if (cb_id == ESP_ZB_CORE_REPORT_ATTR_CB_ID) {
        const esp_zb_zcl_report_attr_message_t *report_msg = (const esp_zb_zcl_report_attr_message_t *)message;

        if (!report_msg) return ESP_FAIL;

        uint16_t cluster = report_msg->cluster;
        uint16_t attr_id = report_msg->attribute.id;

        if (cluster == ZIGBEE_TEMP_CLUSTER && attr_id == ZIGBEE_TEMP_ATTR) {
            int16_t raw = *(int16_t *)report_msg->attribute.data.value;

            // O padrão Zigbee envia a temperatura multiplicada por 100
            float temperature = (float)raw / 100.0f;

            ESP_LOGI(TAG, "Temperatura recebida via Zigbee: %.2f °C", temperature);

            /* Publica no MQTT do BBB */
            esp_err_t err = mqtt_publish_temperature(temperature);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Falha ao publicar temperatura no MQTT.");
            }
        }
    }
    return ret;
}

/* ─── Callback de sinal de rede ─────────────────────────────────────────── */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;
    esp_err_t err_status = signal_struct->esp_err_status;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Stack pronto. Iniciando rede como Coordinator...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Rede Zigbee criada com sucesso!");
            /* Abre a rede automaticamente para associação do sensor (Network Steering) */
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Falha ao criar rede (0x%x). Tentando novamente...", err_status);
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Rede aberta! Pode ligar o sensor de temperatura para emparelhar.");
        } else {
            ESP_LOGW(TAG, "Falha ao abrir rede para novos dispositivos (0x%x).", err_status);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        ESP_LOGI(TAG, "Novo sensor de temperatura associado à rede Zigbee!");
        break;

    default:
        ESP_LOGD(TAG, "Sinal Zigbee recebido: %d, Status: 0x%x", sig_type, err_status);
        break;
    }
}

/* ─── Tarefa do stack Zigbee ─────────────────────────────────────────────── */
static void zigbee_task(void *arg)
{
    // 1. Configuração e Inicialização do Host e Rádio
    esp_zb_platform_config_t config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    // Inicializa como Coordenador (ZC)
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {
            .max_children = 10,
        },
    };
    esp_zb_init(&zb_nwk_cfg);

    // 2. Criação dos Clusters (Basic, Identify e o Client de Temperatura)
    esp_zb_attribute_list_t *basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    uint8_t power_source = 0x01; // Alimentado diretamente pela rede elétrica
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &power_source);

    esp_zb_attribute_list_t *identify_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    uint16_t identify_time = 0;
    esp_zb_identify_cluster_add_attr(identify_cluster, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID, &identify_time);

    // Cria o Cluster de temperatura como CLIENT (para escutar e receber dados vindos do sensor)
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    // Adiciona o cluster de leitura de temperatura vazio (visto que ele só atuará como escuta/client)
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, esp_zb_zcl_attr_list_create(ZIGBEE_TEMP_CLUSTER), ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    // 3. Regista o Endpoint Único
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZIGBEE_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_HOME_GATEWAY_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);
    esp_zb_device_register(ep_list);

    // 4. Regista Handlers de Callbacks
    esp_zb_core_action_handler_register(zigbee_zcl_handler);

    // Configura máscara de canal principal (Usa a máscara configurada no config.h)
    esp_zb_set_primary_network_channel_set(ZIGBEE_CHANNEL_MASK);

    // 5. Inicia o Stack
    ESP_ERROR_CHECK(esp_zb_start(false));

    // 6. Loop Principal do Stack
    esp_zb_stack_main_loop();
}

void zigbee_coordinator_start(void)
{
    xTaskCreatePinnedToCore(zigbee_task, "zigbee_coord", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "Coordinator Zigbee configurado e disparado.");
}