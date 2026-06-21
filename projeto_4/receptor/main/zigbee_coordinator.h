#pragma once

#include "esp_err.h"

/**
 * @brief Inicializa o stack Zigbee como Coordinator.
 *        Cria a rede e fica aguardando dispositivos se associarem.
 *        Quando recebe temperatura de um node, publica via MQTT.
 */
void zigbee_coordinator_start(void);
