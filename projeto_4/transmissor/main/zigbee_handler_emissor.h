#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define ZB_EP            1
#define ZB_CHANNEL_MASK  0x07FFF800U // Canais 11 ao 26
#define ZB_JOINED_BIT    BIT0

void zigbee_emissor_start(void);
EventGroupHandle_t zigbee_emissor_get_event_group(void);
esp_err_t zigbee_emissor_send_temperature(float temperature);