/* EXPGXTRY.C - register recon probe for the EXP "GAME/MIDI G3" PCMCIA
 * card.  Run EXPGXGO first: this tool assumes the card is ENABLED and
 * its 8-byte I/O block is decoding (default 0x200-0x207, /IO=208 for
 * config 2).  It never touches the PCIC.
 * Copyright (c) 2026 zikolas. MIT License.
 *
 * What it does, in order:
 *   1. dumps the 8 registers twice back-to-back (volatile bits show as *)
 *   2. gameport behaviour at base+1: resting value, then fire the
 *      one-shots with a write and count how fast the axis nibble clears
 *      (no joystick = open pots = typically never; a changing count
 *      still proves a real one-shot chip is there)
 *   3. MPU-401-style discovery on the (data,status) pairs base+2/+3,
 *      base+4/+5, base+6/+7: read status (0xFF = open bus), command
 *      0xFF (MPU reset) hunting the 0xFE ACK, command 0x3F (UART mode),
 *      then clock one Active Sensing byte (0xFE, harmless MIDI
 *      keepalive) through the TX watching the write-ready bit toggle -
 *      a real 31.25 kbaud shift register goes busy ~320 us per byte, an
 *      open bus or a latch does not.
 *   4. dumps the 8 registers again (what did the probing change?)
 *
 * Usage: EXPGXTRY [/IO=200|208] [/RO] [/PCIC] [/P03=xx /P06=xx /P07=xx]
 *                 [/MIDI]
 *   /RO      read-only: dump + gameport read, skip all write probes
 *   /PCIC    dump 82365 socket-0 regs 00-1F (back-to-back index/data in
 *            code, so the PC110's resident CS poller can't clobber it -
 *            cross-call MCP io_in/io_out CANNOT do this)
 *   /P03=xx  poke PCIC reg 03/06/07 (socket 0), then re-dump the block -
 *   /P06=xx  for trying interface/window-enable/IOIS16 variants without
 *   /P07=xx  a rebuild
 *   /MIDI    blind MIDI-out spray: the card's MIDI may be a WRITE-ONLY
 *            TX latch with no readable status (the PCJOY2's data port
 *            also floats FF on reads), so reads can't find it.  Sends a
 *            paced note-on/off to EVERY register in the block, a
 *            DIFFERENT pitch per port (C4 D4 E4 F#4 G#4 A#4 C5 D5 for
 *            base+0..+7), >=1 ms/byte (a 31.25 kbaud shift register
 *            needs 320 us).  A listener on the connected synth maps the
 *            heard pitch back to the port.  Ends with All Notes Off to
 *            every port.
 *
 * Build: C:\WATCOM\BLD.BAT EXPGXTRY  (Open Watcom, wcc -ms, C89)
 */

#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <i86.h>

static void mpu_recon(unsigned base);

/* 82365 socket access: index+data back-to-back so a resident CS poller
 * can't rewrite the index register between the two halves.  Socket n
 * lives at index port 3E0h + (n/2)*2, register offset (n&1)*40h -
 * /S=n selects (default 0; the 235's Ricoh puts the card at socket 4). */
static unsigned pcidx = 0x3E0;
static unsigned pcoff = 0;
static unsigned char pcic_rd(unsigned reg)
{
    outp(pcidx, pcoff + reg);
    return (unsigned char)inp(pcidx + 1);
}
static void pcic_wr(unsigned reg, unsigned val)
{
    outp(pcidx, pcoff + reg);
    outp(pcidx + 1, val);
}
static void pcic_dump(void)
{
    int r;
    printf("  PCIC socket 0 regs (3E0/3E1):\n");
    for (r = 0; r < 0x40; r++) {
        if ((r & 7) == 0) printf("    %02X:", r);
        printf(" %02X", pcic_rd(r));
        if ((r & 7) == 7) printf("\n");
    }
}

static void dump_block(unsigned base)
{
    unsigned char r1[8], r2[8];
    int i;
    for (i = 0; i < 8; i++) r1[i] = (unsigned char)inp(base + i);
    for (i = 0; i < 8; i++) r2[i] = (unsigned char)inp(base + i);
    printf("  regs %03X-%03X:", base, base + 7);
    for (i = 0; i < 8; i++) printf(" %02X", r1[i]);
    printf("\n        pass 2:");
    for (i = 0; i < 8; i++) printf(" %02X%c", r2[i], r2[i] != r1[i] ? '*' : ' ');
    printf("\n");
}

