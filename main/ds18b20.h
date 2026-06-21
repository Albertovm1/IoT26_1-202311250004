#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

/* Pino GPIO onde o DS18B20 está conectado (altere se necessário) */
#define DS18B20_GPIO        GPIO_NUM_4

/* Resolução do sensor (9 a 12 bits) */
#define DS18B20_RESOLUTION  12

/**
 * @brief  Lê a temperatura do DS18B20 via protocolo 1-Wire.
 *
 * @param[out] temperature  Temperatura em graus Celsius.
 * @return ESP_OK em caso de sucesso, ESP_ERR_* em caso de falha.
 */
esp_err_t ds18b20_read_temperature(float *temperature);
