// arduino_adc_bridge_v3 - main.cpp
// Versão 3.21 - Corrigido erro de compilação (redefinição de 'watchlistParseBuffer').
// Versão 3.20 - Adicionado nome "ArduBridge" aos headers de status e delay no RESET.
// Versão 3.18 - VERIFICAÇÃO DE CHECKSUM REMOVIDA.
// Versão 3.16 - Comando "DUMP" dividido em "DUMPA" e "DUMPD".
// Versão 3.15 - Watchlist por lista de pinos (ex: "0,1,5").
// Versão 3.11 - Serial Única (USB), início em modo MANUAL.
// Versão 3.7  - REMOVIDO: ALIAS. Reduzido MAX_AVG_WINDOW e buffers.

#include <Arduino.h>

// ====================== CONFIG (Otimizado para RAM) ======================
#define DEBUG false      // DEBUG DEVE SER FALSO. Serial é usado para comunicação.
#define LINK_BAUD 115200 // Baud Rate para DADOS (Serial/USB)

#define DEFAULT_RATE 300
#define DEFAULT_AVG 4
#define DEFAULT_THR 2

// --- Otimizações de RAM v3.7 ---
#define MAX_AVG_WINDOW 4   // Máximo de 4 amostras para média
// Buffers de COM reduzidos
#define RX_BUFFER_SIZE 64  
#define TX_BUFFER_SIZE 100 // Mínimo para DUMP
#define CMD_BUFFER_SIZE 64 
#define PAYLOAD_BUFFER_SIZE 100 

// --- Watchlist (v3.15) ---
bool watchlistActive = false;     // Se true, filtra pelos masks. Se false, monitora tudo.
uint8_t analogWatchMask = 0;      // Bitmask para A0-A7
uint16_t digitalWatchMask = 0;    // Bitmask para D0-D15

// Buffers para parsing da watchlist (reusando CMD_BUFFER_SIZE)
char watchlistParseBuffer[CMD_BUFFER_SIZE];
// --- Fim Otimizações ---


// ====================== BOARD AUTO-DETECT ======================
#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  #define ANALOG_PORT_COUNT 8   // A0..A7
  #define DIGITAL_MAX_PIN 13
#elif defined(__AVR_ATmega2560__)
  #define ANALOG_PORT_COUNT 16
  #define DIGITAL_MAX_PIN 53
#else
  #define ANALOG_PORT_COUNT 6
  #define DIGITAL_MAX_PIN 13
#endif

// ====================== GLOBAL STATE ======================

#define linkSerial Serial 

unsigned long lastScan = 0;
unsigned long scanInterval = DEFAULT_RATE;
int avgWindow = DEFAULT_AVG;
int analogThreshold = DEFAULT_THR;
bool MODE_AUTO = false; // <<< Inicia em modo MANUAL

// Buffers de RX/TX
char rxBuffer[RX_BUFFER_SIZE];
uint8_t rxBufferIdx = 0;
char txBuffer[TX_BUFFER_SIZE];
char payloadBuffer[PAYLOAD_BUFFER_SIZE];
char cmdParseBuffer[CMD_BUFFER_SIZE]; 
// Linha duplicada 'watchlistParseBuffer' removida (v3.21)

// buffers e cache
int lastAnalogSent[ANALOG_PORT_COUNT];
int lastDigitalSent[DIGITAL_MAX_PIN+1];
int analogBuffers[ANALOG_PORT_COUNT][MAX_AVG_WINDOW];
int analogBufIndex[ANALOG_PORT_COUNT];
int analogSamplesCount[ANALOG_PORT_COUNT];
int analogFiltered[ANALOG_PORT_COUNT];
int analogOffset[ANALOG_PORT_COUNT];
int analogGainNum[ANALOG_PORT_COUNT];
int analogGainDen[ANALOG_PORT_COUNT];
int digitalMode[DIGITAL_MAX_PIN+1];

const int pwmCandidates[] = {3,5,6,9,10,11};
const int pwmCount = sizeof(pwmCandidates)/sizeof(pwmCandidates[0]);