static void joy_read(unsigned port)
{
    printf("  gameport %03X resting: %02X %02X %02X %02X\n", port,
           (unsigned char)inp(port), (unsigned char)inp(port),
           (unsigned char)inp(port), (unsigned char)inp(port));
}

static void joy_fire(unsigned port)
{
    unsigned long n;
    outp(port, 0xFF);
    for (n = 0; n < 200000UL; n++)
        if (((unsigned char)inp(port) & 0x0F) == 0x00) break;
    printf("  gameport %03X fired: axis nibble ", port);
    if (n >= 200000UL) printf("never cleared (now %02X) - open pots or no one-shots\n",
                              (unsigned char)inp(port));
    else printf("cleared after %lu polls - one-shots alive\n", n);
}

static void iodelay(unsigned long n)
{
    while (n--) (void)inp(0x80);
}

/* ~1 ms per unit on this class of hardware (port-80h read ~1-2 us) */
#define MSD(x) iodelay((unsigned long)(x) * 800UL)

static void midi_byte(unsigned port, unsigned char b)
{
    outp(port, b);
    MSD(2);                            /* >= 320 us/byte, generously */
}

static const unsigned char note_lo[8] = {   /* C4 D4 E4 F#4 G#4 A#4 C5 D5 */
    0x3C, 0x3E, 0x40, 0x42, 0x44, 0x46, 0x48, 0x4A
};
static const char *name_lo[8] = {
    "C4", "D4", "E4", "F#4", "G#4", "A#4", "C5", "D5"
};
static const unsigned char note_hi[8] = {   /* E5 F#5 G#5 A#5 C6 D6 E6 F#6 */
    0x4C, 0x4E, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5A
};
static const char *name_hi[8] = {
    "E5", "F#5", "G#5", "A#5", "C6", "D6", "E6", "F#6"
};

static void midi_spray(unsigned base, const unsigned char *note,
                       const char **names)
{
    int p, rep;
    printf("MIDI blind spray at %03X-%03X (pitch -> port):\n",
           base, base + 7);
    for (p = 0; p < 8; p++) {
        printf("  port %03X -> note %s ...", base + p, names[p]);
        for (rep = 0; rep < 2; rep++) {
            midi_byte(base + p, 0x90);         /* note on, ch 1 */
            midi_byte(base + p, note[p]);
            midi_byte(base + p, 0x7F);
            MSD(350);
            midi_byte(base + p, 0x80);         /* note off */
            midi_byte(base + p, note[p]);
            midi_byte(base + p, 0x40);
            MSD(120);
        }
        printf(" sent\n");
    }
    for (p = 0; p < 8; p++) {                  /* stuck-note insurance */
        midi_byte(base + p, 0xB0);             /* All Notes Off, ch 1 */
        midi_byte(base + p, 0x7B);
        midi_byte(base + p, 0x00);
    }
}

/* ---- attribute/config-space access --------------------------------------
 * Maps PCIC memory window 0 at segment D000 onto ATTRIBUTE space (REG
 * active) exactly like the enabler's probe does, so config registers can
 * be dumped and poked live while the card stays in I/O mode.  The G3's
 * config base is attr 0x100 (COR); a CCSR would sit at 0x102, PRR at
 * 0x104 - the CIS's reg-present mask claims COR only, but vendors lie. */
#define ATTRSEG 0xD000
static void map_attr(void)
{
    unsigned pg = ATTRSEG >> 8, off = ((0u - pg) & 0x3FFF) | 0x4000;
    pcic_wr(0x10, pg & 0xFF);
    pcic_wr(0x11, (pg >> 8) & 0x3F);
    pcic_wr(0x12, (pg + 3) & 0xFF);
    pcic_wr(0x13, ((pg + 3) >> 8) & 0x3F);
    pcic_wr(0x14, off & 0xFF);
    pcic_wr(0x15, (off >> 8) & 0xFF);
    pcic_wr(0x06, pcic_rd(0x06) | 0x01);
}
static void unmap_attr(void)
{
    pcic_wr(0x06, pcic_rd(0x06) & ~0x01);
}
static unsigned char attr_rd(unsigned a)
{
    /* TPCC_RADR is a raw attribute-space address and the window maps
     * card addresses 1:1 - the COR at attr 0x100 is host offset 0x100
     * (only even offsets are real attribute bytes) */
    unsigned char __far *p =
        (unsigned char __far *)MK_FP(ATTRSEG, a);
    return *p;
}
static void attr_wr(unsigned a, unsigned char v)
{
    unsigned char __far *p =
        (unsigned char __far *)MK_FP(ATTRSEG, a);
    *p = v;
}
static void cfg_dump(void)
{
    unsigned a;
    printf("  config space (attr 100-11E, even bytes):\n   ");
    for (a = 0x100; a < 0x120; a += 2)
        printf(" %02X", attr_rd(a));
    printf("\n");
}

