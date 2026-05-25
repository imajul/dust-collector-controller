// ================================================================
//  ASPIRADORA AUTOMÁTICA — CAJA DE SIERRA
//  v4.0 — correcciones de bugs + mejoras de robustez
//
//  Sensor:    YHDC SCT-013  10A / 1V  → A0
//  Relé ON:   D7  → pulso botón ARRANQUE aspiradora
//  Relé OFF:  D8  → pulso botón PARADA aspiradora
//  Pulsador:  D2  → INT0 (interrupción externa)
//  Sync out:  D5  → D3 de la otra caja
//  Sync in:   D3  → D5 de la otra caja (+ pull-down 10kΩ a GND)
//  LED:       D13 → indicador ASPIRADORA ACTIVA
//
//  MISMO CÓDIGO EN LAS DOS CAJAS.
//  Solo cambiás MI_ID (0 para Caja A, 1 para Caja B).
//
//  ── CAMBIOS RESPECTO A v3 ───────────────────────────────────
//  - Bug fix: bloqueo_auto ahora se libera también cuando la
//    sierra REMOTA se apaga (antes quedaba atascado si solo
//    la sierra remota estaba activa al pulsar el botón).
//  - Bug fix: LED unificado → solo refleja aspiradora_activa.
//    Antes tenía dos dueños en conflicto (sierra local vs
//    aspiradora) y apagaba el LED aunque la aspiradora
//    siguiera activa por sierra remota.
//  - Mejora: bias del SCT-013 auto-calibrado en setup() en
//    lugar de hardcodeado a 512 (2.5V). Requiere que la
//    sierra esté apagada al encender el Arduino.
//  - Mejora: sync input con filtro de mayoría (5 lecturas,
//    umbral 3) para rechazar spikes de ruido del motor.
//
//  ── PINES REQUERIDOS ────────────────────────────────────────
//  ┌─────────────┬──────────────────────────────────────────┐
//  │ Pin         │ Función                                  │
//  ├─────────────┼──────────────────────────────────────────┤
//  │ A0          │ Señal SCT-013 (via divisor de bias)      │
//  │ D2 (INT0)   │ Pulsador manual NO (entre D2 y GND)      │
//  │ D3          │ Entrada sincronía ← D5 otra caja         │
//  │ D5          │ Salida sincronía  → D3 otra caja         │
//  │ D7          │ Relé ON (canal ARRANQUE)                 │
//  │ D8          │ Relé OFF (canal PARADA)                  │
//  │ D13         │ LED indicador aspiradora activa          │
//  └─────────────┴──────────────────────────────────────────┘
//
//  CABLE DE SINCRONÍA (3 hilos UTP):
//    Caja A D5  ──────── Caja B D3
//    Caja B D5  ──────── Caja A D3
//    Caja A GND ──────── Caja B GND
//
// ================================================================

// ── PINES ──────────────────────────────────────────────────────
const int PIN_SCT       = A0;
const int PIN_PULSADOR  = 2;   // INT0 — interrupción externa
const int PIN_SYNC_IN   = 3;   // entrada sincronía ← otra caja
const int PIN_SYNC_OUT  = 5;   // salida sincronía  → otra caja
const int PIN_RELE_ON   = 7;   // relé botón ARRANQUE
const int PIN_RELE_OFF  = 8;   // relé botón PARADA
const int PIN_LED       = 13;  // LED indicador aspiradora

// ── IDENTIFICACIÓN ─────────────────────────────────────────────
//  ★ ÚNICA LÍNEA QUE CAMBIA EN CADA CAJA ★
//  Caja A → MI_ID = 0
//  Caja B → MI_ID = 1
const uint8_t MI_ID = 0;

// ── CALIBRACIÓN SCT-013 10A/1V ─────────────────────────────────
//  10A → 1V RMS → 204.8 counts RMS con ADC 10bit / 5V ref
//  Ajustar si la lectura no coincide con un multímetro:
//    FACTOR_CAL_nuevo = FACTOR_CAL × (A_reales / A_leidos)
const float FACTOR_CAL = 0.04883;

