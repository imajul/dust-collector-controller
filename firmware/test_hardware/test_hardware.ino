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
//  Comportamiento automático:
//    I ≥ 0.8A → pulsa relé ON
//    I < 0.4A → espera 3s → pulsa relé OFF
//
//  Comandos por Monitor Serial (9600 baud):
//    o  → fuerza pulso relé ON
//    f  → fuerza pulso relé OFF
// ================================================================

const int PIN_SCT      = A0;
const int PIN_RELE_ON  = 7;
const int PIN_RELE_OFF = 8;

const int   MUESTRAS         = 200;
const int   DELAY_MUESTRA_US = 200;
const float FACTOR_CAL       = 0.04883;
const int   DURACION_PULSO   = 150;

const float UMBRAL_ON        = 0.8;    // A → sierra encendida
const float UMBRAL_OFF       = 0.4;    // A → sierra apagada
const unsigned long RETARDO  = 3000;   // ms antes de apagar (reducido para test)

int  bias            = 512;
bool aspiradora_ON   = false;
bool sierra_ON       = false;
unsigned long t_apagado = 0;

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

void encender() {
  if (!aspiradora_ON) {
    pulsarRele(PIN_RELE_ON, "RELE ON  (ARRANQUE)");
    aspiradora_ON = true;
  }
}

void apagar() {
  if (aspiradora_ON) {
    pulsarRele(PIN_RELE_OFF, "RELE OFF (PARADA)  ");
    aspiradora_ON = false;
  }
}

// ── setup ──────────────────────────────────────────────────────
void setup() {
  pinMode(PIN_RELE_ON,  OUTPUT); digitalWrite(PIN_RELE_ON,  HIGH);
  pinMode(PIN_RELE_OFF, OUTPUT); digitalWrite(PIN_RELE_OFF, HIGH);

  Serial.begin(9600);
  Serial.println(F("================================================"));
  Serial.println(F("  TEST DE HARDWARE  v1.1"));
  Serial.println(F("  SCT-013 (A0) | Relé ON (D7) | Relé OFF (D8)"));
  Serial.println(F("================================================"));

  Serial.print(F("Calibrando bias (sierra apagada)... "));
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
  Serial.println(F("AUTO: ON si I>=0.8A | OFF si I<0.4A por 3s"));
  Serial.println(F("MANUAL: [o]=ON  [f]=OFF"));
  Serial.println(F("------------------------------------------------"));
  Serial.println(F("I(A)    | Sierra | Aspiradora | Estado"));
}

// ── loop ───────────────────────────────────────────────────────
void loop() {

  // ── Leer corriente cada 500ms ─────────────────────────────
  static unsigned long t_anterior = 0;
  if (millis() - t_anterior >= 500) {
    t_anterior = millis();

    float I = leerRMS();

    // Histéresis detección sierra
    if (!sierra_ON && I >= UMBRAL_ON)  sierra_ON = true;
    if ( sierra_ON && I <  UMBRAL_OFF) sierra_ON = false;

    // Lógica automática
    if (sierra_ON) {
      t_apagado = millis();
      encender();
    } else {
      if (aspiradora_ON) {
        unsigned long transcurrido = millis() - t_apagado;
        if (transcurrido >= RETARDO) {
          apagar();
        }
      }
    }

    // Monitor serial
    Serial.print(I, 3);
    Serial.print(F("A | "));
    Serial.print(sierra_ON      ? F("ON  ") : F("OFF "));
    Serial.print(F(" | "));
    Serial.print(aspiradora_ON  ? F("ON        ") : F("OFF       "));
    Serial.print(F(" | "));
    if (sierra_ON) {
      Serial.println(F("sierra detectada"));
    } else if (aspiradora_ON) {
      unsigned long resta = RETARDO - (millis() - t_apagado);
      Serial.print(F("apagando en "));
      Serial.print(resta / 1000 + 1);
      Serial.println(F("s..."));
    } else {
      Serial.println(F("en reposo"));
    }
  }

  // ── Comandos manuales ─────────────────────────────────────
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'o' || c == 'O') { aspiradora_ON = false; encender(); }
    if (c == 'f' || c == 'F') { aspiradora_ON = true;  apagar();  }
  }
}