/* Offset-remap proof for the PC110's Ricoh RB5C396 (82365 superset).
 * Superset PCICs keep I/O window offset regs at 0x36/0x37 (win0) and
 * 0x38/0x39 (win1): card address = host address + offset (16-bit
 * wrap).  We map host 0x340-0x347 (nothing decodes there) onto the
 * card's MPU block at 0x250-0x257 via offset 0xFF10 and look for the
 * MPU status coming back.  Cleans up after itself. */
static void offset_test(unsigned mbase)
{
    unsigned off = (unsigned)(mbase - 0x340);   /* e.g. 0x250-0x340 = 0xFF10 */
    printf("offset-remap test: host 340-347 -> card %03X-%03X (win1 offset %04X)\n",
           mbase, mbase + 7, off);
    pcic_wr(0x06, pcic_rd(0x06) & ~0x80);       /* win1 off while we program */
    pcic_wr(0x38, off & 0xFF);
    pcic_wr(0x39, (off >> 8) & 0xFF);
    printf("  offset regs 38/39 read back: %02X %02X\n",
           pcic_rd(0x38), pcic_rd(0x39));
    pcic_wr(0x0C, 0x40);                        /* win1 = 340-347 */
    pcic_wr(0x0D, 0x03);
    pcic_wr(0x0E, 0x47);
    pcic_wr(0x0F, 0x03);
    pcic_wr(0x06, pcic_rd(0x06) | 0x80);
    dump_block(0x340);
    mpu_recon(0x340);
    pcic_wr(0x06, pcic_rd(0x06) & ~0x80);       /* cleanup: win1 off, offset 0 */
    pcic_wr(0x38, 0);
    pcic_wr(0x39, 0);
    printf("  (win1 disabled again, offset cleared)\n");
}

/* ---- 8250/16450 UART recon --------------------------------------------
 * The MIDI block's idle values (IER=00 IIR=01 LSR=60 MSR=F0) are a
 * textbook 8250-class UART register file: the card's MIDI is a plain PC
 * serial UART, and the vendor VxD MPU-emulates on top of it.  MIDI is
 * 31250 baud = clock/(16*divisor): crystal 0.5/1/2/4 MHz -> divisor
 * 1/2/4/8.  We time the transmitter-empty (LSR bit6) busy period per
 * byte at each divisor - busy polls scale with the divisor, and the
 * right one is confirmed by ear with a per-divisor pitch. */
static void uart_setdiv(unsigned b, unsigned d)
{
    outp(b + 3, 0x80);                 /* DLAB */
    outp(b + 0, d & 0xFF);
    outp(b + 1, (d >> 8) & 0xFF);
    outp(b + 3, 0x03);                 /* 8N1, DLAB off */
    outp(b + 4, 0x0F);                 /* MCR: DTR|RTS|OUT1|OUT2 */
    (void)inp(b + 0);                  /* flush RBR */
    (void)inp(b + 5);                  /* clear stale LSR bits */
}

static unsigned uart_txtime(unsigned b, unsigned char v)
{
    unsigned n;
    for (n = 0; n < 60000U; n++)
        if ((unsigned char)inp(b + 5) & 0x20) break;   /* THRE */
    outp(b, v);
    for (n = 0; n < 60000U; n++)
        if ((unsigned char)inp(b + 5) & 0x40) break;   /* TEMT back */
    return n;
}

static void uart_send(unsigned b, unsigned char v)
{
    unsigned n;
    for (n = 0; n < 60000U; n++)
        if ((unsigned char)inp(b + 5) & 0x20) break;
    outp(b, v);
}

/* Calibrate the LSR poll loop against the BIOS tick (54.925 ms): returns
 * polls per tick, so busy-poll counts convert to real microseconds. */
