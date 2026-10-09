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

On the OmniBook, the ROM runs the card in the C: drive slot in either memory
mode or I/O mode, depending on the card. The HP and SanDisk SDP5 flash cards
run in memory mode; a 128 MB ATA card ran in I/O mode. The EXP card needs
the C: drive slot in memory mode. It is 8-bit, and with the C: card in
16-bit I/O mode the adapter reaches only its even-numbered registers: the
UART's divisor, line control and status registers sit at odd offsets and
float.

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

## Games need an MPU-401 — use MPUSHIM

There is **no MPU-401 on this card**, and no amount of enabling creates one.
Games do not just stream bytes at a MIDI port: they expect the MPU-401
handshake — a reset command answered by an `FEh` ACK, and a status port
whose bits report data-ready and write-ready. A bare UART answers none of
that, and a game's "drain any pending input" loop spins forever against a
status port that never says "no data", so it hangs before it draws a frame.
Relocating the UART to 0x330 with `/MIDI` does not help: the byte at 0x331
is then the UART's interrupt-enable register, not an MPU status port.

The missing layer is supplied in software by a companion tool:

> **[MPUSHIM](https://github.com/zikolas/mpushim)** — traps 330h/331h and
> presents a real MPU-401 UART-mode facade (correct status byte, the `FEh`
> ACK) while forwarding MIDI to this card's UART. It covers both real-mode
> (V86) and 32-bit DPMI/DOS4GW games.

Bring the card up at its **native** base and let the shim own 0x330:

```
EXPGXGO /PCIC          :: UART at 250 - do NOT pass /MIDI here
MPUSHIM /UART=250      :: presents the MPU-401 at 330
```

Then start the game and select **MPU-401 / General MIDI at 330**.
MPUSHIM is a separate project and is licensed **GPL v2**, unlike this
repository.

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
