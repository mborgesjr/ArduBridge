# ArduBridge (v3.21)

![Built with PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-blue?style=for-the-badge&logo=platformio)

**ArduBridge** é um firmware de I/O de alta performance e baixa RAM que transforma um Arduino (Pro Mini, Nano, Mega) em uma ponte de comunicação (bridge) inteligente.

Ele é projetado para ser controlado por um dispositivo mestre (como um PC, Raspberry Pi, ou uma aplicação Delphi) através de um protocolo serial robusto e de alta velocidade (115200 baud).

Este projeto foi **altamente otimizado para estabilidade em dispositivos com pouca memória** (como o ATmega328P do Pro Mini), eliminando completamente a alocação dinâmica de memória (classe `String`) e utilizando `char` arrays estáticos.

## 🎛️ Principais Recursos

* **Protocolo Serial Robusto:** Comunicação em alta velocidade (115200 baud) usando a porta serial de hardware (Pinos 0/1).
* **Baixo Uso de RAM:** Altamente otimizado para caber e rodar estavelmente no ATmega328P (Pro Mini / Nano), que possui apenas 2KB de RAM.
* **Modo de Operação Duplo:** Inicia em modo **Manual** (silencioso) e pode ser ativado para modo **Automático** (`MODE:AUTO`), que reporta mudanças de valor em tempo real.
* **Watchlist Configurável:** Permite definir (via comando) quais pinos analógicos e digitais devem ser monitorados, ignorando ruídos de pinos não conectados.
* **Leitura por Demanda:** Comandos `DUMPA` e `DUMPD` para ler o estado de todos os pinos monitorados instantaneamente.
* **Controle de I/O:** Suporte total para `READ` (leitura), `OUT` (escrita digital) e `PWM` (escrita analógica).
* **Configuração de Pinos:** Comando `SETMODE` para definir pinos como `INPUT`, `INPUT_PULLUP` ou `OUTPUT`.
* **Calibração de Sensor:** Comandos `CAL` para aplicar `OFFSET` (soma/subtração) e `GAIN` (multiplicação) em pinos analógicos individualmente.
* **Protocolo Simplificado (v3.18+):** **Não usa checksum.** Isso simplifica a implementação do software mestre (Delphi, Python, etc.).
* **Resposta de Erro Padronizada:** Qualquer comando malformado, inválido ou falho sempre retornará `#E:FAIL\n`, facilitando o tratamento de erros no lado do mestre.

## 🚀 Instalação e Hardware

