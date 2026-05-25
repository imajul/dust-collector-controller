// ================================================================
//  ASPIRADORA AUTOMÁTICA — CAJA DE SIERRA
//  v3.0 — delay no bloqueante + pulsador por interrupción
//
//  Sensor:    YHDC SCT-013  10A / 1V  → A0
//  Relé ON:   D7  → pulso botón ARRANQUE aspiradora
//  Relé OFF:  D8  → pulso botón PARADA aspiradora
//  Pulsador:  D2  → INT0 (interrupción externa)  ← CAMBIO v3
//  Sync out:  D5  → D3 de la otra caja           ← CAMBIO v3
//  Sync in:   D3  → D5 de la otra caja (+ pull-down 10kΩ a GND)
//  LED:       D13 → indicador sierra activa
//
//  MISMO CÓDIGO EN LAS DOS CAJAS.
//  Solo cambiás MI_ID (0 para Caja A, 1 para Caja B).
//
//  ── CAMBIOS RESPECTO A v2 ───────────────────────────────────
//  - Pulsador en D2 (INT0): responde instantáneamente mediante
//    interrupción, sin depender del ciclo del loop.
//  - Sincronía salida movida a D5 (D2 ahora es del pulsador).
//  - delay(100) al final del loop reemplazado por control de
//    tiempo no bloqueante con millis(). El loop nunca se bloquea.
//  - leerRMS() sigue siendo bloqueante 40ms (necesario para
//    muestrear 2 ciclos de 50Hz), pero es el único bloqueo real.
//
//  ── PINES REQUERIDOS ────────────────────────────────────────
//  ┌─────────────┬──────────────────────────────────────────┐
//  │ Pin         │ Función                                  │
//  ├─────────────┼──────────────────────────────────────────┤
//  │ A0          │ Señal SCT-013 (via bias)                 │
//  │ D2 (INT0)   │ Pulsador manual NO (entre D2 y GND)      │
//  │ D3          │ Entrada sincronía ← D5 otra caja         │
//  │ D5          │ Salida sincronía  → D3 otra caja         │
//  │ D7          │ Relé ON (canal ARRANQUE)                 │
//  │ D8          │ Relé OFF (canal PARADA)                  │
//  │ D13         │ LED indicador (onboard)                  │
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
const int PIN_LED       = 13;  // LED indicador

// ── IDENTIFICACIÓN ─────────────────────────────────────────────
//  ★ ÚNICA LÍNEA QUE CAMBIA EN CADA CAJA ★
//  Caja A → MI_ID = 0
//  Caja B → MI_ID = 1
const uint8_t MI_ID = 0;

// ── CALIBRACIÓN SCT-013 10A/1V ─────────────────────────────────
//  10A → 1V RMS → 204.8 counts RMS con ADC 10bit / 5V ref
//  Ajustar si lectura no coincide con multímetro:
//    FACTOR_CAL_nuevo = FACTOR_CAL × (A_reales / A_leidos)
const float FACTOR_CAL = 0.04883;

// ── UMBRALES (histéresis) ───────────────────────────────────────
const float UMBRAL_ON  = 0.8;   // A → sierra ENCENDIDA
const float UMBRAL_OFF = 0.4;   // A → sierra APAGADA

// ── TIEMPOS ────────────────────────────────────────────────────
const unsigned long RETARDO_APAGADO_MS = 5000;  // retardo tras apagar sierra
const unsigned long DURACION_PULSO_MS  = 150;   // duración pulso relé
const unsigned long INTERVALO_LOOP_MS  = 100;   // cadencia del loop (no bloqueante)
const unsigned long DEBOUNCE_MS        = 50;    // antirrebote pulsador (interrupción)

// ── MUESTRAS RMS ───────────────────────────────────────────────
//  200 muestras × 200µs = 40ms → cubre 2 ciclos de 50Hz
const int  MUESTRAS         = 200;
const int  DELAY_MUESTRA_US = 200;

