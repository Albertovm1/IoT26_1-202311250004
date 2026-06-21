#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#define DS18B20_GPIO        GPIO_NUM_4
#define DS18B20_RESOLUTION  12

esp_err_t ds18b20_read_temperature(float *temperature);