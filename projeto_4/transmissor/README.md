# Zigbee Node — Sensor de Temperatura DS18B20

Node de monitoramento ambiental usando **ESP32C6** + **DS18B20** com comunicação **Zigbee**.

---

## Hardware

| Componente | Pino ESP32C6 |
|---|---|
| DS18B20 DATA | GPIO4 |
| DS18B20 VCC | 3.3V |
| DS18B20 GND | GND |
| Resistor pull-up | 4.7kΩ entre DATA e VCC |

> O resistor de **4.7kΩ** é obrigatório para o protocolo 1-Wire funcionar corretamente.

---

## Estrutura do projeto

```
zigbee_node/
├── CMakeLists.txt          ← raiz do projeto ESP-IDF
├── sdkconfig.defaults      ← configurações Zigbee + sleep
└── main/
    ├── CMakeLists.txt
    ├── main.c              ← loop principal (lê, envia, espera)
    ├── ds18b20.h / .c      ← driver 1-Wire para o DS18B20
    └── zigbee_handler.h/.c ← stack Zigbee (End Device, cluster de temperatura)
```

---

## Como compilar e gravar

```bash
# 1. Abra o terminal ESP-IDF (rode export.ps1 antes no Windows)

# 2. Entre na pasta do projeto
cd zigbee_node

# 3. Configure o target para ESP32C6
idf.py set-target esp32c6

# 4. (Opcional) ajuste configurações
idf.py menuconfig

# 5. Compile
idf.py build

# 6. Grave no dispositivo
idf.py -p COMX flash monitor
```

---

## Configurações importantes

### Alterar o GPIO do DS18B20
Edite `ds18b20.h`:
```c
#define DS18B20_GPIO  GPIO_NUM_4   // ← altere aqui
```

### Alterar o intervalo de envio
Edite `main.c`:
```c
#define SEND_INTERVAL_MS  (30 * 1000)  // ← 30 segundos
```

### Alterar o canal Zigbee
Edite `zigbee_handler.h`:
```c
#define ZIGBEE_CHANNEL_MASK  (1 << 15)  // ← canal 15; use (1<<11) até (1<<26)
```

---

## Funcionamento

```
┌─────────────────────────────────────────────────────┐
│                    ESP32C6 Node                      │
│                                                      │
│  1. Liga e entra na rede Zigbee (steering)           │
│  2. Loop a cada 30s:                                 │
│     a. Lê temperatura via DS18B20 (1-Wire)           │
│     b. Converte para formato ZCL (int16, 0.01°C)     │
│     c. Envia report ao Coordinator (Gateway)         │
│     d. Dorme o tempo restante                        │
└────────────────────┬────────────────────────────────┘
                     │ Zigbee (IEEE 802.15.4)
                     ▼
           ┌─────────────────┐
           │  Gateway        │
           │  (ESP32C6 +     │
           │  BBB/RPi)       │
           │  Broker MQTT    │
           └────────┬────────┘
                    │ MQTT
                    ▼
           ┌─────────────────┐
           │  Nuvem          │
           │  Dashboard +    │
           │  Banco de dados │
           └─────────────────┘
```

---

## Protocolo Zigbee utilizado

- **Perfil:** Home Automation (0x0104)
- **Device ID:** Temperature Sensor (0x0302)
- **Cluster:** Temperature Measurement (0x0402)
- **Atributo:** MeasuredValue (0x0000) — int16, unidade = 0.01°C
  - Exemplo: 2575 = 25.75°C
- **Tipo de dispositivo:** End Device (dorme entre envios)

---

## Dependências ESP-IDF

Requer **ESP-IDF v5.1+** com suporte ao componente `esp-zigbee-sdk`.

Se o componente Zigbee não estiver instalado:
```bash
idf.py add-dependency "espressif/esp-zigbee-sdk"
```