// ── UMBRALES (histéresis) ───────────────────────────────────────
const float UMBRAL_ON  = 0.8;   // A → sierra ENCENDIDA
const float UMBRAL_OFF = 0.4;   // A → sierra APAGADA

// ── TIEMPOS ────────────────────────────────────────────────────
const unsigned long RETARDO_APAGADO_MS = 5000;  // retardo tras apagar sierra
const unsigned long DURACION_PULSO_MS  = 150;   // duración pulso relé
const unsigned long INTERVALO_LOOP_MS  = 100;   // cadencia del loop
const unsigned long DEBOUNCE_MS        = 50;    // antirrebote pulsador

// ── MUESTRAS RMS ───────────────────────────────────────────────
//  200 muestras × 200µs = 40ms → cubre 2 ciclos de 50Hz
const int MUESTRAS         = 200;
const int DELAY_MUESTRA_US = 200;

// ── FILTRO SYNC INPUT ──────────────────────────────────────────
//  Mayoría sobre N lecturas para rechazar spikes de ruido EMI
const int SYNC_LECTURAS = 5;
const int SYNC_UMBRAL   = 3;   // mínimo de HIGH para considerar activa

// ================================================================
//  VARIABLES COMPARTIDAS CON LA INTERRUPCIÓN
//  Deben ser 'volatile' para que el compilador no las optimice
// ================================================================
volatile bool          pulsador_disparado = false;
volatile unsigned long t_ultimo_puls      = 0;

// ── VARIABLES DE ESTADO ─────────────────────────────────────────
bool  sierra_local_ON   = false;
bool  sierra_prev       = false;
bool  remota_prev       = false;  // detecta flanco bajada sierra remota
bool  aspiradora_activa = false;
bool  bloqueo_auto      = false;
int   bias              = 512;    // punto de offset ADC, calibrado en setup()
unsigned long t_apagado     = 0;
unsigned long t_ultimo_loop = 0;

// ================================================================
//  ISR — Interrupción del pulsador (INT0 / D2)
//
//  Reglas: sin delay(), sin Serial, sin cálculos pesados.
//  Solo setea el flag y sale. El procesamiento real está en
//  procesarPulsador(), llamada desde el loop.
// ================================================================
void ISR_pulsador() {
  unsigned long ahora = millis();
  if ((ahora - t_ultimo_puls) > DEBOUNCE_MS) {
    pulsador_disparado = true;
    t_ultimo_puls = ahora;
  }
}

// ================================================================
//  leerSyncIn() — lectura filtrada del pin de sincronía
//  5 muestras separadas 500µs → mayoría → inmune a spikes cortos
// ================================================================
bool leerSyncIn() {
  int count = 0;
  for (int i = 0; i < SYNC_LECTURAS; i++) {
    if (digitalRead(PIN_SYNC_IN) == HIGH) count++;
    delayMicroseconds(500);
  }
  return (count >= SYNC_UMBRAL);
}

// ================================================================
//  leerRMS() — corriente RMS en Amperes
//  Tarda ~40ms (único bloqueo real del sistema)
// ================================================================
float leerRMS() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    int v = analogRead(PIN_SCT) - bias;
    suma += (long)v * v;
    delayMicroseconds(DELAY_MUESTRA_US);
  }
  return sqrt((float)suma / MUESTRAS) * FACTOR_CAL;
}

// ================================================================
//  pulsarRele() — cierra el relé N ms y lo abre
//  Bloqueante 150ms, pero ocurre muy puntualmente
// ================================================================
void pulsarRele(int pin) {
  digitalWrite(pin, HIGH);
  delay(DURACION_PULSO_MS);
  digitalWrite(pin, LOW);
}

// ================================================================
//  encenderAspiradora() / apagarAspiradora()
//  El LED refleja únicamente el estado de la aspiradora.
// ================================================================
void encenderAspiradora() {
  if (!aspiradora_activa) {
    Serial.println(F(">>> PULSO ON  → ASPIRADORA ENCENDIDA"));
    pulsarRele(PIN_RELE_ON);
    aspiradora_activa = true;
    digitalWrite(PIN_LED, HIGH);
  }
}