// ================================================================
//  VARIABLES COMPARTIDAS CON LA INTERRUPCIÓN
//  Deben ser 'volatile' para que el compilador no las optimice
// ================================================================
volatile bool    pulsador_disparado = false; // flag seteado por la ISR
volatile unsigned long t_ultimo_puls = 0;    // timestamp último flanco (debounce)

// ── VARIABLES DE ESTADO ─────────────────────────────────────────
bool  sierra_local_ON   = false;
bool  sierra_prev       = false;
bool  aspiradora_activa = false;
bool  bloqueo_auto      = false;
unsigned long t_apagado = 0;
unsigned long t_ultimo_loop = 0;  // control cadencia no bloqueante

// ================================================================
//  ISR — Interrupción del pulsador (INT0 / D2)
//
//  Se ejecuta automáticamente en el flanco de BAJADA de D2
//  (cuando se presiona el pulsador, D2 pasa de HIGH a LOW).
//  La ISR es lo más corta posible: solo registra el evento.
//  El procesamiento real ocurre en el loop.
// ================================================================
void ISR_pulsador() {
  unsigned long ahora = millis();

  // Antirrebote: ignorar flancos demasiado seguidos
  if ((ahora - t_ultimo_puls) > DEBOUNCE_MS) {
    pulsador_disparado = true;
    t_ultimo_puls = ahora;
  }
}

