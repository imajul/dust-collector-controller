# Diagrama de conexiones

## Componentes por caja

| Componente | Modelo / Valor |
|---|---|
| Microcontrolador | Arduino Uno / Nano |
| Sensor de corriente | YHDC SCT-013 10A/1V |
| Módulo relé | 2 canales, 5V, activo en HIGH |
| Pulsador | NO (normalmente abierto) |
| Resistencia pull-down | 10kΩ (sync in) |
| Resistencia bias SCT | 2× 10kΩ (divisor a 2.5V) + 100Ω en serie |

---

## Mapa de pines

| Pin Arduino | Función | Conectar a |
|---|---|---|
| `A0` | Señal SCT-013 | Salida divisor de bias |
| `D2` (INT0) | Pulsador manual | Pulsador NO → GND |
| `D3` | Sync IN | D5 de la otra caja |
| `D5` | Sync OUT | D3 de la otra caja |
| `D7` | Relé ARRANQUE | Canal IN1 del módulo relé |
| `D8` | Relé PARADA | Canal IN2 del módulo relé |
| `D13` | LED indicador | Integrado en placa |
| `GND` | Referencia común | GND otra caja + relé + sensor |
| `5V` | Alimentación | Módulo relé VCC |

---

## Circuito de acondicionamiento SCT-013

El sensor SCT-013 10A/1V produce una tensión AC centrada en 0V.
Para poder leerla con el ADC del Arduino (0–5V) se necesita un divisor
de tensión que eleve el punto de referencia a 2.5V (bias).

```
       5V ──┬── 10kΩ (R1) ──┬──── A0
            │               │
           10kΩ (R2)      SCT-013
            │               │ (secundario)
           GND ─────────────┴──── GND

           (nodo central) ──┤ 10µF (C1) ┤── GND
```

- El divisor 10kΩ/10kΩ establece 2.5V en el nodo central.
- La resistencia de 100Ω en serie protege el pin ADC.
- Agregar un capacitor de 10µF entre el nodo central y GND
  mejora la estabilidad del bias.
- El firmware auto-calibra el valor exacto de bias en cada arranque.

---

## Cable de sincronía entre cajas

Conectar con cable UTP (3 hilos). Mantener el cable alejado de los
cables de alimentación de los motores para minimizar interferencias EMI.

```
┌─────────────────┐    UTP Cat5    ┌─────────────────┐
│     CAJA A      │                │     CAJA B      │
│                 │                │                 │
│  D5 (sync out) ─┼────────────────┼─ D3 (sync in)   │
│  D3 (sync in)  ─┼────────────────┼─ D5 (sync out)  │
│  GND           ─┼────────────────┼─ GND            │
│                 │                │                 │
└─────────────────┘                └─────────────────┘
```

**Pull-down**: colocar una resistencia de 10kΩ entre `D3` y `GND`
en cada caja. Garantiza lectura LOW cuando la otra caja está apagada
o desconectada, evitando encendidos falsos.

---

## Conexión de los relés a la aspiradora

El sistema usa dos relés para simular el pulsado de los botones físicos
de la aspiradora (ARRANQUE y PARADA). Los contactos del relé van en
paralelo con los botones originales de la aspiradora.

```
Relé D7 (canal 1) → contactos en paralelo con botón ARRANQUE
Relé D8 (canal 2) → contactos en paralelo con botón PARADA
```

Usar los contactos **NA (normalmente abierto)** del relé.
Al activar el relé, se cierra el circuito 150ms (pulso) y se suelta,
igual que una pulsación manual.

> **Nota de seguridad**: trabajar con la aspiradora desenchufada
> de la red al momento de conectar los relés. Los contactos de los
> relés van al circuito de control de la aspiradora (baja tensión),
> no a la línea de alimentación del motor.

---

## Configuración: Caja A vs Caja B

El firmware es idéntico en ambas cajas. La única diferencia es
la constante `MI_ID` al principio del archivo `.ino`:

```cpp
// Caja A:
const uint8_t MI_ID = 0;

// Caja B:
const uint8_t MI_ID = 1;
```

`MI_ID` actualmente se usa para identificación en el monitor serial.
La lógica de sincronía es completamente simétrica.