void apagarAspiradora() {
  if (aspiradora_activa) {
    Serial.println(F(">>> PULSO OFF → ASPIRADORA APAGADA"));
    pulsarRele(PIN_RELE_OFF);
    aspiradora_activa = false;
    digitalWrite(PIN_LED, LOW);
  }
}

// ================================================================
//  procesarPulsador()
//  Llamada desde el loop cuando la ISR dejó el flag en true.
// ================================================================
void procesarPulsador() {
  if (!aspiradora_activa) {
    bloqueo_auto = false;
    encenderAspiradora();
    Serial.println(F("[MANUAL] Encendido por pulsador"));
  } else {
    apagarAspiradora();
    t_apagado = millis();
    if (sierra_local_ON || leerSyncIn()) {
      bloqueo_auto = true;
      Serial.println(F("[MANUAL] Apagado con sierra activa → BLOQUEO AUTO"));
    } else {
      Serial.println(F("[MANUAL] Apagado por pulsador"));
    }
  }
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  pinMode(PIN_RELE_ON,  OUTPUT); digitalWrite(PIN_RELE_ON,  LOW);
  pinMode(PIN_RELE_OFF, OUTPUT); digitalWrite(PIN_RELE_OFF, LOW);
  pinMode(PIN_SYNC_OUT, OUTPUT); digitalWrite(PIN_SYNC_OUT, LOW);
  pinMode(PIN_SYNC_IN,  INPUT);  // pull-down externo 10kΩ a GND
  pinMode(PIN_LED,      OUTPUT); digitalWrite(PIN_LED,      LOW);
  pinMode(PIN_PULSADOR, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_PULSADOR), ISR_pulsador, FALLING);

  Serial.begin(9600);

  // Auto-calibrar el offset DC del SCT-013.
  // La sierra DEBE estar apagada al encender el Arduino.
  long suma_bias = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma_bias += analogRead(PIN_SCT);
    delayMicroseconds(DELAY_MUESTRA_US);
  }
  bias = (int)(suma_bias / MUESTRAS);

  Serial.println(F("================================================"));
  Serial.println(F("  ASPIRADORA AUTOMATICA v4.0"));
  Serial.print  (F("  Caja ID : ")); Serial.println(MI_ID);
  Serial.print  (F("  Bias    : ")); Serial.println(bias);
  Serial.println(F("  Pulsador: D2(INT0) | SyncOut: D5 | SyncIn: D3"));
  Serial.println(F("  Rele ON : D7       | Rele OFF: D8 | SCT: A0"));
  Serial.println(F("================================================"));
  Serial.println(F("I(A) | Local | Remota | Asp | Bloq"));

  delay(500);
}

