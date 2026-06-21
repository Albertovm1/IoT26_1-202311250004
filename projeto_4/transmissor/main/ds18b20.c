#include "ds18b20.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";

#define CMD_SKIP_ROM         0xCC
#define CMD_CONVERT_T        0x44
#define CMD_READ_SCRATCHPAD  0xBE

// Flag para garantir que a GPIO será configurada apenas uma vez
static bool g_gpio_initialized = false;

static void ds18b20_init_gpio(void) {
    if (!g_gpio_initialized) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << DS18B20_GPIO),
            .mode = GPIO_MODE_INPUT_OUTPUT_OD, // Modo correto: Entrada/Saída em Dreno Aberto
            .pull_up_en = GPIO_PULLUP_ENABLE,  // Ativa o resistor interno de auxílio
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level(DS18B20_GPIO, 1); // Deixa a linha solta em nível alto por padrão
        g_gpio_initialized = true;
        ESP_LOGI(TAG, "GPIO %d inicializada em modo Open-Drain com Pull-up.", DS18B20_GPIO);
    }
}

static inline void pin_low(void) {
    gpio_set_level(DS18B20_GPIO, 0);
}

static inline void pin_release(void) {
    gpio_set_level(DS18B20_GPIO, 1);
}

static inline int pin_read(void) {
    return gpio_get_level(DS18B20_GPIO);
}

static int onewire_reset(void) {
    int presence = 0;
    
    pin_low();
    esp_rom_delay_us(480); // Pulso de Reset
    
    // Desativa interrupções críticas temporariamente se necessário, 
    // mas o esp_rom_delay_us costuma ser preciso o suficiente aqui.
    pin_release();
    esp_rom_delay_us(70);  // Espera o sensor responder
    
    presence = !pin_read(); // Lê o pulso de presença (0 = presente, pois inverteu)
    
    esp_rom_delay_us(410); // Completa o ciclo de 960us
    return presence;
}

static void onewire_write_bit(int bit) {
    if (bit) {
        pin_low();
        esp_rom_delay_us(6);
        pin_release();
        esp_rom_delay_us(64);
    } else {
        pin_low();
        esp_rom_delay_us(60);
        pin_release();
        esp_rom_delay_us(10);
    }
}

static int onewire_read_bit(void) {
    int bit;
    pin_low();
    esp_rom_delay_us(6);
    pin_release();
    esp_rom_delay_us(9);
    bit = pin_read();
    esp_rom_delay_us(55);
    return bit;
}

static void onewire_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        onewire_write_bit(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t onewire_read_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        if (onewire_read_bit()) {
            byte |= (1 << i);
        }
    }
    return byte;
}

esp_err_t ds18b20_read_temperature(float *temperature) {
    // Garanta que a GPIO está configurada corretamente antes de ler
    ds18b20_init_gpio();

    ESP_LOGD(TAG, "Iniciando leitura do sensor...");
    if (!onewire_reset()) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_CONVERT_T);

    // Tempo necessário para a conversão de 12 bits
    vTaskDelay(pdMS_TO_TICKS(750));

    if (!onewire_reset()) {
        return ESP_ERR_NOT_FOUND;
    }

    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_READ_SCRATCHPAD);

    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }

    uint8_t crc = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t b = scratchpad[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }

    if (crc != scratchpad[8]) {
        ESP_LOGW(TAG, "Erro de CRC: Calculado=0x%02X, Recebido=0x%02X", crc, scratchpad[8]);
        return ESP_ERR_INVALID_CRC;
    }

    int16_t raw = (scratchpad[1] << 8) | scratchpad[0];
    *temperature = (float)raw / 16.0f;
    return ESP_OK;
}