// ====================== UTIL (Sem String) ======================
void dbg(const char *s) {
  #if DEBUG
    Serial.print("[DBG] ");
    Serial.println(s);
  #endif
}
void dbg(const char *s1, const char *s2, const char *s3 = "", const char *s4 = "") {
  #if DEBUG
    Serial.print("[DBG] ");
    Serial.print(s1);
    Serial.print(s2);
    Serial.print(s3);
    Serial.println(s4);
  #endif
}

// Checksum não é mais usado
uint8_t computeChecksum(const char *content) {
  uint8_t sum = 0;
  while (*content) {
    sum += (uint8_t)(*content);
    content++;
  }
  return sum & 0xFF;
}

void toHex2(uint8_t v, char* out) {
  const char *h="0123456789ABCDEF";
  out[0]=h[(v>>4)&0xF];
  out[1]=h[v&0xF];
  out[2]=0;
}

void sendPacket(const char *payload) {
  // Checksum removido do envio (v3.18)
  snprintf(txBuffer, TX_BUFFER_SIZE, "#%s\n", payload);
  linkSerial.print(txBuffer);
}
void sendOK(const char *msg){ 
  snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "A:OK:%s", msg);
  sendPacket(payloadBuffer); 
}

// Erro padronizado
void sendErr(const char *ignoredMsg = ""){ 
  snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "E:FAIL");
  sendPacket(payloadBuffer); 
}

// ====================== ALIASES (Removido) ======================
// ...

// ====================== INIT STATE ======================
void resetState() {
  for (int i=0;i<ANALOG_PORT_COUNT;i++) {
    lastAnalogSent[i] = -1;
    analogBufIndex[i]=0;
    analogSamplesCount[i]=0;
    analogFiltered[i]=-1;
    analogOffset[i]=0;
    analogGainNum[i]=1000;
    analogGainDen[i]=1000;
    for (int j=0;j<MAX_AVG_WINDOW;j++) analogBuffers[i][j]=0;
  }
  
  for (int p=0;p<=DIGITAL_MAX_PIN;p++) {
    digitalMode[p]=0; 
    if (p>=2 && p<=DIGITAL_MAX_PIN) {
      pinMode(p, INPUT_PULLUP);
      digitalMode[p]=1;
      lastDigitalSent[p] = digitalRead(p); 
    } else {
      lastDigitalSent[p] = 0; 
    }
  }
}

// ====================== FILTER (avg) ======================
int pushAnalogSample(int ch, int sample) {
  int w = avgWindow;
  if (w<=1) { analogFiltered[ch]=sample; return analogFiltered[ch]; }
  if (w > MAX_AVG_WINDOW) w = MAX_AVG_WINDOW;
  
  analogBuffers[ch][analogBufIndex[ch]] = sample;
  analogBufIndex[ch] = (analogBufIndex[ch]+1) % w;
  if (analogSamplesCount[ch] < w) analogSamplesCount[ch]++;
  long sum=0;
  for (int i=0;i<analogSamplesCount[ch];i++) sum += analogBuffers[ch][i];
  analogFiltered[ch] = (int)(sum / analogSamplesCount[ch]);
  return analogFiltered[ch];
}

// ====================== SCAN & REPORT (v3.13 com Watchlist) ======================
void sendSingleAnalog(int ch) {
  int v = analogFiltered[ch];
  snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "R:A%d:%d", ch, v);
  sendPacket(payloadBuffer);
}
void sendSingleDigital(int pin, int val) {
  snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "R:D%d:%d", pin, val);
  sendPacket(payloadBuffer);
  lastDigitalSent[pin]=val;
}

