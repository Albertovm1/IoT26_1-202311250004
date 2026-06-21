#pragma once

#include "esp_err.h"
#include "ezbee/zcl/zcl_type.h"    /* EZB_ZCL_CLUSTER_SERVER */
#include "ezbee/app_signals.h"      /* ezb_app_signal_t */

#define ZIGBEE_ENDPOINT          1
#define ZIGBEE_HA_PROFILE_ID     0x0104
#define ZIGBEE_DEVICE_ID         0x0302
#define ZIGBEE_TEMP_CLUSTER_ID   0x0402
#define ZIGBEE_TEMP_ATTR_ID      0x0000
#define ZIGBEE_CHANNEL_MASK      (1 << 15)

void      zigbee_node_start(void);
esp_err_t zigbee_send_temperature(float temperature);
