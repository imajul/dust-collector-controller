// ================================================================
//  TEST DE HARDWARE — SCT-013 + Relé ON/OFF
//
//  Propósito: verificar sensor de corriente y relés por separado
//  antes de cargar el firmware de producción.
//
//  Conexiones:
//    A0  → nodo central circuito bias SCT-013
//    D7  → IN1 relé ARRANQUE (ON)
//    D8  → IN2 relé PARADA   (OFF)
//
//  Comandos por Monitor Serial (9600 baud, "Nueva línea"):
//    o  → pulsa relé ON  (150ms)
//    f  → pulsa relé OFF (150ms)
//    cualquier tecla → muestra lectura de corriente
// ================================================================

const int PIN_SCT      = A0;
const int PIN_RELE_ON  = 7;
const int PIN_RELE_OFF = 8;

const int   MUESTRAS         = 200;
const int   DELAY_MUESTRA_US = 200;
const float FACTOR_CAL       = 0.04883;
const int   DURACION_PULSO   = 150;

int bias = 512;

// ── leerRMS ────────────────────────────────────────────────────
float leerRMS() {
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    int v = analogRead(PIN_SCT) - bias;
    suma += (long)v * v;
    delayMicroseconds(DELAY_MUESTRA_US);
  }
  return sqrt((float)suma / MUESTRAS) * FACTOR_CAL;
}

// ── pulsarRele ─────────────────────────────────────────────────
void pulsarRele(int pin, const char* nombre) {
  Serial.print(F(">>> Pulso "));
  Serial.print(nombre);
  Serial.print(F(" ... "));
  digitalWrite(pin, LOW);   // activo en LOW: cierra el relé
  delay(DURACION_PULSO);
  digitalWrite(pin, HIGH);  // reposo en HIGH: abre el relé
  Serial.println(F("OK"));
}

// ── setup ──────────────────────────────────────────────────────
void setup() {
  pinMode(PIN_RELE_ON,  OUTPUT); digitalWrite(PIN_RELE_ON,  HIGH);  // HIGH = reposo (relé abierto)
  pinMode(PIN_RELE_OFF, OUTPUT); digitalWrite(PIN_RELE_OFF, HIGH);

  Serial.begin(9600);
  Serial.println(F("================================================"));
  Serial.println(F("  TEST DE HARDWARE  v1.0"));
  Serial.println(F("  SCT-013 (A0) | Relé ON (D7) | Relé OFF (D8)"));
  Serial.println(F("================================================"));

  // Calibrar bias (sierra debe estar apagada)
  Serial.print(F("Calibrando bias... "));
  long suma = 0;
  for (int i = 0; i < MUESTRAS; i++) {
    suma += analogRead(PIN_SCT);
    delayMicroseconds(DELAY_MUESTRA_US);
  }
  bias = (int)(suma / MUESTRAS);
  Serial.print(F("bias = "));
  Serial.print(bias);
  Serial.println(F("  (esperado: ~512)"));

  Serial.println();
  Serial.println(F("Comandos: [o]=Rele ON  [f]=Rele OFF  [Enter]=Leer corriente"));
  Serial.println();
}

// ── loop ───────────────────────────────────────────────────────
void loop() {

  // Leer corriente y mostrar continuamente cada ~500ms
  static unsigned long t_anterior = 0;
  if (millis() - t_anterior >= 500) {
    t_anterior = millis();
    float I = leerRMS();
    Serial.print(F("I = "));
    Serial.print(I, 3);
    Serial.print(F(" A"));
    if (I >= 0.8) Serial.print(F("  *** SIERRA ENCENDIDA ***"));
    Serial.println();
  }

  // Comandos por serial
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'o' || c == 'O') pulsarRele(PIN_RELE_ON,  "RELE ON  (ARRANQUE)");
    if (c == 'f' || c == 'F') pulsarRele(PIN_RELE_OFF, "RELE OFF (PARADA)  ");
  }
}