// ================================================================
//  leerRMS() — corriente RMS en Amperes
//  Tarda ~40ms (único bloqueo real del sistema)
// ================================================================
float leerRMS() {
  long suma = 0;
  const int bias = 512;  // 2.5V → 512 con ADC 10bit / 5V ref

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
//  Aquí se hace todo el procesamiento — nunca en la ISR.
// ================================================================
void procesarPulsador() {
  if (!aspiradora_activa) {
    // Estaba apagada → encender y liberar bloqueo
    bloqueo_auto = false;
    encenderAspiradora();
    Serial.println(F("[MANUAL] Encendido por pulsador"));
  } else {
    // Estaba encendida → apagar
    apagarAspiradora();
    t_apagado = millis();
    if (sierra_local_ON || (digitalRead(PIN_SYNC_IN) == HIGH)) {
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
  pinMode(PIN_PULSADOR, INPUT_PULLUP);  // pullup interno: reposo=HIGH

  // Registrar la ISR en INT0 (D2), flanco de bajada
  // FALLING = HIGH→LOW = momento en que se presiona el pulsador
  attachInterrupt(digitalPinToInterrupt(PIN_PULSADOR), ISR_pulsador, FALLING);

  Serial.begin(9600);
  Serial.println(F("================================================"));
  Serial.println(F("  ASPIRADORA AUTOMÁTICA v3.0"));
  Serial.println(F("  delay no bloqueante + ISR pulsador"));
  Serial.print  (F("  Caja ID: ")); Serial.println(MI_ID);
  Serial.println(F("  Pulsador: D2(INT0) | SyncOut: D5 | SyncIn: D3"));
  Serial.println(F("  Relé ON: D7 | Relé OFF: D8 | SCT: A0"));
  Serial.println(F("================================================"));
  Serial.println(F("I(A) | Local | Remota | Asp | Bloq"));

  delay(500);
}

// ================================================================
//  LOOP — nunca se bloquea salvo durante leerRMS() y pulsarRele()
//
//  Estructura de tiempos por iteración:
//    leerRMS()      → ~40ms  (siempre)
//    pulsarRele()   → 150ms  (solo cuando cambia estado aspiradora)
//    resto          → ~0ms
//    cadencia total → 100ms mínimo entre iteraciones de lógica
// ================================================================
void loop() {

  // ── 1. PULSADOR (vía ISR — prioridad máxima) ─────────────────
  //  La ISR setea el flag en cualquier momento.
  //  Aquí lo leemos y lo limpiamos atómicamente.
  if (pulsador_disparado) {
    // Leer y limpiar el flag de forma atómica
    // (deshabilitar interrupciones brevemente para evitar race condition)
    noInterrupts();
    pulsador_disparado = false;
    interrupts();

    procesarPulsador();
  }

  // ── 2. CADENCIA NO BLOQUEANTE ─────────────────────────────────
  //  El resto del loop solo se ejecuta cada INTERVALO_LOOP_MS.
  //  El pulsador (arriba) se procesa siempre, sin esperar.
  if ((millis() - t_ultimo_loop) < INTERVALO_LOOP_MS) return;
  t_ultimo_loop = millis();

  // ── 3. LEER CORRIENTE LOCAL ───────────────────────────────────
  float I = leerRMS();  // ~40ms de muestreo

  // ── 4. ACTUALIZAR ESTADO CON HISTÉRESIS ──────────────────────
  sierra_prev = sierra_local_ON;
  if (!sierra_local_ON && I >= UMBRAL_ON)  sierra_local_ON = true;
  if ( sierra_local_ON && I <  UMBRAL_OFF) sierra_local_ON = false;

  // ── 5. LIBERAR BLOQUEO SI SIERRA SE APAGÓ ────────────────────
  if (sierra_prev && !sierra_local_ON && bloqueo_auto) {
    bloqueo_auto = false;
    Serial.println(F("[AUTO] Sierra apagada → bloqueo liberado"));
  }

  // ── 6. PUBLICAR ESTADO LOCAL (sincronía) ─────────────────────
  digitalWrite(PIN_SYNC_OUT, sierra_local_ON ? HIGH : LOW);
  digitalWrite(PIN_LED,      sierra_local_ON ? HIGH : LOW);

  // ── 7. LEER ESTADO REMOTO ─────────────────────────────────────
  bool remota_activa = (digitalRead(PIN_SYNC_IN) == HIGH);

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
//  Las ISR interrumpen cualquier cosa que esté ejecutando el
//  procesador, incluyendo otras operaciones críticas. Por eso
//  deben ser lo más rápidas posible. Reglas básicas:
//    - No usar delay() dentro de una ISR
//    - No usar Serial dentro de una ISR
//    - No hacer cálculos pesados
//    - Solo setear un flag y salir
//
//  ¿Qué es 'volatile'?
//  Indica al compilador que la variable puede cambiar en cualquier
//  momento (desde la ISR), y que no debe optimizarla guardándola
//  en un registro interno. Sin volatile, el loop podría leer un
//  valor desactualizado sin saberlo.
//
//  ¿Qué es noInterrupts() / interrupts()?
//  Deshabilita/habilita las interrupciones brevemente para leer
//  y limpiar el flag de forma atómica (sin que la ISR pueda
//  interrumpir justo en medio de la lectura).
//
//  ¿Por qué FALLING y no RISING o CHANGE?
//  El pulsador con INPUT_PULLUP está en HIGH en reposo.
//  Al presionar, cae a LOW → el flanco es FALLING.
//  Esto detecta exactamente el momento del press, no del release.
//
// ================================================================
//
//  DIAGRAMA DE TIEMPOS:
//
//  loop() iteration:
//  │←────────────── ~140ms total ──────────────────→│
//  │                                                 │
//  │  check ISR flag  leerRMS()   lógica  espera    │
//  │  ←0ms→│←──────40ms──────→│←~1ms→│←~100ms→│   │
//  │                                                 │
//  ISR puede disparar en CUALQUIER momento:
//  │  ───────────────────↑──────────────────────     │
//  │                     └── procesarPulsador()      │
//  │                         se llama en próximo     │
//  │                         chequeo del flag (~0ms) │
//
// ================================================================
//
//  RESUMEN DE CAMBIOS DE PINES respecto a v2:
//
//  v2           v3          Motivo
//  ──────────   ──────────  ──────────────────────────────
//  D2 sync_out  D5 sync_out D2 necesario para INT0
//  D3 sync_in   D3 sync_in  sin cambio
//  D9 pulsador  D2 pulsador INT0 solo disponible en D2/D3
//
// ================================================================