static unsigned long uart_calib(unsigned b)
{
    volatile unsigned long __far *tick =
        (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned long t0, n = 0;
    t0 = *tick;
    while (*tick == t0) ;              /* align to a tick edge */
    t0 = *tick;
    while (*tick == t0) {
        (void)inp(b + 5);              /* the same shape as the busy poll */
        n++;
    }
    return n;                          /* polls per 54925 us */
}

static void uart_test(unsigned b)
{
    static const unsigned dv[5] = { 1, 2, 4, 8, 16 };
    unsigned long ppt, byte_us, baud;
    unsigned t1, t2, bestdiv = 8;
    unsigned char m;
    int i;
    printf("UART recon at %03X (8250-class):\n", b);
    ppt = uart_calib(b);
    printf("  poll calibration: %lu polls/BIOS tick (%lu ns/poll)\n",
           ppt, ppt ? 54925000UL / ppt : 0);
    for (i = 0; i < 5; i++) {
        uart_setdiv(b, dv[i]);
        t1 = uart_txtime(b, 0xFE);
        t2 = uart_txtime(b, 0xFE);
        byte_us = ppt ? ((unsigned long)t2 * 54925UL) / ppt : 0;
        baud = byte_us ? 10000000UL / byte_us : 0;
        printf("  div %2u: busy %u,%u polls = %lu us/byte -> ~%lu baud",
               dv[i], t1, t2, byte_us, baud);
        if (baud > 28000UL && baud < 35000UL) {
            bestdiv = dv[i];
            printf("  <== MIDI");
        }
        printf("\n");
    }
    /* internal loopback: TX->RX inside the chip proves the data path and
     * our divisor/framing programming end to end */
    uart_setdiv(b, 24);
    outp(b + 4, 0x1F);                 /* loopback + DTR/RTS/OUT1/OUT2 */
    (void)inp(b);                      /* flush */
    uart_send(b, 0xA5);
    for (t1 = 0; t1 < 60000U; t1++)
        if ((unsigned char)inp(b + 5) & 0x01) break;   /* DR */
    m = (unsigned char)inp(b);
    printf("  loopback @div 24: sent A5, DR %s, RBR %02X %s\n",
           (t1 < 60000U) ? "set" : "NEVER",
           m, (m == 0xA5) ? "(PASS)" : "(FAIL)");
    outp(b + 4, 0x0F);                 /* loopback off */
    (void)bestdiv;
    /* burst timing at div 24: 12.000 MHz / 16 / 24 = 31250.0 exactly -
     * stream 2048 bytes against the BIOS tick for a few-percent baud
     * measurement that the short single-byte polls cannot give */
    {
        volatile unsigned long __far *tick =
            (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
        unsigned long tk0, tk1, us;
        unsigned k;
        uart_setdiv(b, 24);
        tk0 = *tick;
        while (*tick == tk0) ;         /* align */
        tk0 = *tick;
        for (k = 0; k < 2048; k++) uart_send(b, 0xFE);
        for (k = 0; k < 60000U; k++)
            if ((unsigned char)inp(b + 5) & 0x40) break;
        tk1 = *tick;
        us = (tk1 - tk0) * 54925UL;
        printf("  burst @div 24: 2048 bytes in %lu ticks (~%lu us) -> ~%lu baud\n",
               tk1 - tk0, us, us ? 20480000UL / (us / 1000UL) : 0);
    }
    /* divisor fine scan around 24, MCR=0F: only the right baud is a note */
    printf("divisor scan (MCR=0F): 22=C4 23=D4 24=E4 25=F#4 26=G#4\n");
    for (i = 0; i < 5; i++) {
        static const unsigned fdv[5] = { 22, 23, 24, 25, 26 };
        static const unsigned char fnote[5] = { 0x3C, 0x3E, 0x40, 0x42, 0x44 };
        uart_setdiv(b, fdv[i]);
        uart_send(b, 0x90);
        uart_send(b, fnote[i]);
        uart_send(b, 0x7F);
        MSD(350);
        uart_send(b, 0x80);
        uart_send(b, fnote[i]);
        uart_send(b, 0x40);
        uart_send(b, 0xB0);
        uart_send(b, 0x7B);
        uart_send(b, 0x00);
        MSD(120);
    }
    /* MCR sweep note test at div 24: the DIN driver may be enable-gated
     * on a modem control bit; chromatic pitch per MCR value, C5 + MCR */
    printf("MCR sweep at div 24 - chromatic notes C5(MCR=0)..D#6(MCR=F):\n");
    for (m = 0; m < 16; m++) {
        uart_setdiv(b, 24);
        outp(b + 4, m);
        uart_send(b, 0x90);
        uart_send(b, (unsigned char)(0x48 + m));
        uart_send(b, 0x7F);
        MSD(350);
        uart_send(b, 0x80);
        uart_send(b, (unsigned char)(0x48 + m));
        uart_send(b, 0x40);
        uart_send(b, 0xB0);
        uart_send(b, 0x7B);
        uart_send(b, 0x00);
        MSD(120);
    }
    printf("  done - which pitches played? low octave = divisor scan,\n"
           "  high octave = MCR sweep (semitone above C5 = MCR value)\n");
}

/* Windows-matched TX test.  Live peek of the WORKING vendor stack (Win98
 * on the 235, MIDI playing flawlessly) showed the one register state we
 * never tried: IIR reads 0xC1 = this is a 16550-CLASS UART WITH ITS
 * FIFOs ENABLED (the VxD writes FCR, which a true 16450 ignores - so our
 * register-file ID missed it), and MCR=00.  A vendor ASIC whose driver
 * always enables FIFOs may simply have no working non-FIFO TX path.
 * Reproduce the exact Windows state and play. */
static void fifo_test(unsigned b)
{
    static const unsigned char n1[4] = { 0x3C, 0x40, 0x43, 0x48 };
    int i;
    /* CM-32L/MT-32 family DEFAULTS TO RECEIVE CHANNELS 2-10 - channel 1
     * (9n = 90h) is DISCARDED, and Active Sensing does not blink the
     * MIDI light.  Every earlier spray was channel 1 or 0xFE...  Send on
     * channels 2 AND 3 (91h/92h). */
    printf("Windows-matched TX: 8N1, FCR=01 (FIFO ON), MCR=00, CH 2+3\n");
    uart_setdiv(b, 24);
    outp(b + 2, 0x01);                 /* FCR: enable FIFOs */
    outp(b + 4, 0x00);                 /* MCR = 00, exactly as Windows */
    printf("  IIR now %02X (C1 = FIFO mode latched)\n",
           (unsigned char)inp(b + 2));
    printf("  div 24, ch2+ch3: C4 E4 G4 C5 ...");
    for (i = 0; i < 4; i++) {
        uart_send(b, 0x91); uart_send(b, n1[i]); uart_send(b, 0x7F);
        uart_send(b, 0x92); uart_send(b, n1[i]); uart_send(b, 0x7F);
        MSD(300);
        uart_send(b, 0x81); uart_send(b, n1[i]); uart_send(b, 0x40);
        uart_send(b, 0x82); uart_send(b, n1[i]); uart_send(b, 0x40);
        MSD(80);
    }
    uart_send(b, 0xB1); uart_send(b, 0x7B); uart_send(b, 0x00);
    uart_send(b, 0xB2); uart_send(b, 0x7B); uart_send(b, 0x00);
    printf(" sent\n  div 25, ch2+ch3: same tune ...");
    uart_setdiv(b, 25);
    outp(b + 2, 0x01);
    outp(b + 4, 0x00);
    for (i = 0; i < 4; i++) {
        uart_send(b, 0x91); uart_send(b, n1[i]); uart_send(b, 0x7F);
        uart_send(b, 0x92); uart_send(b, n1[i]); uart_send(b, 0x7F);
        MSD(300);
        uart_send(b, 0x81); uart_send(b, n1[i]); uart_send(b, 0x40);
        uart_send(b, 0x82); uart_send(b, n1[i]); uart_send(b, 0x40);
        MSD(80);
    }
    uart_send(b, 0xB1); uart_send(b, 0x7B); uart_send(b, 0x00);
    uart_send(b, 0xB2); uart_send(b, 0x7B); uart_send(b, 0x00);
    /* is the FIFO even needed, or was that another channel-1 artifact?
     * FCR=00 (16450 mode), ch2, div 24 then 25 */
    printf(" sent\n  FIFO OFF, div 24, ch2: same tune ...");
    uart_setdiv(b, 24);
    outp(b + 2, 0x00);                 /* FCR: FIFOs off */
    outp(b + 4, 0x00);
    for (i = 0; i < 4; i++) {
        uart_send(b, 0x91); uart_send(b, n1[i]); uart_send(b, 0x7F);
        MSD(300);
        uart_send(b, 0x81); uart_send(b, n1[i]); uart_send(b, 0x40);
        MSD(80);
    }
    uart_send(b, 0xB1); uart_send(b, 0x7B); uart_send(b, 0x00);
    printf(" sent\n  FIFO OFF, div 25, ch2: same tune ...");
    uart_setdiv(b, 25);
    outp(b + 2, 0x00);
    outp(b + 4, 0x00);
    for (i = 0; i < 4; i++) {
        uart_send(b, 0x91); uart_send(b, n1[i]); uart_send(b, 0x7F);
        MSD(300);
        uart_send(b, 0x81); uart_send(b, n1[i]); uart_send(b, 0x40);
        MSD(80);
    }
    uart_send(b, 0xB1); uart_send(b, 0x7B); uart_send(b, 0x00);
    printf(" sent\n  which of the four tunes played?\n"
           "  (1: FIFO div24  2: FIFO div25  3: noFIFO div24  4: noFIFO div25)\n");
}

/* FIFO-ON divisor sweep: the FIFO discovery and the divisor scan never
 * overlapped (all divisor tests ran FIFO-off), and the div-24 pick rests
 * on +/-8% timing.  Sweep 20-26 with FCR=01/MCR=00, burst-time each to a
 * real baud figure, and tag each with its own pitch. */
static void fifo_sweep(unsigned b)
{
    static const unsigned dv[7] = { 20, 21, 22, 23, 24, 25, 26 };
    static const unsigned char nt[7] =
        { 0x3C, 0x3E, 0x40, 0x42, 0x44, 0x46, 0x48 };
    static const char *nn[7] =
        { "C4", "D4", "E4", "F#4", "G#4", "A#4", "C5" };
    volatile unsigned long __far *tick =
        (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned long t0, t1, us, baud;
    unsigned k;
    int i;
    printf("FIFO-ON divisor sweep (FCR=01 MCR=00), 2048-byte burst + note:\n");
    for (i = 0; i < 7; i++) {
        uart_setdiv(b, dv[i]);
        outp(b + 2, 0x01);
        outp(b + 4, 0x00);
        t0 = *tick;
        while (*tick == t0) ;
        t0 = *tick;
        for (k = 0; k < 2048; k++) uart_send(b, 0xFE);
        for (k = 0; k < 60000U; k++)
            if ((unsigned char)inp(b + 5) & 0x40) break;
        t1 = *tick;
        us = (t1 - t0) * 54925UL;
        baud = us ? 20480000UL / (us / 1000UL) : 0;
        printf("  div %2u: ~%5lu baud -> note %s ...", dv[i], baud, nn[i]);
        uart_send(b, 0x90); uart_send(b, nt[i]); uart_send(b, 0x7F);
        MSD(350);
        uart_send(b, 0x80); uart_send(b, nt[i]); uart_send(b, 0x40);
        uart_send(b, 0xB0); uart_send(b, 0x7B); uart_send(b, 0x00);
        MSD(120);
        printf(" sent\n");
    }
    printf("  which pitch played cleanly (C4..C5 = div 20..26)?\n");
}

/* map PCIC I/O window 1 over an 8-byte block so accesses reach the card.
 * The vendor's PCMGAME3.INF (config data) declares a SECOND I/O range the
 * CIS never admits to: the MPU-401 block at 0x250-0x257 with COR 1, or
 * 0x260-0x267 with COR 2. */
static void map_win1(unsigned b)
{
    pcic_wr(0x0C, b & 0xFF);
    pcic_wr(0x0D, (b >> 8) & 0xFF);
    pcic_wr(0x0E, (b + 7) & 0xFF);
    pcic_wr(0x0F, ((b + 7) >> 8) & 0xFF);
    pcic_wr(0x06, pcic_rd(0x06) | 0x80);
    printf("  (PCIC win1 mapped over %03X-%03X)\n", b, b + 7);
}

static void mpu_recon(unsigned base)
{
    unsigned st = base + 1;
    unsigned char s0, d;
    unsigned i, busy;
    int got_ack = 0;
    s0 = (unsigned char)inp(st);
    printf("  pair %03X/%03X: status %02X", base, st, s0);
    if (s0 == 0xFF) { printf(" - open bus, skip\n"); return; }
    outp(st, 0xFF);                                   /* MPU reset */
    for (i = 0; i < 60000U; i++)
        if (!((unsigned char)inp(st) & 0x80)) break;  /* DSR: data ready */
    if (i < 60000U) {
        d = (unsigned char)inp(base);
        printf(", reset -> data %02X%s", d, d == 0xFE ? " (ACK!)" : "");
        got_ack = (d == 0xFE);
    } else printf(", reset -> no ACK");
    outp(st, 0x3F);                                   /* enter UART mode */
    for (i = 0; i < 60000U; i++)
        if (!((unsigned char)inp(st) & 0x80)) { (void)inp(base); break; }
    /* TX shift test: DRR (bit6) low = ready; must go busy on a byte and
     * come back ready when the shift register drains */
    for (i = 0; i < 60000U; i++)
        if (!((unsigned char)inp(st) & 0x40)) break;
    if ((unsigned char)inp(st) & 0x40) { printf(", never write-ready\n"); return; }
    outp(base, 0xFE);                                 /* MIDI Active Sensing */
    for (i = 0; i < 60000U; i++)
        if ((unsigned char)inp(st) & 0x40) break;     /* went busy? */
    if (i < 60000U) {
        busy = i;
        for (i = 0; i < 60000U; i++)
            if (!((unsigned char)inp(st) & 0x40)) break;
        if (i < 60000U) printf(", TX SHIFTS (busy after %u, ready again)%s\n",
                               busy, got_ack ? "  ** MPU-401 HERE **"
                                             : "  ** UART HERE **");
        else printf(", TX stuck busy\n");
    } else printf(", write-ready never toggled (latch or nothing)\n");
}

int main(int argc, char **argv)
{
    unsigned base = 0x200;
    int ro = 0, showpcic = 0, domidi = 0, dooff = 0, douart = 0, docfg = 0;
    int dotx = 0, dofifo = 0, i;
    int pokereg[3];
    unsigned pokeval[3];
    int npoke = 0;
    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        if ((a[1] == 'I' || a[1] == 'i') && (a[2] == 'O' || a[2] == 'o') &&
            (a[3] == '=' || a[3] == ':'))
            base = (unsigned)strtol(a + 4, 0, 16);
        else if ((a[1] == 'P' || a[1] == 'p') && a[2] == '0' &&
                 (a[3] == '3' || a[3] == '6' || a[3] == '7') &&
                 (a[4] == '=' || a[4] == ':')) {
            if (npoke < 3) {
                pokereg[npoke] = a[3] - '0';
                pokeval[npoke] = (unsigned)strtol(a + 5, 0, 16);
                npoke++;
            }
        }
        else if ((a[1] == 'S' || a[1] == 's') && (a[2] == '=' || a[2] == ':')) {
            unsigned s = (unsigned)strtol(a + 3, 0, 10);
            if (s <= 7) {
                pcidx = 0x3E0 + ((s >> 1) * 2);
                pcoff = (s & 1) ? 0x40 : 0;
            }
        }
        else if ((a[1] == 'P' || a[1] == 'p'))
            showpcic = 1;
        else if (a[1] == 'M' || a[1] == 'm')
            domidi = 1;
        else if ((a[1] == 'O' || a[1] == 'o') && (a[2] == 'T' || a[2] == 't'))
            dooff = 1;
        else if (a[1] == 'U' || a[1] == 'u')
            douart = 1;
        else if (a[1] == 'C' || a[1] == 'c')
            docfg = 1;
        else if (a[1] == 'T' || a[1] == 't')
            dotx = 1;
        else if (a[1] == 'F' || a[1] == 'f')
            dofifo = 1;
        else if (a[1] == 'R' || a[1] == 'r')
            ro = 1;
        else if (a[1] == '?') {
            printf("EXPGXTRY [/IO=200|208] [/RO] [/PCIC] [/P03=xx /P06=xx /P07=xx]\n"
                   "Run EXPGXGO first.\n");
            return 0;
        }
    }
    if (base != 0x200 && base != 0x208) {
        printf("Bad /IO=%03X : the card decodes 200 or 208.\n", base);
        return 2;
    }
    printf("EXPGXTRY - EXP GAME/MIDI G3 register recon at %03X (run EXPGXGO first)\n",
           base);
    if (showpcic) pcic_dump();
    for (i = 0; i < npoke; i++) {
        printf("  poke PCIC reg %02X <- %02X (was %02X)\n",
               pokereg[i], pokeval[i], pcic_rd(pokereg[i]));
        pcic_wr(pokereg[i], pokeval[i]);
    }
    if ((unsigned char)inp(base + 1) == 0xFF &&
        (unsigned char)inp(base) == 0xFF &&
        (unsigned char)inp(base + 2) == 0xFF) {
        printf("  whole block floats FF - card not enabled? Run EXPGXGO.\n");
    }
    printf("pristine:\n");
    dump_block(base);
    joy_read(base + 1);
    if (dofifo) {
        unsigned mbase = (base == 0x208) ? 0x260 : 0x250;
        map_win1(mbase);
        fifo_test(mbase);
        return 0;
    }
    if (dotx) {
        unsigned mbase = (base == 0x208) ? 0x260 : 0x250;
        unsigned k, s;
        map_win1(mbase);
        uart_setdiv(mbase, 24);
        printf("TX measurement stream: continuous 00 bytes at 31250 for ~10s.\n"
               "  DIN pin 5 -> pin 2 should sit LOW (~0.5-1V) now vs ~5V idle;\n"
               "  DIN pin 4 -> pin 2 should read ~+5V the whole time.\n");
        for (s = 0; s < 10; s++) {
            for (k = 0; k < 3125U; k++)     /* ~1s of back-to-back bytes */
                uart_send(mbase, 0x00);
            printf("  %u", s + 1);
        }
        printf("\n  stream done - line back to idle (mark, ~5V).\n");
        return 0;
    }
    if (docfg) {
        unsigned mbase = (base == 0x208) ? 0x260 : 0x250;
        unsigned char corv = (base == 0x208) ? 2 : 1;
        map_win1(mbase);
        map_attr();
        printf("config-space experiments (watch the MIDI light per note):\n");
        cfg_dump();
        printf("  exp 1: CCSR(attr 102) <- 08 (audio/enable bit), note C4\n");
        attr_wr(0x102, 0x08);
        MSD(5);
        cfg_dump();
        uart_setdiv(mbase, 24);
        uart_send(mbase, 0x90); uart_send(mbase, 0x3C); uart_send(mbase, 0x7F);
        MSD(400);
        uart_send(mbase, 0x80); uart_send(mbase, 0x3C); uart_send(mbase, 0x40);
        printf("  exp 2: CCSR <- 00, COR <- %02X (index | bit6), note E4\n",
               corv | 0x40);
        attr_wr(0x102, 0x00);
        attr_wr(0x100, (unsigned char)(corv | 0x40));
        MSD(5);
        cfg_dump();
        uart_setdiv(mbase, 24);
        uart_send(mbase, 0x90); uart_send(mbase, 0x40); uart_send(mbase, 0x7F);
        MSD(400);
        uart_send(mbase, 0x80); uart_send(mbase, 0x40); uart_send(mbase, 0x40);
        printf("  exp 3: COR <- %02X (bit6+bit5+idx), note G4\n",
               corv | 0x60);
        attr_wr(0x100, (unsigned char)(corv | 0x60));
        MSD(5);
        cfg_dump();
        uart_setdiv(mbase, 24);
        uart_send(mbase, 0x90); uart_send(mbase, 0x43); uart_send(mbase, 0x7F);
        MSD(400);
        uart_send(mbase, 0x80); uart_send(mbase, 0x43); uart_send(mbase, 0x40);
        uart_send(mbase, 0xB0); uart_send(mbase, 0x7B); uart_send(mbase, 0x00);
        attr_wr(0x100, corv);              /* restore plain index */
        unmap_attr();
        printf("  restored COR=%02X. Any note/light? C4=CCSR08 E4=CORbit6"
               " G4=CORbit6+5\n", corv);
        return 0;
    }
    if (douart) {
        unsigned mbase = (base == 0x208) ? 0x260 : 0x250;
        map_win1(mbase);
        printf("MIDI block pristine:\n");
        dump_block(mbase);
        uart_test(mbase);
        return 0;
    }
    if (dooff) {
        offset_test((base == 0x208) ? 0x260 : 0x250);
        return 0;
    }
    if (domidi) {
        unsigned mbase = (base == 0x208) ? 0x260 : 0x250;
        map_win1(mbase);
        printf("MIDI (MPU) block by reads:\n");
        dump_block(mbase);
        mpu_recon(mbase);
        midi_spray(mbase, note_hi, name_hi);
        printf("  done - which pitch(es) did the synth play, if any?\n"
               "  (E5..F#6 = ports %03X..%03X)\n", mbase, mbase + 7);
        printf("after spraying:\n");
        dump_block(base);
        dump_block(mbase);
        return 0;
    }
    if (ro) return 0;
    printf("gameport one-shot test:\n");
    joy_fire(base + 1);
    printf("MPU/UART hunt:\n");
    mpu_recon(base + 2);
    mpu_recon(base + 4);
    mpu_recon(base + 6);
    printf("after probing:\n");
    dump_block(base);
    return 0;
}