void scanAndReport() {
  int changedMask = 0;
  
  // --- Leitura Analógica (com Watchlist) ---
  for (int ch=0; ch<ANALOG_PORT_COUNT; ch++) {
    if (watchlistActive && !(analogWatchMask & (1 << ch))) {
      lastAnalogSent[ch] = -1; 
      continue;
    }
    
    int raw = analogRead(ch);
    long v = raw + analogOffset[ch];
    v = (v * analogGainNum[ch]) / analogGainDen[ch];
    int filt = pushAnalogSample(ch, (int)v);
    if (lastAnalogSent[ch] == -1) {
      if (filt != 0) changedMask |= (1<<ch);
    } else {
      if (abs(filt - lastAnalogSent[ch]) > analogThreshold) changedMask |= (1<<ch);
    }
  }

  // --- Leitura Digital (com Watchlist) ---
  for (int p=2; p<=DIGITAL_MAX_PIN; p++) {
    if (watchlistActive && !(digitalWatchMask & (1UL << p))) {
      continue;
    }
    
    int val = digitalRead(p);
    if (val != lastDigitalSent[p]) {
      sendSingleDigital(p,val);
    }
  }

  // --- Envio Agrupado (Analógico) ---
  int cnt=0;
  for (int ch=0; ch<ANALOG_PORT_COUNT; ch++) if (changedMask & (1<<ch)) cnt++;
  
  if (cnt == 1) {
    for (int ch=0; ch<ANALOG_PORT_COUNT; ch++) if (changedMask & (1<<ch)) {
      sendSingleAnalog(ch);
      lastAnalogSent[ch] = analogFiltered[ch];
    }
  } else if (cnt > 1) {
    strcpy(payloadBuffer, "R:G:");
    char* payloadPtr = payloadBuffer + strlen(payloadBuffer); 
    size_t remaining = PAYLOAD_BUFFER_SIZE - strlen(payloadBuffer);
    bool first=true;
    
    for (int ch=0; ch<ANALOG_PORT_COUNT; ch++) {
      if (changedMask & (1<<ch)) {
        int written = snprintf(payloadPtr, remaining, "%sA%d=%d", 
                          (first ? "" : ";"), ch, analogFiltered[ch]);
        
        payloadPtr += written;
        remaining -= written;
        first=false;
        lastAnalogSent[ch]=analogFiltered[ch];

        if (remaining < 20) break; 
      }
    }
    sendPacket(payloadBuffer);
  }
}

// ====================== COMMANDS PARSE & EXEC (v3.21) ======================
bool isPWMSupportedPin(int pin) {
  for (int i=0;i<pwmCount;i++) if (pwmCandidates[i] == pin) return true;
  return false;
}

// --- DUMP Dividido (v3.16) ---
void do_dump_analog() {
  strcpy(payloadBuffer, "R:DUMPA:");
  char* payloadPtr = payloadBuffer + strlen(payloadBuffer);
  size_t remaining = PAYLOAD_BUFFER_SIZE - strlen(payloadBuffer);
  bool firstAnalog = true;

  for (int ch=0; ch<ANALOG_PORT_COUNT; ch++) {
    if (watchlistActive && !(analogWatchMask & (1 << ch))) continue;

    if (!firstAnalog) { *payloadPtr = ';'; payloadPtr++; remaining--; }
    
    int raw = analogRead(ch);
    long v = raw + analogOffset[ch];
    v = (v * analogGainNum[ch]) / analogGainDen[ch];
    int filt = pushAnalogSample(ch, (int)v);
    
    int written = snprintf(payloadPtr, remaining, "A%d=%d", ch, filt);
    payloadPtr += written;
    remaining -= written;
    lastAnalogSent[ch] = filt;
    firstAnalog = false;
    if (remaining < 20) break;
  }
  sendPacket(payloadBuffer);
}

void do_dump_digital() {
  strcpy(payloadBuffer, "R:DUMPD:");
  char* payloadPtr = payloadBuffer + strlen(payloadBuffer);
  size_t remaining = PAYLOAD_BUFFER_SIZE - strlen(payloadBuffer);
  bool firstDigital=true;
  
  for (int p=2;p<=DIGITAL_MAX_PIN;p++) {
    if (watchlistActive && !(digitalWatchMask & (1UL << p))) continue;
        
    int val = digitalRead(p);
    int written = snprintf(payloadPtr, remaining, "%sD%d=%d", (firstDigital ? "" : ";"), p, val);
    payloadPtr += written;
    remaining -= written;
    lastDigitalSent[p] = val; 
    firstDigital=false;
    if (remaining < 10) break;
  }
  sendPacket(payloadBuffer);
}
// --- Fim DUMP ---

