# Controlador de Aspiradora Automática para Caja de Sierra

Firmware para Arduino que controla automáticamente una aspiradora/colector de polvo
en función del consumo eléctrico de una sierra. Detecta si la sierra está encendida
midiendo corriente con un sensor SCT-013, y activa/desactiva la aspiradora mediante
pulsos de relé sobre sus botones originales.

Soporta **dos cajas simultáneas** (dos sierras, una aspiradora compartida): las cajas
se sincronizan entre sí y mantienen la aspiradora activa mientras cualquiera de las
dos sierras esté en uso.

---

## Características

- Detección de corriente sin contacto (sensor SCT-013 en pinza)
- Histéresis de umbral para evitar falsos encendidos/apagados
- Retardo configurable antes de apagar la aspiradora
- Pulsador manual con respuesta por interrupción (INT0)
- Bloqueo manual: apagar la aspiradora mientras la sierra está activa
  evita que la automatización la vuelva a encender hasta que la sierra se detenga
- Sincronía entre dos cajas por cable UTP (3 hilos)
- Loop no bloqueante basado en `millis()`
- Auto-calibración del offset del sensor en cada arranque
- Filtro de mayoría en la línea de sincronía para rechazo de ruido EMI
- Monitor serial en tiempo real

---

## Estructura del repositorio

```
dust-collector-controller/
├── firmware/
│   └── aspiradora/
│       └── aspiradora.ino       <- sketch principal (abrir con Arduino IDE)
├── docs/
│   ├── wiring.md                <- diagrama de conexiones y notas de hardware
│   └── wiring_diagram.html      <- diagrama interactivo (abrir en navegador)
└── README.md
```

---

## Hardware requerido (por caja)

| Componente | Descripción |
|---|---|
| Arduino Uno / Nano | Microcontrolador |
| YHDC SCT-013 10A/1V | Sensor de corriente en pinza |
| Módulo relé 2 canales 5V | Controla botones ARRANQUE y PARADA |
| Pulsador NO | Control manual |
| 2× resistencia 10kΩ | Divisor de bias para SCT-013 (R1 y R2) |
| 1× capacitor 10µF 16V | Estabilización del punto de bias (C1) |
| 1× resistencia 10kΩ | Pull-down en sync input (D3) |
| Cable UTP (3 hilos) | Conexión entre las dos cajas |

---

## Mapa de pines

| Pin | Función |
|---|---|
| `A0` | Señal SCT-013 (via divisor de bias) |
| `D2` (INT0) | Pulsador manual NO (entre D2 y GND) |
| `D3` | Entrada sincronía ← D5 de la otra caja |
| `D5` | Salida sincronía → D3 de la otra caja |
| `D7` | Relé canal ARRANQUE aspiradora |
| `D8` | Relé canal PARADA aspiradora |
| `D13` | LED indicador (encendido = aspiradora activa) |

Ver [`docs/wiring_diagram.html`](docs/wiring_diagram.html) para el diagrama interactivo completo,
o [`docs/wiring.md`](docs/wiring.md) para la versión en texto.

---

## Conexionado detallado

### 1. Alimentación

```
Red 220V
  └─[Fusible 1A / 250V]─ L ─┐
                             ├─ Fuente Switching 12V ─ Jack DC Arduino
  └─────────────────── N ───┘        │
  └─────────────────── PE ── caja    └─ regulador interno → Pin 5V Arduino
```

El pin `5V` del Arduino alimenta el módulo relé y el divisor de bias del SCT-013.

---

### 2. Sensor de corriente SCT-013

El sensor produce una señal AC centrada en 0V. El circuito bias la desplaza a 2.5V
para que el ADC del Arduino (0–5V) pueda leerla correctamente.

```
                        ┌──[R1 10kΩ]── 5V Arduino
                        │
Jack 3.5mm TIP ─────────┼──────────────────────────── A0 Arduino
(señal SCT)             │
                        ├──[C1 10µF 16V]── GND   (estabiliza el bias)
                        │
                        └──[R2 10kΩ]── GND Arduino

Jack 3.5mm SLEEVE ──────────────────────────────────── GND Arduino
```

> La pinza SCT-013 va sobre **un solo conductor** del cable de alimentación de la sierra.
> El bias se auto-calibra al arrancar (sierra apagada). Valor esperado: ~512 (2.5V).

---

### 3. Cable de sincronía entre cajas

Permite que la aspiradora permanezca activa mientras cualquiera de las dos sierras esté en uso.
Usar cable UTP Cat5 (3 hilos). Mantenerlo alejado de los cables de alimentación de los motores.

```
┌──────────────────┐   UTP Cat5   ┌──────────────────┐
│      CAJA A      │              │      CAJA B      │
│                  │              │                  │
│  D5 (sync out) ──┼──────────────┼── D3 (sync in)   │
│  D3 (sync in)  ──┼──────────────┼── D5 (sync out)  │
│  GND           ──┼──────────────┼── GND            │
│                  │              │                  │
└──────────────────┘              └──────────────────┘
```

