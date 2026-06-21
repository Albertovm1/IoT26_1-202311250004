#include "ds18b20.h"

#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DS18B20";

/* ─── Primitivas 1-Wire (bit-bang) ─────────────────────────────────────── */

static void pin_low(void)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DS18B20_GPIO, 0);
}

static void pin_release(void)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT);
}

static int pin_read(void)
{
    gpio_set_direction(DS18B20_GPIO, GPIO_MODE_INPUT);
    return gpio_get_level(DS18B20_GPIO);
}

/**
 * @brief Reset 1-Wire — retorna 1 se o sensor foi detectado.
 */
static int onewire_reset(void)
{
    pin_low();
    esp_rom_delay_us(480);   /* Pull-down >= 480 µs  */
    pin_release();
    esp_rom_delay_us(70);    /* Aguarda presença      */
    int presence = !pin_read();
    esp_rom_delay_us(410);   /* Completa o slot       */
    return presence;
}

static void onewire_write_bit(int bit)
{
    pin_low();
    esp_rom_delay_us(bit ? 6 : 60);
    pin_release();
    esp_rom_delay_us(bit ? 64 : 10);
}

static int onewire_read_bit(void)
{
    pin_low();
    esp_rom_delay_us(6);
    pin_release();
    esp_rom_delay_us(9);
    int bit = pin_read();
    esp_rom_delay_us(55);
    return bit;
}

static void onewire_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        onewire_write_bit((byte >> i) & 0x01);
    }
}

static uint8_t onewire_read_byte(void)
{
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++) {
        byte |= (onewire_read_bit() << i);
    }
    return byte;
}

/* ─── Comandos DS18B20 ──────────────────────────────────────────────────── */

#define CMD_SKIP_ROM        0xCC   /* Fala com todos os sensores no barramento */
#define CMD_CONVERT_T       0x44   /* Inicia conversão de temperatura          */
#define CMD_READ_SCRATCHPAD 0xBE   /* Lê os 9 bytes do scratchpad              */
#define CMD_WRITE_SCRATCHPAD 0x4E  /* Configura resolução                      */

/* Tempo de conversão por resolução (ms) */
static const int conversion_time_ms[] = {94, 188, 375, 750}; /* 9,10,11,12 bits */

/* ─── API pública ───────────────────────────────────────────────────────── */

esp_err_t ds18b20_read_temperature(float *temperature)
{
    if (temperature == NULL) return ESP_ERR_INVALID_ARG;

    /* Configura pino */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* Pull-up interno */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    /* ── Passo 1: configura resolução ─────────────────────────────────── */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "Sensor não detectado na inicialização");
        return ESP_ERR_NOT_FOUND;
    }
    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_WRITE_SCRATCHPAD);
    onewire_write_byte(0x00);  /* TH register */
    onewire_write_byte(0x00);  /* TL register */
    /* Byte de configuração: bits 5-6 definem resolução
       0x1F=9bit, 0x3F=10bit, 0x5F=11bit, 0x7F=12bit */
    uint8_t resolution_byte = 0x1F | (((DS18B20_RESOLUTION - 9) & 0x03) << 5);
    onewire_write_byte(resolution_byte);

    /* ── Passo 2: solicita conversão ──────────────────────────────────── */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "Sensor não detectado antes da conversão");
        return ESP_ERR_NOT_FOUND;
    }
    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_CONVERT_T);

    /* Aguarda conversão (modo parasita: sensor alimentado pelo barramento) */
    vTaskDelay(pdMS_TO_TICKS(conversion_time_ms[DS18B20_RESOLUTION - 9]));

    /* ── Passo 3: lê scratchpad ───────────────────────────────────────── */
    if (!onewire_reset()) {
        ESP_LOGE(TAG, "Sensor não detectado na leitura");
        return ESP_ERR_NOT_FOUND;
    }
    onewire_write_byte(CMD_SKIP_ROM);
    onewire_write_byte(CMD_READ_SCRATCHPAD);

    uint8_t scratchpad[9];
    for (int i = 0; i < 9; i++) {
        scratchpad[i] = onewire_read_byte();
    }

    /* ── CRC simples (byte 8 deve ser CRC dos primeiros 8) ────────────── */
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
        ESP_LOGE(TAG, "CRC inválido: calculado=0x%02X, recebido=0x%02X", crc, scratchpad[8]);
        return ESP_ERR_INVALID_CRC;
    }

    /* ── Converte para graus Celsius ─────────────────────────────────── */
    int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);

    /* Para resoluções menores que 12 bits, zera os bits inválidos */
    int unused_bits = 12 - DS18B20_RESOLUTION;
    raw = (raw >> unused_bits) << unused_bits;

    *temperature = (float)raw / 16.0f;

    ESP_LOGI(TAG, "Temperatura lida: %.2f °C", *temperature);
    return ESP_OK;
}
