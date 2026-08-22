# EXPGXGO — DOS enabler for the EXP "GAME/MIDI G3" PC Card

Clean-room DOS enabler + recon tools for the **EXP GAME/MIDI G3** PCMCIA
game port / MIDI card (EXP Computer "Game Traveler" family; OEM: **Argosy
Research**, named in a non-standard CIS tuple 0x40). Built from the card's
own CIS, the vendor driver's plain-text configuration declarations (INF
resource tables), and live hardware recon; no vendor binary was read or
disassembled.

**Status: MIDI OUT working, including at 0x330 (2026-08-21).**
`EXPGXGO /PCIC /MPU` then `DOSMID /com=330 song.mid` plays a full MIDI
file to an external synth through the classic Sound Blaster-style layout
(gameport 0x201 + MIDI 0x330) — the 0x330 appearance is a pure hardware
remap through the PCIC's I/O-window offset registers, no TSR involved.

## The card (what the CIS says, and what it hides)

- VERS_1 `"EXP" / "GAME/MIDI" / "G3"`; **no MANFID** (identified by the
  VERS_1 substring `GAME/MIDI`)
- Config registers @ attribute **0x100**, COR only (index bits only — bits
  6/5 don't latch)
- CIS declares one 8-byte I/O block: index 1 (default) **0x200–0x207**,
  index 2 **0x208–0x20F** — the gameport (classic 0x201, reads 0xFF with
  no stick plugged)
- **The CIS does NOT declare the MIDI function.** The vendor's Windows INF
  does: a second 8-byte block — **0x250–0x257 with COR 1**, 0x260–0x267
  with COR 2 — holding a **16550-class UART** clocked so that **divisor
  24 = 31250 baud** (~12 MHz UART clock). MIDI OUT needs no IRQ; the
  vendor routes one for MIDI IN.
- Bring-up: window over the block → LCR=03 (8N1) → divisor 24 (DLAB
  dance) → MCR=00 → FCR=01 (optional; the vendor stack runs FIFOs on) →
  write THR with LSR-bit-5 pacing.

## Tools

- `EXPGXGO.ASM` → `EXPGXGO.COM` — three-backend enabler in the
  ES1688GO/SCP55GO mold: `/PCIC` (direct 82365), `/CS` (Card Services
  client, TSR + hot-plug), `/OB` (OmniBook 300/425/430 Socket Services).
  Auto-detects CS > SS > PCIC. Brings up gameport window, MIDI window,
  and the UART (31250 8N1), and reports the UART's LSR as liveness.
  `/MPU[=330]` remaps the UART onto the host bus at 0x330 through the
  PCIC's I/O-window offset registers (Cirrus/Ricoh 82365 supersets;
  verified on a Ricoh RL5C476) — with readback verification and automatic
  fallback to the native base on controllers without offset support.
  PCIC backend only. Game caveat: 0x331 is the UART's IER, not an MPU
  status port, so games hang probing the remap — for games use MPUSHIM
  instead (see below); the remap serves players like DOSMid.
  `/IO=200|208`, `/S=n`, `/W=seg`, `/FORCE`, `/OFF`, `/SWEEP` (recon).
  Build: `nasm -f bin EXPGXGO.ASM -o EXPGXGO.COM`.
- `EXPGXTRY.C` — recon probe (Watcom, `C:\WATCOM\BLD.BAT EXPGXTRY`):
  block dumps, gameport test, UART battery (`/UART`: poll-calibrated
  baud measurement, loopback, burst timing), `/FIFO` (plays test chords
  — on MIDI channels 2+3), `/CFG` (config-space experiments), `/PCIC`
  reg dump, `/OT` (I/O-window offset-remap experiment), `/S=n` socket.

## Playing MIDI from DOS

```
EXPGXGO /PCIC /MPU
DOSMID /com=330 CANYON.MID
```

Use DOSMid's **`/com=`** output (raw UART), NOT `/mpu` — the card has no
MPU-401 protocol engine, so MPU reset/ACK handshakes can never succeed;
`/com=` is the mode that matches the hardware. Without `/MPU` on the
enabler, the UART sits at its native base: `/com=250` (or `/com=260`
after `EXPGXGO /IO=208`). The enabler programs the UART, so DOSMid needs
no `MODE` setup.

## Lessons that cost real time (so you don't pay twice)

1. **The CIS lies by omission** — the MIDI block exists only in the
   vendor INF's resource declarations. A CIS-faithful enabler and a
   64-index COR sweep both missed it because nothing *reads back* at the
   gameport block without a joystick (0xFF is that gameport's honest
   empty answer).
2. **The UART hides its 16550 nature** — FCR writes are invisible on a
   16450, and the idle register file is byte-identical.
3. **The MT-32/CM-32L family ignores MIDI channel 1 by default**
   (receives 2–10), and Active Sensing doesn't blink its MIDI light.
   Days of "dead TX" were a synth discarding channel-1 test notes; the
   transmitter had been working nearly from the start. Blind test notes
   must use channel 2+.

## Games: use MPUSHIM

The `/MPU` hardware remap gives *players* an MPU at 330h, but games also
need the MPU-401 *protocol* (status bits, the 0xFE ACK) that this card's
bare UART cannot answer — they hang probing it. That layer now exists as
its own tool: **MPUSHIM**, a 1.6 KB resident MPU-401 facade that traps
330h/331h and forwards to this card's UART. Bench-proven with Monkey
Island 1 (Roland MT-32 music) on the IBM PC110. Recipe: enable with
`EXPGXGO /PCIC` (native 250h, no `/MPU`), then run MPUSHIM under
JEMM386+QPIEMU — see the MPUSHIM project.

## Open items

- Hardware-verify the /CS and /OB backends
- MIDI IN (IRQ) untested

## License

MIT © zikolas