### 1. Software
Este projeto é construído usando [PlatformIO](https://platformio.org/) (recomendado) ou o Arduino IDE.

* **Se usar PlatformIO:** Simplesmente abra esta pasta de projeto e clique em "Upload" (build) para o seu ambiente (ex: `promini`).
* **Se usar Arduino IDE:**
    1.  Crie um novo sketch.
    2.  Copie o conteúdo de `src/main.cpp` para o seu sketch `.ino`.
    3.  Selecione a placa correta (ex: "Arduino Pro or Pro Mini", ATmega328P, 5V, 16MHz) e a porta COM.
    4.  Clique em Upload.

### 2. Conexão (Hardware)
O ArduBridge usa a porta serial de hardware principal (Pinos 0 e 1) para comunicação.

* **Placa:** Arduino Pro Mini / Nano / Uno
* **Pino 0 (RX):** Conecte ao pino **TX** do seu dispositivo mestre (ex: conversor USB-Serial, Raspberry Pi).
* **Pino 1 (TX):** Conecte ao pino **RX** do seu dispositivo mestre.

> **⚠️ ATENÇÃO!** Você **deve** desconectar fisicamente qualquer coisa ligada aos pinos 0 e 1 antes de tentar enviar um novo firmware para o Arduino via USB, ou o upload falhará.

## 📖 Guia de Funções e Protocolo (v3.21)

O ArduBridge é controlado por comandos de texto simples enviados pela porta serial.

* **Porta:** `Serial` (Hardware, Pinos 0/1, via USB).
* **Velocidade:** `115200` baud.
* **Modo Inicial:** Manual (silencioso).

### Formato dos Comandos

**Formato de Envio (Mestre → Arduino):**
`#C:<porta>:<ACAO>:<valor>\n`
(Ex: `#C:0:MODE:AUTO\n`)

**Formato de Resposta (Arduino → Mestre):**
* **Sistema:** `#S:<payload>\n` (Enviado pelo sistema)
* **OK:** `#A:OK:<msg>\n` (Comando aceito)
* **Dados:** `#R:<payload>\n` (Dados de sensores)
* **ERRO (Padronizado):** `#E:FAIL\n` (Comando falhou)

### Headers de Status

O Arduino envia mensagens de sistema (`S:`) para sinalizar eventos:

* `#S:ArduBridge:READY:v3.21\n`: Enviado **uma vez** quando o Arduino inicia. Seu software mestre deve aguardar esta mensagem antes de enviar comandos.
* `#S:ArduBridge:RESETTING_NOW\n`: Enviado em resposta ao comando `RESET`, 500ms antes da reinicialização.

### Exemplo de Sessão

1.  O Arduino é ligado.
    * **Arduino envia:** `#S:ArduBridge:READY:v3.21\n`
2.  O Mestre (Delphi) define uma watchlist para monitorar apenas A0 e D7.
    * **Mestre envia:** `#C:0:WLA:0\n`
    * **Arduino responde:** `#A:OK:WLA_SET=0\n`
    * **Mestre envia:** `#C:0:WLD:7\n`
    * **Arduino responde:** `#A:OK:WLD_SET=7\n`
3.  O Mestre ativa o modo automático.
    * **Mestre envia:** `#C:0:MODE:AUTO\n`
    * **Arduino responde:** `#A:OK:MODE=AUTO\n`
4.  O usuário mexe em um sensor em A0.
    * **Arduino envia:** `#R:A0:123\n`
    * **Arduino envia:** `#R:A0:125\n`
5.  O usuário mexe em um sensor em A1 (não está na watchlist).
    * **Arduino envia:** *(nada)*
6.  O Mestre desliga o modo automático.
    * **Mestre envia:** `#C:0:MODE:MANUAL\n`
    * **Arduino responde:** `#A:OK:MODE=MANUAL\n`

---

### 📚 Referência Completa de Comandos (Helper v3.21)

| Ação | Porta | Valor | Exemplo de Comando (sem `\n`) | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| **WLA** | `0` | `pinos` | `#C:0:WLA:0,1,5` | **(Watchlist)** Define a lista de pinos Analógicos a monitorar (ex: "0,1,5"). |
| **WLD** | `0` | `pinos` | `#C:0:WLD:2,3,8` | **(Watchlist)** Define a lista de pinos Digitais a monitorar (ex: "2,3,8"). |
| **WL** | `0` | `CLEAR` | `#C:0:WL:CLEAR` | **(Watchlist)** Limpa todos os filtros da watchlist (volta a monitorar tudo). |
| **RESET** | `0` | (vazio) | `#C:0:RESET` | Reinicia o ArduBridge (delay de 500ms). Responde com `#S:ArduBridge:RESETTING_NOW\n`. |
| **MODE** | `0` | `AUTO` | `#C:0:MODE:AUTO` | Ativa o modo automático (envia dados ao detectar mudança). |
| **MODE** | `0` | `MANUAL` | `#C:0:MODE:MANUAL` | Desativa o modo automático (modo padrão, silencioso). |
| **DUMPA** | `0` | (vazio) | `#C:0:DUMPA` | Leitura manual de todos os pinos Analógicos (obedece a watchlist). |
| **DUMPD** | `0` | (vazio) | `#C:0:DUMPD` | Leitura manual de todos os pinos Digitais (obedece a watchlist). |
| **READ** | `A#` / `D#`| (vazio) | `#C:A1:READ` | Leitura manual de um pino específico (ignora a watchlist). |
| **OUT** | `D#` | `0` ou `1` | `#C:D7:OUT:1` | Define o estado de uma saída digital (HIGH ou LOW). |
| **PWM** | `D#` | `0`-`255` | `#C:D9:PWM:128` | Define o brilho de uma saída PWM (nos pinos `~`). |
| **SETMODE** | `D#` | `INPUT` | `#C:D2:SETMODE:INPUT` | Define o modo do pino (INPUT, INPUT_PULLUP, OUTPUT). |
| **RATE** | `0` | `ms` | `#C:0:RATE:500` | Define o intervalo de varredura do modo AUTO (em milissegundos, min 20). |
| **AVG** | `0` | `1`-`4` | `#C:0:AVG:4` | Define o tamanho da janela de média móvel para suavizar ruído (máx 4). |
| **THR** | `0` | `valor` | `#C:0:THR:3` | Define o "limiar" (threshold) de mudança analógica. Um valor só é enviado se mudar mais que X. |
| **CAL** | `A#` | `OFF:val` | `#C:A0:CAL:OFF:-10` | Adiciona um "offset" à leitura do sensor (Leitura = Real - 10). |
| **CAL** | `A#` | `GAIN:n/d` | `#C:A0:CAL:GAIN:5/10` | Aplica um "ganho" à leitura (Leitura = Real * (5 / 10)). |
| **CAL** | `0` | `RESET` | `#C:0:CAL:RESET` | Reseta todas as calibrações de Offset e Gain para o padrão. |

## ⚖️ Licença

Este projeto está licenciado sob a [Licença MIT](LICENSE).
