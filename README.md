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
│       └── aspiradora.ino   <- sketch principal (abrir con Arduino IDE)
├── docs/
│   └── wiring.md            <- diagrama de conexiones y notas de hardware
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
| 2× resistencia 10kΩ | Divisor de bias para SCT-013 |
| 1× resistencia 100Ω | Protección pin ADC |
| 1× resistencia 10kΩ | Pull-down en sync input |
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

Ver [`docs/wiring.md`](docs/wiring.md) para el circuito de acondicionamiento
del sensor y el diagrama de conexión entre cajas.

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
