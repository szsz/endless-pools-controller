# SwimMachine V5 – Wiring & Power Guide

Hardware: `SwimMachineV5 final r3` (EasyEDA Pro, 2026-11-10) · ESP32-S3FH4R2 + W5500 · 65 × 55 mm

## Board overview

One 8-pin header (H1) carries both Ethernet and 12–24 V DC power. The LED panel gets 5 V from H2 and data from J1.

![SwimMachine V5 PCB layout: H1 top right, J1 centre, H2 bottom right, H3 left, antenna top left](images/swimmachine-v5-pcb.webp)

*PCB layout from the EasyEDA project, top view.*

| Connector | Where on the board | What it is for |
| --- | --- | --- |
| H1 – ETH / DC | Top right, 2 × 4 pins | Ethernet data + 12–24 V DC input, from one cable |
| H2 – +5V / GND | Bottom right, screw terminal | 5 V output to the LED panel, 4 A max |
| J1 – HUB75 3V3 | Centre, 2 × 8 socket | Data to the HUB75 LED panel |
| H3 – TC2030 | Left, pads (no connector fitted) | 3.3 V UART for flashing and logs |
| Antenna | Top left corner | 2.4 GHz Wi-Fi PCB antenna — keep metal and wires away |

## Ethernet + power cable (H1)

Wire the cable's far end as a standard **T568B** RJ45 plug. At the board end each of the 8 wires goes to its own H1 pin as below. Orange and green carry data, blue carries +12–24 V, and brown is ground. This is the same pair use as a common passive-PoE injector (+ on RJ45 pins 4/5, − on 7/8).

| H1 pin | Signal | Wire colour (T568B) | RJ45 pin at far end |
| --- | --- | --- | --- |
| 1 | +12–24 V | Blue | 4 |
| 2 | +12–24 V | White/blue | 5 |
| 3 | TX+ | White/orange | 1 |
| 4 | TX− | Orange | 2 |
| 5 | RX+ | White/green | 3 |
| 6 | RX− | Green | 6 |
| 7 | GND | White/brown | 7 |
| 8 | GND | Brown | 8 |

- **Pin 1** is the square pad, bottom-left of the H1 block. Odd pins (1, 3, 5, 7) are the row nearer the board centre; even pins (2, 4, 6, 8) are the row at the board edge. The silkscreen reads +12–24V · TX+ TX− · RX+ RX− · GND GND.
- Keep each pair twisted right up to the header, so white/orange stays with orange and white/green with green.
- The board acts like a PC: it transmits on orange and receives on green. Plug the far end into a normal switch port or into the LAN/PoE port of a passive injector.
- If the far end is crimped **T568A** instead, the orange and green pairs swap: white/green goes to H1-3, green to H1-4, white/orange to H1-5 and orange to H1-6. Blue and brown stay the same.

## Power setup

Use an isolated, current-limited **24 V DC** supply of at least **36 W (1.5 A)**, fed into the cable's blue (+) and brown (−) pairs with a passive injector. H1 is **not IEEE 802.3af/at PoE**: a PoE switch will not power it, and the board has no PoE detection.

| Item | Value |
| --- | --- |
| Input voltage | 12–24 V DC; 24 V recommended |
| Absolute limit | Keep the supply at or below 24 V nominal: the SMBJ24A surge diode starts clamping at about 26.7 V |
| Input protection | 4 A fuse (F1), reverse-polarity MOSFET (Q1), surge diode (D2) |
| 5 V rail (AP64502) | Feeds the LED panel through H2: 4 A / 20 W max |
| 3.3 V rail (AP63203, from 5 V) | ESP32 + W5500: 1 A budget; 5 V panel + 3.3 V together must stay under 5 A |
| Input draw at full load | About 28 W: roughly 1.2 A at 24 V, or 2.4 A at 12 V |

**Why 24 V rather than 12 V:** the same power at half the current means a quarter of the voltage loss in the cable. At 12 V a full panel pulls about 2.4 A through four thin conductors, which is more than Ethernet cable is meant to carry and drops noticeably over a long run.

**Wiring the supply:** injector DC+ → blue pair (H1 pins 1 and 2), injector DC− → brown pair (H1 pins 7 and 8). The injector's data port goes to your switch. A reversed supply is blocked by Q1 and won't damage the board, but the board stays off.

**LED panel power:** run the panel's 5 V cable to the H2 screw terminal (+5V on the left, GND on the right as printed). Don't power the panel from a second supply at the same time.

## Other connectors

### J1 – HUB75 panel socket

The panel's data lines are driven at 3.3 V logic straight from the ESP32, so the panel must accept 3.3 V inputs. Pin numbers below are J1's own (pin 1 = square pad, bottom-left).

| J1 pin | Signal | ESP32 GPIO | J1 pin | Signal | ESP32 GPIO |
| --- | --- | --- | --- | --- | --- |
| 1 | OE | 48 | 2 | GND | – |
| 3 | CLK | 4 | 4 | LAT | 47 |
| 5 | C | 42 | 6 | D | 6 |
| 7 | A | 40 | 8 | B | 41 |
| 9 | B2 | 5 | 10 | E | 39 |
| 11 | R2 | 16 | 12 | G2 | 15 |
| 13 | B1 | 18 | 14 | GND | – |
| 15 | R1 | 21 | 16 | G1 | 17 |

J1 is numbered in the reverse pair order of a standard HUB75 header (R1/G1 sit at pins 15/16, not 1/2). That fits a socket that plugs straight onto the panel's pins; with a ribbon cable, check that R1 lands on the panel's pin 1.

### H2 – 5 V panel output

Two-way screw terminal: **+5V** (left) and **GND** (right), 4 A max.

### H3 – programming / UART (TC2030-IDC-NL pads)

| H3 pad | Signal | Connect to on a 3.3 V USB-UART adapter |
| --- | --- | --- |
| 1 | 3V3 | 3.3 V (only if the board is not powered from H1) |
| 2 | ESP TX | Adapter RX |
| 3 | ESP RX | Adapter TX |
| 4 | EN (reset) | RTS, or pulse to GND by hand |
| 5 | BOOT (GPIO0) | DTR, or hold to GND by hand |
| 6 | GND | GND |

To enter flashing mode without DTR/RTS wiring: hold pad 5 to GND, briefly short pad 4 to GND, then release pad 5. Serial runs at 115200 baud.

## Before powering up

- [ ] H1 wired to the colour table above; each pair still twisted to the header
- [ ] Supply is passive DC (24 V recommended), not a PoE switch port
- [ ] Supply polarity checked: blue = +, brown = −
- [ ] First power-up without the panel: measure 5 V on H2 and 3.3 V on H3 pad 1
- [ ] Panel 5 V cable on H2 with correct polarity; no second supply connected to the panel
- [ ] HUB75 orientation checked: J1 pin 15 (R1) reaches the panel's R1
- [ ] Nothing metal near the antenna corner (top left)
