# EXPGXGO — DOS enabler for EXP "Game Traveler" PC Cards

Clean-room DOS enabler and recon tool for EXP Computer's PCMCIA game
port / MIDI cards (OEM: **Argosy Research**, named in a non-standard CIS
tuple 0x40). Built from the cards' own CIS, the vendor driver's plain-text
configuration declarations, and live hardware recon — no vendor binary was
read or disassembled.

**Status:** the **GAME/MIDI G3** is fully bench-proven on an IBM PC110 —
gameport and MIDI both come up, and MIDI plays. The gameport-only siblings
(the G1/G2 class) are supported best-effort but **untested**; we hold no
such card.

## The card, and what its CIS hides

- VERS_1 `"EXP" / "GAME/MIDI" / "G3"`; **no MANFID**, so identity is a
  VERS_1 substring match
- Config registers @ attribute **0x100**, COR only (index bits only)
- CIS declares one 8-byte I/O block: index 1 (default) **0x200–0x207**,
  index 2 **0x208–0x20F** — the gameport (classic 0x201; reads 0xFF with
  no stick plugged, which is its honest empty answer)
- **The CIS never declares the MIDI function.** The vendor's Windows INF
  does: a second 8-byte block — **0x250–0x257 with COR 1**, 0x260–0x267
  with COR 2 — holding a **16550-class UART** clocked so that **divisor 24
  = 31250 baud**. MIDI OUT needs no IRQ.

The family differs in what it carries, not how it is configured, so one
bring-up serves all of it: match `GAME` to claim the card, then
`GAME/MIDI` to decide whether the MIDI half exists. Gameport-only cards
get a single I/O window and no UART programming.

## Tools

**`EXPGXGO.COM`** — three-backend enabler in the ES1688GO/SCP55GO mould:
`/PCIC` (direct 82365), `/CS` (Card Services client, TSR + hot-plug),
`/OB` (OmniBook 300/425/430 Socket Services). Auto-detects CS > SS > PCIC.

```
EXPGXGO [/PCIC|/CS|/OB] [/IO=200] [/MIDI[=330]] [/S=n] [/W=D000] [/OFF]
  /IO=hex    card config: 200 (default) or 208; picks the COR index
  /MIDI[=hex] put the MIDI UART at host I/O 330 (or hex) instead of its
             native base — a bridge remap, NOT an MPU-401
  /S=dec     socket (PCIC 0-7, OB 1-2; CS: probe only this socket)
  /W=hex     attr-window segment for CIS/COR (PCIC, default D000)
  /FORCE     configure without the CIS identity check (needs /S)
  /OFF       power the socket down (PCIC) / release + dormant (CS)
  /SWEEP     recon: walk COR 00-3F watching the I/O blocks
```

`/MIDI` uses the bridge's I/O-window offset registers (Cirrus/Ricoh 82365
supersets — verified on both a Ricoh RL5C476 and the PC110's RB5C396),
with readback verification and automatic fallback to the native base on
controllers without offset support. PCIC backend only.

**`EXPGXTRY.C`** — recon probe (Watcom, `C:\WATCOM\BLD.BAT EXPGXTRY`):
block dumps, gameport test, UART battery (`/UART`: poll-calibrated baud
measurement, loopback, burst timing), `/FIFO` test chords, `/CFG`
config-space experiments, `/PCIC` register dump, `/OT` offset-remap
experiment, `/S=n` socket select.

## Playing MIDI

For a *player*, the card's UART is driven directly:

```
EXPGXGO /PCIC
DOSMID /com=250 CANYON.MID
```

Use DOSMid's `/com=` (raw UART), **not** `/mpu` — the card has no MPU-401
protocol engine, so reset/ACK handshakes can never succeed. With `/MIDI`
the UART appears at 0x330 and `/com=330` works instead.

*Games* are different: they speak the MPU-401 handshake, which a bare UART
cannot answer — they hang probing it. That layer is a separate tool,
**MPUSHIM**, which traps 330h/331h and presents a real MPU-401 facade over
this card's UART. Enable with `EXPGXGO /PCIC` (native base, no `/MIDI`),
then run MPUSHIM.

## Lessons that cost real time

1. **The CIS lies by omission** — the MIDI block exists only in the vendor
   INF's resource declarations. A CIS-faithful enabler and a 64-index COR
   sweep both missed it, because nothing reads back at the gameport block
   without a joystick attached.
2. **The UART hides its 16550 nature** — FCR writes are invisible on a
   16450 and the idle register file is byte-identical.
3. **The MT-32/CM-32L family ignores MIDI channel 1 by default** (it
   receives 2–10), and Active Sensing doesn't blink its MIDI light. Days
   of "dead TX" were a synth discarding channel-1 test notes while the
   transmitter worked fine. Blind test notes must use channel 2+.

## Open items

- Hardware-verify the `/CS` and `/OB` backends
- Verify the family path on a real G1/G2
- MIDI IN (IRQ) untested

## License

MIT © zikolas
