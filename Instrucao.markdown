# 🚀 Projeto 2 – Controle de Iluminação com Gateway (BeagleBone Black)

## 🎯 Objetivo

Configurar um **Gateway IoT** usando a BeagleBone Black (BBB) para centralizar a comunicação MQTT entre os nós ESP32.

---

# 🧠 Arquitetura do Sistema

```
ESP32 (Botão) → MQTT → BeagleBone (Broker) → MQTT → ESP32 (LED)
```

---

# 🛠️ Parte A – Configuração da BeagleBone Black (Gateway)

## 1. Acessar a BBB via SSH

```bash
ssh debian@<IP_DA_BBB>
```

---

## 2. Atualizar sistema

```bash
sudo apt update
```

---

## 3. Instalar Mosquitto

```bash
sudo apt install mosquitto mosquitto-clients -y
```

---

## 4. Habilitar inicialização automática

```bash
sudo systemctl enable mosquitto
```

---

## 5. Criar arquivo de configuração

```bash
sudo nano /etc/mosquitto/conf.d/external.conf
```

---

## 6. Inserir configuração

```conf
listener 1883 0.0.0.0
allow_anonymous true
```

### Salvar:

* Ctrl + O → Enter
* Ctrl + X

---

## 7. Reiniciar o serviço

```bash
sudo systemctl restart mosquitto
```

---

## 8. Verificar se está funcionando

```bash
netstat -tln | grep 1883
```

✔ Deve aparecer:

```
0.0.0.0:1883
```

---

## 9. Descobrir o IP da BBB

```bash
hostname -I
```

Exemplo:

```
192.168.0.105
```

---

## 10. Testar MQTT localmente

### Terminal 1:

```bash
mosquitto_sub -t "teste"
```

### Terminal 2:

```bash
mosquitto_pub -t "teste" -m "Ola BBB"
```

✔ Se aparecer mensagem → OK

---

# 📡 Parte B – Atualização dos ESP32

## 1. Alterar o broker no código

### Em `NODE_A.c` e `NODE_B.c`:

```c
#define BROKER_URI "mqtt://192.168.0.105"
```

(Substitua pelo IP da BBB)

---

## 2. Verificar Wi-Fi

Certifique-se que:

* ESP32 está conectado ao mesmo roteador da BBB

---

## 3. Compilar e enviar código

```bash
idf.py build
idf.py -p <PORTA> flash monitor
```

---

# 🧪 Teste Final

## Passo 1

Pressione o botão (NODE A)

## Passo 2

Observe o LED (NODE B)

✔ Deve ligar/desligar

---

# 🔍 Monitoramento no Gateway

Para ver todas as mensagens:

```bash
mosquitto_sub -t "#"
```

---

# 💾 Desafio Extra (log de eventos)

Salvar mensagens em arquivo:

```bash
mosquitto_sub -t "#" > log.txt
```

---

# ⚠️ Problemas Comuns

## ❌ ESP32 não conecta

* Verifique IP da BBB
* Verifique Wi-Fi

---

## ❌ Porta não aparece

* Verifique configuração do Mosquitto

---

## ❌ Nada chega no MQTT

* Verifique tópico correto:

```
ifpb/projeto/led
```

---

# ✅ Checklist Final

* [ ] Mosquitto instalado
* [ ] Configuração criada
* [ ] Porta 1883 ativa
* [ ] IP da BBB identificado
* [ ] ESP32 atualizado
* [ ] Teste funcionando

---

# 🎯 Conclusão

Você implementou uma arquitetura IoT completa com:

* Gateway local
* Comunicação MQTT
* Nodes distribuídos
* Monitoramento de rede

---