// --- Funções de Parsing de Watchlist (v3.15) ---
uint8_t parseAnalogWatchlist(char* valueStr) {
  uint8_t mask = 0;
  char* saveptr;
  strncpy(watchlistParseBuffer, valueStr, CMD_BUFFER_SIZE);
  watchlistParseBuffer[CMD_BUFFER_SIZE-1] = '\0';

  char* pStr = strtok_r(watchlistParseBuffer, ",", &saveptr);
  while (pStr != NULL) {
    int p = atoi(pStr);
    if (p >= 0 && p < ANALOG_PORT_COUNT) {
      mask |= (1 << p);
    }
    pStr = strtok_r(NULL, ",", &saveptr);
  }
  return mask;
}

uint16_t parseDigitalWatchlist(char* valueStr) {
  uint16_t mask = 0;
  char* saveptr;
  strncpy(watchlistParseBuffer, valueStr, CMD_BUFFER_SIZE);
  watchlistParseBuffer[CMD_BUFFER_SIZE-1] = '\0';

  char* pStr = strtok_r(watchlistParseBuffer, ",", &saveptr);
  while (pStr != NULL) {
    int p = atoi(pStr);
    if (p >= 0 && p <= DIGITAL_MAX_PIN) {
      mask |= (1UL << p);
    }
    pStr = strtok_r(NULL, ",", &saveptr);
  }
  return mask;
}
// --- Fim Funções de Parsing de Watchlist ---