> Resistencia pull-down de **10kΩ entre D3 y GND** en cada Arduino.
> Garantiza lectura LOW cuando la otra caja está apagada o desconectada.

---

### 4. Módulo relé → Aspiradora

Los relés simulan el pulsado de los botones físicos de la aspiradora (150ms).
Conectar los contactos NA (normalmente abierto) **en paralelo** con cada botón.

```
Arduino D7 ── IN1 ──┐
Arduino D8 ── IN2 ──┤  Módulo Relé 2CH   COM1 + NO1 ── Botón ARRANQUE asp.
Arduino  5V ─ VCC ──┤  (optoacoplador)   COM2 + NO2 ── Botón PARADA asp.
Arduino GND ─ GND ──┘
```

> Medir con multímetro la tensión en los terminales del botón antes de conectar.
> Si hay baja tensión (12–24V): usar el módulo como está.
> Si hay 220V: usar relé industrial 220V/10A y cable 2×1.5mm².

---

### 5. Pulsador manual

```
D2 Arduino ── Terminal 1 ──[Pulsador NO]── Terminal 2 ── GND Arduino
```

El código activa `INPUT_PULLUP` internamente: no se necesita resistencia externa.
D2 es el único pin con INT0 en el Arduino Uno, lo que permite detectar el
flanco de bajada por interrupción hardware (respuesta instantánea).

---

## Cómo cargar el firmware

1. Abrir `firmware/aspiradora/aspiradora.ino` con **Arduino IDE 1.8+** o **Arduino IDE 2**
2. Editar la constante `MI_ID`:
   ```cpp
   const uint8_t MI_ID = 0;  // Caja A
   const uint8_t MI_ID = 1;  // Caja B
   ```
3. Seleccionar placa: **Arduino Uno** (o **Nano** según corresponda)
4. Seleccionar puerto COM
5. Verificar y cargar

El firmware es idéntico en ambas cajas; solo cambia `MI_ID`.

---

## Calibración

### Factor de corriente (`FACTOR_CAL`)

El valor por defecto (`0.04883`) está calculado para el sensor SCT-013 10A/1V
con ADC de 10 bits y referencia de 5V. Si la lectura no coincide con un
multímetro de gancho:

```
FACTOR_CAL_nuevo = FACTOR_CAL_actual × (A_reales / A_leidos)
```

### Umbrales de histéresis

```cpp
const float UMBRAL_ON  = 0.8;  // A → sierra ENCENDIDA
const float UMBRAL_OFF = 0.4;  // A → sierra APAGADA
```

Ajustar según el consumo en vacío de la sierra. Usar el monitor serial
(9600 baud) para ver la corriente en tiempo real mientras se enciende
y apaga la sierra sin carga.

### Bias (offset ADC)

El bias se auto-calibra en cada arranque muestreando el sensor con la
sierra apagada. No requiere ajuste manual. El valor calibrado se imprime
en el monitor serial al arrancar.

---

## Lógica de funcionamiento

```
Sierra local encendida (I ≥ 0.8A)
  └── Si aspiradora apagada y sin bloqueo → ENCENDER aspiradora

Sierra local apagada (I < 0.4A)
  ├── Sierra remota activa → mantener aspiradora, reiniciar temporizador
  └── Sierra remota apagada → contar 5s y APAGAR aspiradora

Pulsador (manual)
  ├── Aspiradora apagada → ENCENDER, liberar bloqueo
  └── Aspiradora encendida → APAGAR
        └── Sierra activa (local o remota) → activar BLOQUEO AUTO
              └── Se libera automáticamente cuando esa sierra se apaga
```

### Monitor serial (9600 baud)

```
I(A) | Local | Remota | Asp | Bloq
0.00A | L:OFF | R:OFF  | Asp:OFF | Bloq:no
3.42A | L:ON  | R:OFF  | Asp:ON  | Bloq:no
```

---

## Changelog

### v4.0
- **Bug fix**: `bloqueo_auto` ahora se libera también cuando la sierra
  **remota** se apaga. Antes, si solo la sierra remota estaba activa al
  pulsar el botón, el bloqueo quedaba atascado indefinidamente.
- **Bug fix**: LED unificado bajo un solo dueño (`aspiradora_activa`).
  Antes, el LED tenía dos fuentes en conflicto y se apagaba aunque la
  aspiradora siguiera activa por sierra remota.
- **Mejora**: bias del SCT-013 auto-calibrado en `setup()`. Elimina el
  error de offset por tolerancias del divisor de tensión.
- **Mejora**: sync input con filtro de mayoría (5 lecturas, umbral 3)
  para rechazar spikes de ruido EMI generados por los motores de sierra.

### v3.0
- Pulsador movido a D2 (INT0): respuesta instantánea por interrupción
- Sync OUT movida de D2 a D5
- Loop no bloqueante con `millis()` (reemplaza `delay(100)` al final)

### v2.0
- Soporte sincronía entre dos cajas
- Pulsador manual con bloqueo automático

### v1.0
- Control automático básico por umbral de corriente
