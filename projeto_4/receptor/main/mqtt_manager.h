#pragma once

#include "esp_err.h"

/**
 * @brief Inicializa e conecta ao broker MQTT.
 *        Deve ser chamado após Wi-Fi conectado.
 */
esp_err_t mqtt_manager_start(void);

/**
 * @brief Publica a temperatura no tópico MQTT configurado.
 *
 * @param temperature  Temperatura em graus Celsius.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t mqtt_publish_temperature(float temperature);
