#pragma once

#include "esp_err.h"

/**
 * @brief Inicializa o Wi-Fi em modo station e conecta.
 *        Bloqueia até conexão bem-sucedida ou falha após WIFI_MAX_RETRIES.
 */
esp_err_t wifi_manager_start(void);
