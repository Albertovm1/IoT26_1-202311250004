#include "zigbee_handler_emissor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Header principal unificado para a versão v1.6.x
#include "esp_zigbee_core.h"

static const char *TAG = "ZB_HANDLER";
static EventGroupHandle_t s_evt_grp = NULL;
static volatile bool s_connected = false;

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Iniciando Inicializacao do BDB...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Dispositivo pronto. Iniciando Network Steering...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGE(TAG, "Falha ao iniciar BDB. Forcando reinicio...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Conectado ao coordenador com sucesso! Canal: %d, PAN ID: 0x%04hx",
                     esp_zb_get_current_channel(), esp_zb_get_pan_id());
            s_connected = true;
            if (s_evt_grp) {
                xEventGroupSetBits(s_evt_grp, ZB_JOINED_BIT);
            }
        } else {
            ESP_LOGW(TAG, "Network steering falhou. Tentando novamente em 3s...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    default:
        ESP_LOGD(TAG, "Sinal recebido: %d, Status: 0x%x", sig_type, err_status);
        break;
    }
}

static void zigbee_task(void *pvParameters)
{
    // Correção da inicialização para compatibilidade com a v1.6.8 / IDF v5.5
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED, // Zigbee End Device
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = 20,
            .keep_alive = 3000,
        }
    };
    esp_zb_init(&zb_nwk_cfg);

    /* 1. Criar e configurar o Cluster Básico (Basic Cluster) */
    uint8_t power_source = 0x01; // Mains-powered
    esp_zb_attribute_list_t *basic_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    
    esp_zb_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_CLUSTER_ID_BASIC, 
                            ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, 
                            ESP_ZB_ZCL_ATTR_TYPE_U8, 
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, 
                            &power_source);

    /* 2. Criar e configurar o Cluster de Medição de Temperatura (Temperature Measurement) */
    int16_t raw_value = 0;       // Valor inicial padrão
    int16_t min_val = -4000;     // -40°C
    int16_t max_val = 12500;     // 125°C
    
    esp_zb_attribute_list_t *temp_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT);
    
    // Adicionando o valor medido atual (Corrigido para ESP_ZB_ZCL_ATTR_TYPE_S16)
    esp_zb_cluster_add_attr(temp_cluster, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 
                            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, 
                            ESP_ZB_ZCL_ATTR_TYPE_S16, 
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, 
                            &raw_value);
                            
    // Adicionando valor mínimo
    esp_zb_cluster_add_attr(temp_cluster, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 
                            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MIN_VALUE_ID, 
                            ESP_ZB_ZCL_ATTR_TYPE_S16, 
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, 
                            &min_val);
                            
    // Adicionando valor máximo
    esp_zb_cluster_add_attr(temp_cluster, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 
                            ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_MAX_VALUE_ID, 
                            ESP_ZB_ZCL_ATTR_TYPE_S16, 
                            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, 
                            &max_val);

    /* 3. Criar a Lista de Clusters e adicionar os clusters criados */
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_temperature_meas_cluster(cluster_list, temp_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* 4. Criar o Endpoint e adicionar a lista de clusters */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ZB_EP,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);

    /* 5. Registrar o dispositivo e iniciar */
    esp_zb_device_register(ep_list);
    
    esp_zb_set_primary_network_channel_set(ZB_CHANNEL_MASK);
    
    ESP_ERROR_CHECK(esp_zb_start(false));
    
    esp_zb_main_loop_iteration();
}

void zigbee_emissor_start(void)
{
    s_evt_grp = xEventGroupCreate();
    xTaskCreate(zigbee_task, "zigbee_task", 4096, NULL, 5, NULL);
}

EventGroupHandle_t zigbee_emissor_get_event_group(void)
{
    return s_evt_grp;
}

esp_err_t zigbee_emissor_send_temperature(float temperature)
{
    if (!s_connected) {
        ESP_LOGW(TAG, "Nao conectado ao coordenador. Envio abortado.");
        return ESP_ERR_INVALID_STATE;
    }

    int16_t raw_value = (int16_t)(temperature * 100.0f);

    esp_zb_zcl_status_t zcl_status = esp_zb_zcl_set_attribute_val(
        ZB_EP,
        ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID,
        &raw_value,
        false
    );

    if (zcl_status != ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGE(TAG, "Erro ao atualizar atributo local: 0x%x", zcl_status);
        return ESP_FAIL;
    }

    // Inicialização limpa e compatível com esp-zigbee-lib v1.6.8
    esp_zb_zcl_report_attr_cmd_t report_cmd;
    memset(&report_cmd, 0, sizeof(report_cmd));
    
    // Configura o endereço curto do coordenador (0x0000) dentro da união básica
    report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
    report_cmd.zcl_basic_cmd.dst_endpoint = 1;
    
    report_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
    report_cmd.attributeID = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;

    esp_zb_zcl_report_attr_cmd_req(&report_cmd);
    ESP_LOGI(TAG, "Report enviado: %.2f C", temperature);

    return ESP_OK;
}