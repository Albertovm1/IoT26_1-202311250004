#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

#define ZIGBEE_COORDINATOR_ENDPOINT 1
#define ZIGBEE_NETWORK_CHANNEL      11

static const char *TAG = "ZIGBEE_COORDINATOR";

/**
 * Callback executado sempre que uma ação/mensagem chega na stack Zigbee.
 * É aqui que interceptamos o dado de temperatura enviado pelo Node.
 */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const esp_zb_core_action_ctx_t *message) {
    esp_err_t ret = ESP_OK;
    
    switch (callback_id) {
        // Intercepta relatórios de atributos (Report Attributes) vindos do Node
        case ESP_ZB_CORE_REPORT_ATTR_CB_ID: {
            esp_zb_zcl_report_attr_message_t *report_msg = (esp_zb_zcl_report_attr_message_t *)message->p_msg;
            
            // Verifica se o dado pertence ao Cluster de Medição de Temperatura
            if (report_msg->cluster == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT) {
                // Verifica se é o atributo do valor da temperatura
                if (report_msg->attributeID == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID) {
                    int16_t raw_temp = *(int16_t *)report_msg->data.value;
                    // O padrão Zigbee envia o valor multiplicado por 100, então dividimos de volta
                    float temperature = (float)raw_temp / 100.0;
                    
                    // Formato padronizado impresso na Serial para facilitar a leitura da BeagleBone Black
                    printf("DATA:TEMP:%.2f\n", temperature);
                    
                    ESP_LOGI(TAG, "Dado recebido via Zigbee -> Temperatura: %.2f C", temperature);
                }
            }
            break;
        }
        default:
            ESP_LOGD(TAG, "Ação Zigbee não tratada no handler: %d", callback_id);
            break;
    }
    return ret;
}

/**
 * Handler que gerencia o status e o comissionamento da rede Zigbee
 */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Inicializando Stack Zigbee no modo Coordenador...");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
            
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "Iniciando a formação da rede Zigbee (Formation)...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
            } else {
                ESP_LOGE(TAG, "Falha ao inicializar o dispositivo: %s", esp_err_to_name(err_status));
            }
            break;
            
        case ESP_ZB_BDB_SIGNAL_FORMATION:
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "Rede Zigbee criada com sucesso!");
                ESP_LOGI(TAG, "Abrindo rede para receber o Node de temperatura (Steering)...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGE(TAG, "Falha ao formar a rede. Tentando novamente...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
            }
            break;
            
        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "A rede está aberta e pronta para parear com o sensor de temperatura!");
            } else {
                ESP_LOGW(TAG, "Abertura de rede (Steering) falhou ou expirou.");
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Sinal de rede recebido: %d", sig_type);
            break;
    }
}

/**
 * Task principal de configuração do Zigbee
 */
static void esp_zb_task(void *pvParameters) {
    // Configura o dispositivo explicitamente como um Coordenador Zigbee (ZC)
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
    esp_zb_init(&zb_nwk_cfg);

    // Cria a lista de clusters que este dispositivo vai suportar
    esp_zb_attribute_list_t *esp_zb_cluster_list = esp_zb_zcl_cluster_list_create();
    
    // O Coordenador atuará como CLIENTE do cluster de temperatura (ele recebe relatórios do servidor/nó)
    esp_zb_cluster_list_add_temperature_measurement_cluster(
        esp_zb_cluster_list, 
        esp_zb_temperature_measurement_cluster_create(NULL), 
        ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE
    );

    // Registra o Endpoint do Gateway na arquitetura Home Automation
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
    esp_zb_ep_list_add_ep(
        esp_zb_ep_list, 
        esp_zb_cluster_list, 
        ZIGBEE_COORDINATOR_ENDPOINT, 
        ESP_ZB_AF_HA_PROFILE_ID, 
        ESP_ZB_HA_HOME_GATEWAY_DEVICE_ID
    );
    
    esp_zb_device_register(esp_zb_ep_list);
    
    // Registra o gerenciador de ações que criamos lá em cima para tratar os dados recebidos
    esp_zb_core_action_handler_register(zb_action_handler);

    // Trava o canal no mesmo do Node (Canal 11)
    esp_zb_set_primary_network_channel_set(1 << ZIGBEE_NETWORK_CHANNEL);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_main_loop_iteration();
}

void app_main(void) {
    ESP_LOGI(TAG, "Iniciando Gateway Coordenador Zigbee - Grupo 2");

    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    // Cria a tarefa que roda a stack Zigbee continuamente
    xTaskCreate(esp_zb_task, "esp_zb_main_task", 8192, NULL, 5, NULL);
}