// ================================================================
//  LOOP — nunca se bloquea salvo durante leerRMS() y pulsarRele()
//
//  Estructura de tiempos por iteración:
//    leerRMS()      → ~40ms  (siempre)
//    leerSyncIn()   →  ~3ms  (siempre, 5×500µs)
//    pulsarRele()   → 150ms  (solo cuando cambia estado aspiradora)
//    resto          →  ~0ms
//    cadencia total → 100ms mínimo entre iteraciones de lógica
// ================================================================
void loop() {

  // ── 1. PULSADOR (vía ISR — prioridad máxima) ─────────────────
  if (pulsador_disparado) {
    noInterrupts();
    pulsador_disparado = false;
    interrupts();
    procesarPulsador();
  }

  // ── 2. CADENCIA NO BLOQUEANTE ─────────────────────────────────
  if ((millis() - t_ultimo_loop) < INTERVALO_LOOP_MS) return;
  t_ultimo_loop = millis();

  // ── 3. LEER CORRIENTE LOCAL ───────────────────────────────────
  float I = leerRMS();

  // ── 4. ACTUALIZAR ESTADO CON HISTÉRESIS ──────────────────────
  sierra_prev = sierra_local_ON;
  if (!sierra_local_ON && I >= UMBRAL_ON)  sierra_local_ON = true;
  if ( sierra_local_ON && I <  UMBRAL_OFF) sierra_local_ON = false;

  // ── 5. PUBLICAR ESTADO LOCAL (sincronía) ─────────────────────
  digitalWrite(PIN_SYNC_OUT, sierra_local_ON ? HIGH : LOW);

  // ── 6. LEER ESTADO REMOTO (con filtro anti-ruido EMI) ─────────
  bool remota_activa = leerSyncIn();

  // ── 7. LIBERAR BLOQUEO ────────────────────────────────────────
  //  Se libera cuando cualquiera de las dos sierras que motivó
  //  el bloqueo acaba de apagarse (flanco bajada local O remota).
  if (bloqueo_auto) {
    bool local_apagada  = sierra_prev && !sierra_local_ON;
    bool remota_apagada = remota_prev && !remota_activa;
    if (local_apagada || remota_apagada) {
      bloqueo_auto = false;
      Serial.println(F("[AUTO] Sierra apagada → bloqueo liberado"));
    }
  }
  remota_prev = remota_activa;

  // ── 8. LÓGICA AUTOMÁTICA ──────────────────────────────────────
  if (sierra_local_ON) {
    t_apagado = millis();
    if (!aspiradora_activa && !bloqueo_auto) {
      encenderAspiradora();
      Serial.println(F("[AUTO] Encendido por sierra local"));
    }

  } else {
    if (aspiradora_activa) {
      if (remota_activa) {
        // La otra sierra sigue activa → mantener aspiradora
        t_apagado = millis();
      } else {
        // Ninguna sierra activa → contar retardo y apagar
        unsigned long transcurrido = millis() - t_apagado;
        if (transcurrido >= RETARDO_APAGADO_MS) {
          apagarAspiradora();
          Serial.println(F("[AUTO] Apagado por retardo"));
        } else {
          Serial.print(F("  [Apagando en "));
          Serial.print((RETARDO_APAGADO_MS - transcurrido) / 1000 + 1);
          Serial.println(F("s...]"));
        }
      }
    }
  }

  // ── 9. MONITOR SERIAL ─────────────────────────────────────────
  Serial.print(I, 2);
  Serial.print(F("A | L:"));
  Serial.print(sierra_local_ON   ? F("ON ") : F("OFF"));
  Serial.print(F(" | R:"));
  Serial.print(remota_activa     ? F("ON ") : F("OFF"));
  Serial.print(F(" | Asp:"));
  Serial.print(aspiradora_activa ? F("ON ") : F("OFF"));
  Serial.print(F(" | Bloq:"));
  Serial.println(bloqueo_auto    ? F("SI") : F("no"));
}

// ================================================================
//  NOTAS SOBRE LA INTERRUPCIÓN
//
//  ¿Por qué la ISR es tan corta?
//  Las ISR interrumpen cualquier cosa en ejecución, incluyendo
//  operaciones críticas. Reglas básicas:
//    - No usar delay() dentro de una ISR
//    - No usar Serial dentro de una ISR
//    - No hacer cálculos pesados
//    - Solo setear un flag y salir
//
//  ¿Qué es 'volatile'?
//  Indica al compilador que la variable puede cambiar en cualquier
//  momento (desde la ISR), forzándolo a leerla siempre desde RAM
//  en lugar de optimizarla en un registro interno.
//
//  ¿Qué es noInterrupts() / interrupts()?
//  Deshabilita/habilita las interrupciones brevemente para leer
//  y limpiar el flag de forma atómica, evitando una race condition
//  entre la ISR y el loop.
//
//  ¿Por qué FALLING y no RISING o CHANGE?
//  El pulsador con INPUT_PULLUP está en HIGH en reposo.
//  Al presionar cae a LOW → el flanco es FALLING.
//  Detecta exactamente el press, no el release.
//
// ================================================================
//
//  DIAGRAMA DE TIEMPOS:
//
//  loop() iteration:
//  │←──────────────── ~143ms total ──────────────────→│
//  │                                                   │
//  │  ISR flag  leerRMS()  leerSyncIn()  lógica  espera│
//  │  <0ms>  │<──40ms──>│<────3ms────>│ <1ms> │<100ms>│
//  │                                                   │
//  ISR puede disparar en CUALQUIER momento.
//
// ================================================================