void processCommandLine(char* rawline) {
  static char empty_string[] = ""; 
  
  dbg("RX raw: ", rawline);
  
  if (rawline[0] != '#') {
    return sendErr(); // Erro padronizado (BadPrefix)
  }
  
  // Copia o 'rawline' (sem #) para o buffer de parsing
  strncpy(cmdParseBuffer, rawline + 1, CMD_BUFFER_SIZE);
  cmdParseBuffer[CMD_BUFFER_SIZE-1] = '\0';

  // Parse C:<port>:<ACTION>:<value>
  char* saveptr; 
  char* c_token = strtok_r(cmdParseBuffer, ":", &saveptr); 
  if (!c_token || c_token[0] != 'C') { return sendErr(); } 
  
  char* port = strtok_r(NULL, ":", &saveptr);
  char* action = strtok_r(NULL, ":", &saveptr);
  char* value = strtok_r(NULL, "", &saveptr); 

  if (!port || !action) { return sendErr(); } 
  
  if (!value) { value = empty_string; } 

  for (char* p = action; *p; ++p) *p = toupper(*p);

  dbg("Parsed: port=", port, " action=", action);
  dbg(" value=", value);

  const char* canonical = port; 

  // --- Handle Actions ---

  if (strcmp(action, "RESET") == 0) {
      // Modificação v3.20: Adicionado "ArduBridge" e delay aumentado
      sendPacket("S:ArduBridge:RESETTING_NOW");
      delay(500); // Delay aumentado para 500ms
      
      void(* resetFunc) (void) = 0;
      resetFunc();
      return; 
  }
  
  // *** Watchlist (v3.15) ***
  if (strcmp(action, "WLA") == 0) { // WatchList Analog
    if (value[0] == '\0') return sendErr(); 
    analogWatchMask = parseAnalogWatchlist(value);
    watchlistActive = true;
    char okMsg[30]; 
    snprintf(okMsg, 30, "WLA_SET=%s", value);
    sendOK(okMsg);
    return;
  }
  
  if (strcmp(action, "WLD") == 0) { // WatchList Digital
    if (value[0] == '\0') return sendErr(); 
    digitalWatchMask = parseDigitalWatchlist(value);
    watchlistActive = true;
    char okMsg[30]; 
    snprintf(okMsg, 30, "WLD_SET=%s", value);
    sendOK(okMsg);
    return;
  }
  
  if (strcmp(action, "WL") == 0) { // WatchList (geral)
    if (strcasecmp(value, "CLEAR") == 0) {
      watchlistActive = false;
      sendOK("WL_CLEARED");
      return;
    }
    return sendErr(); 
  }
  // *** Fim Watchlist v3.15 ***

  if (strcmp(action, "OUT") == 0) {
    if (canonical[0] != 'D') return sendErr(); 
    int pin = atoi(canonical+1);
    if (pin < 0 || pin > DIGITAL_MAX_PIN) return sendErr(); 
    int val = atoi(value);
    pinMode(pin, OUTPUT); digitalMode[pin]=2;
    digitalWrite(pin, val ? HIGH : LOW);
    
    char okMsg[15];
    snprintf(okMsg, 15, "D%d=%d", pin, val);
    sendOK(okMsg);
    return;
  }

  if (strcmp(action, "READ") == 0) {
    if (canonical[0] == 'A') {
      int ch = atoi(canonical+1);
      if (ch<0 || ch>=ANALOG_PORT_COUNT) return sendErr(); 
      int raw = analogRead(ch);
      long v = raw + analogOffset[ch];
      v = (v * analogGainNum[ch]) / analogGainDen[ch];
      int filt = pushAnalogSample(ch, (int)v);
      snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "R:A%d:%d", ch, filt);
      sendPacket(payloadBuffer);
      lastAnalogSent[ch]=filt;
      return;
    } else if (canonical[0] == 'D') {
      int pin = atoi(canonical+1);
      if (pin<0 || pin>DIGITAL_MAX_PIN) return sendErr(); 
      int val = digitalRead(pin);
      snprintf(payloadBuffer, PAYLOAD_BUFFER_SIZE, "R:D%d:%d", pin, val);
      sendPacket(payloadBuffer);
      lastDigitalSent[pin] = val;
      return;
    } else {
      return sendErr(); 
    }
  }

  if (strcmp(action, "PWM") == 0) {
    if (canonical[0] != 'D') return sendErr(); 
    int pin = atoi(canonical+1);
    int pwmv = atoi(value);
    if (!isPWMSupportedPin(pin)) return sendErr(); 
    pinMode(pin, OUTPUT);
    analogWrite(pin, constrain(pwmv, 0, 255));
    
    char okMsg[20];
    snprintf(okMsg, 20, "PWM D%d=%d", pin, pwmv);
    sendOK(okMsg);
    return;
  }

  if (strcmp(action, "MODE") == 0) {
    if (strcasecmp(value, "AUTO") == 0) { MODE_AUTO=true; sendOK("MODE=AUTO"); return; }
    if (strcasecmp(value, "MANUAL") == 0) { MODE_AUTO=false; sendOK("MODE=MANUAL"); return; }
    return sendErr(); 
  }

  if (strcmp(action, "RATE") == 0) {
    int ms = atoi(value);
    if (ms < 20) return sendErr(); 
    scanInterval = ms; 
    char okMsg[15];
    snprintf(okMsg, 15, "RATE=%d", ms);
    sendOK(okMsg); return;
  }

  if (strcmp(action, "AVG") == 0) {
    int w = atoi(value);
    if (w<1 || w > MAX_AVG_WINDOW) { 
        return sendErr(); 
    }
    avgWindow = w;
    for (int ch=0;ch<ANALOG_PORT_COUNT;ch++){ analogSamplesCount[ch]=0; analogBufIndex[ch]=0; }
    char okMsg[10];
    snprintf(okMsg, 10, "AVG=%d", w);
    sendOK(okMsg); return;
  }

  if (strcmp(action, "THR") == 0) {
    int t = atoi(value);
    if (t<0) return sendErr(); 
    analogThreshold = t; 
    char okMsg[10];
    snprintf(okMsg, 10, "THR=%d", t);
    sendOK(okMsg); return;
  }

  // --- Comandos DUMP divididos (v3.16) ---
  if (strcmp(action, "DUMP") == 0) { 
    return sendErr(); 
  }
  
  if (strcmp(action, "DUMPA") == 0) {
    do_dump_analog();
    sendOK("DUMPA_DONE");
    return;
  }
  
  if (strcmp(action, "DUMPD") == 0) {
    do_dump_digital();
    sendOK("DUMPD_DONE");
    return;
  }
  // --- Fim Comandos DUMP ---


  if (strcmp(action, "CAL") == 0) {
    if (strcasecmp(value, "RESET") == 0) {
      for (int ch=0;ch<ANALOG_PORT_COUNT;ch++){ analogOffset[ch]=0; analogGainNum[ch]=1000; analogGainDen[ch]=1000; }
      return sendOK("CAL_RESET");
    }
    if (canonical[0] != 'A') return sendErr(); 
    int ch = atoi(canonical+1);
    
    char* kind = strtok_r(value, ":", &saveptr);
    char* param = strtok_r(NULL, "", &saveptr);
    if (!kind || !param) return sendErr(); 

    if (strcasecmp(kind, "OFF") == 0) {
      int off = atoi(param); 
      analogOffset[ch]=off; 
      char okMsg[20];
      snprintf(okMsg, 20, "CAL_OFF A%d=%d", ch, off);
      sendOK(okMsg); return;
    } else if (strcasecmp(kind, "GAIN") == 0) {
      char* numStr = strtok_r(param, "/", &saveptr);
      char* denStr = strtok_r(NULL, "", &saveptr);
      if (!numStr || !denStr) return sendErr(); 
      int num = atoi(numStr);
      int den = atoi(denStr);
      if (num<=0 || den<=0) return sendErr(); 
      analogGainNum[ch]=num; analogGainDen[ch]=den;
      char okMsg[15];
      snprintf(okMsg, 15, "CAL_GAIN A%d", ch);
      sendOK(okMsg); return;
    } else return sendErr(); 
  }

  if (strcmp(action, "SETMODE") == 0) {
    if (canonical[0] != 'D') return sendErr(); 
    int pin = atoi(canonical+1);
    char okMsg[30];
    if (strcasecmp(value, "INPUT") == 0) { 
      pinMode(pin, INPUT); digitalMode[pin]=0; 
      snprintf(okMsg, 30, "SETMODE D%d=INPUT", pin);
      sendOK(okMsg); return; 
    }
    if (strcasecmp(value, "INPUT_PULLUP") == 0) { 
      pinMode(pin, INPUT_PULLUP); digitalMode[pin]=1; 
      snprintf(okMsg, 30, "SETMODE D%d=INPUT_PULLUP", pin);
      sendOK(okMsg); return; 
    }
    if (strcasecmp(value, "OUTPUT") == 0) { 
      pinMode(pin, OUTPUT); digitalMode[pin]=2; 
      snprintf(okMsg, 30, "SETMODE D%d=OUTPUT", pin);
      sendOK(okMsg); return; 
    }
    return sendErr(); 
  }

  sendErr(); 
}

// ====================== MAIN ======================
void setup() {
  linkSerial.begin(LINK_BAUD); 
  
  resetState();
  lastScan = millis();
  
  // Modificação v3.20: Adicionado nome "ArduBridge"
  sendPacket("S:ArduBridge:READY:v3.21"); // Atualizado para v3.21
}

void readLinkSerial() {
  while (linkSerial.available()) {
    char c = linkSerial.read();
    
    if (c == '\n' || c == '\r') { 
      if (rxBufferIdx > 0) {
        rxBuffer[rxBufferIdx] = '\0'; 
        processCommandLine(rxBuffer);
        rxBufferIdx = 0; 
      }
    } else if (c >= 32) { 
      if (rxBufferIdx < RX_BUFFER_SIZE - 1) {
        rxBuffer[rxBufferIdx++] = c;
      } else {
        rxBufferIdx = 0;
      }
    }
  }
}

void loop() {
  readLinkSerial(); 
  
  if (MODE_AUTO && (millis() - lastScan >= scanInterval)) {
    lastScan = millis();
    scanAndReport();
  }
}