/* osl1.h - Ocean OSL1 container parser (Phase 1)
 *
 * Parses the shared OSL1 header used by .LAP/.ALB/.ADL/.SCC/.RLD files into a
 * Song struct: header fields, the instrument table, and the pattern block
 * (track/row counts, order table @block+0x50, position-pointer table
 * @block+0x150). See RE-REPORT.md sections 4/8 and PLAN.md Appendix A.
 *
 * All reads are explicit little-endian; we never rely on struct packing.
 */
#ifndef MEDPLAY_OSL1_H
#define MEDPLAY_OSL1_H

#include <stddef.h>
#include <stdint.h>

#define OSL1_MAX_INSTR  256
#define OSL1_MAX_ORDER  256

/* Header byte @0x07 was previously assumed to be a device-type selector. That
 * is WRONG: MED.EXE does not tag OSL1 files by sound card at all. The container
 * is device-agnostic - each instrument record carries sub-data for whichever of
 * MED's five drivers is loaded (see osl1_device_name()), and one file can mix
 * instruments voiced on different devices. Across the whole corpus @0x07 only
 * ever holds 0x00, 0x02 or 0x04 and does not track the extension (.ADL/.SCC/
 * .RLD from one project all share 0x02). It is a format-revision/generation
 * counter, not a device id. The playable nature of a file is derived from its
 * instrument data instead - see Osl1Kind below. */
enum {
    OSL1_GEN_0 = 0x00,  /* oldest observed revision                           */
    OSL1_GEN_2 = 0x02,  /* mid revision                                       */
    OSL1_GEN_4 = 0x04   /* newest observed revision                           */
};

/* The sound devices MED.EXE drives, each via its own driver (ADLIB.DEV,
 * SBLAST.DEV, LAPC1.DEV, SCC1.DEV, plus a Super Nintendo target). Confirmed by
 * disassembly/strings of MED.EXE: the per-device extended-command help proves
 * the identities - "SNES Help." lists the S-DSP 8-tap FIR filter coefficients
 * and echo unit, while "SCC1 Help." lists Roland GS reverb macros (Room/Hall/
 * Plate/Delay) and chorus macros (Chorus 1-4/Flanger). NOTE: the device is a
 * per-instrument editor choice ("Device:" field) / loaded-driver choice; OSL1
 * itself is device-agnostic and stores no reliable device id, so this enum is
 * for reference/labelling only - use Osl1Kind for what is actually renderable. */
typedef enum {
    OSL1_DEV_ADLIB = 0,  /* ADLIB.DEV  - Yamaha OPL2 FM                        */
    OSL1_DEV_SBLAST,     /* SBLAST.DEV - Creative Sound Blaster               */
    OSL1_DEV_LAPC1,      /* LAPC1.DEV  - Roland LAPC-I / MT-32 (MIDI)         */
    OSL1_DEV_SCC1,       /* SCC1.DEV   - Roland SCC-1 / Sound Canvas (GS)     */
    OSL1_DEV_SNES        /* Super Nintendo S-DSP (FIR filter + echo)          */
} Osl1Device;

/* Per-instrument synth-type code, at variant +0x1A (= record +0x24 when the
 * record holds a single variant). It is the DEVICE NUMBER the instrument was
 * authored for - the same enum as `D_DEVICENUMBER` at offset 0 of every .DEV
 * (LAPC1.DEV.annotated.asm:29) - and it fixes the payload length exactly.
 * Verified on 6750 of the corpus's 6757 records (the 7 exceptions are .SNS
 * sample banks, whose payload really is variable):
 *
 *   code  device                payload  records
 *   0x02  Adlib / OPL2           16       734
 *   0x04  Roland LAPC-1 / MT-32  244     4953
 *   0x08  Roland SCC-1 / GS      128     1033
 *   0x81  SNES S-DSP sample      varies    35
 *
 * `0x04` used to be documented here as "FM-ext", a 286-byte FM record with a
 * 228-byte editor tail. That was wrong, and it is why every Roland song was
 * voiced with nonsense: the 244-byte payload is an MT-32 timbre dump - 8 bytes
 * of Patch Memory, 4 of Timbre Common, then 4 x 58-byte partials - which
 * LAPC1.DEV uploads over SysEx (LAPC1.DEV.annotated.asm:775-904). Reading its
 * first 11 bytes as an OPL2 patch yields a full-level modulator and a carrier
 * that never decays. See OSL1.md section 6. */
enum {
    OSL1_SYNTH_FM     = 0x02,  /* Adlib / OPL2: 16-byte payload, 11 live     */
    OSL1_SYNTH_ROLAND = 0x04,  /* LAPC-1 / MT-32: 244-byte timbre dump       */
    OSL1_SYNTH_SCC1   = 0x08,  /* SCC-1 / GS: 128-byte payload, GM prog @+2  */
    OSL1_SYNTH_SNES   = 0x81   /* SNES S-DSP sample (.SNS banks only)        */
};

/* Heuristic renderability classification derived from the instrument mix.
 * Since neither the extension nor any header byte reliably names the target
 * device, we classify a file by whether its instruments carry usable OPL2 FM
 * patches - i.e. by what the Adlib player can actually render. */
typedef enum {
    OSL1_KIND_UNKNOWN = 0,  /* no valid instruments                          */
    OSL1_KIND_ADLIB,        /* every valid instrument has an OPL2 FM patch    */
    OSL1_KIND_MIXED,        /* some OPL2, some Roland/SCC1 -> partly playable */
    OSL1_KIND_MIDI          /* no OPL2 patch anywhere -> nothing to render    */
} Osl1Kind;

/* File offset of the instrument pointer table: `instr_count` file-absolute
 * u32 record offsets, immediately followed by the records themselves. */
#define OSL1_INSTR_TAB_OFF  0x4E

/* Bytes of an instrument record medplay actually reads (up to adl[15] at
 * +0x3D). Used as the bounds check for a table entry; records are spaced
 * 0x3E apart in the corpus and carry their own length at +0x00. */
#define OSL1_INSTR_SPAN     0x3E

typedef struct {
    int      valid;         /* passed the table-consistency checks   */
    uint32_t offset;        /* file-absolute offset of this instrument */
    uint16_t len;           /* +0x00 total instrument data length     */
    uint16_t p1;            /* +0x04 param 1 (const 1 across corpus)  */
    uint16_t p2;            /* +0x06 param 2 (const 6 across corpus)  */
    char     name[21];      /* variant +0x00, 20-byte null-padded name */
    uint8_t  adl[16];       /* the OPL2 payload, zeroed when there is none */
    uint8_t  synth;         /* device code of the SELECTED variant     */
    uint8_t  n_variants;    /* record +0x04; >1 in only 5 corpus records */
    uint32_t paylen;        /* selected variant's payload length       */
    uint32_t payload_off;   /* file offset of that payload (0 = none)  */
    uint8_t  program;       /* payload +0x02 GM program (SCC1 records) */
    int8_t   transpose;     /* variant +0x18 signed transpose          */
    int8_t   finetune;      /* variant +0x16 signed "FineTune" field
                             * (range -99..+99). Distinct from transpose.
                             * NOT applied to pitch: every OSL replay driver
                             * (ADLIB.DEV, SBLAST.DEV) derives pitch from the
                             * note number via a fixed 12-per-octave table, so
                             * finetune has no runtime effect. Parsed for
                             * completeness/display only; 0 across the corpus. */
    int      fm;            /* the record has an OPL2 (device 0x02) variant,
                             * i.e. adl[] is a real patch and this instrument
                             * can be rendered. Roland/SCC1-only instruments
                             * are 0 and must stay silent. */

    /* ---- old formats only (see oldrld.c / oldalb.c); 0 for OSL1 -------- */
    uint8_t  def_volume;    /* default level 0..0x7F from the slot table's
                             * volume byte, doubled from its stored 0..0x3F.
                             * Both era drivers apply this at note-on:
                             * ADLIB.DRV @0x0508 (shl al,1, clamp 0x7F) and
                             * MED.EXE's B6 loader (RE-REPORT 11.4 step 3).
                             * A riding 0Ch overrides it for that note. */
    uint8_t  rhythm;        /* B4 editor record +0x26: 0 = melodic, 6 = bass
                             * drum, 7 = snare, 8 = tom, 9 = top cymbal,
                             * 10 = hi-hat. ADLIB.DRV runs the OPL2 in rhythm
                             * mode permanently (6 melodic + 5 percussion);
                             * medplay's opl_dev is melodic-only, so this is
                             * parsed and reported but not yet voiced as a
                             * percussion slot - see RLD.md section 10. */
} Instrument;

typedef struct {
    uint32_t block_off;     /* file-absolute offset of the block      */
    char     subtitle[17];  /* +0x00 16-byte null-padded subtitle     */
    uint8_t  restart_idx;   /* +0x10 loop-restart order (driver @0x417)    */
    uint16_t track_count;   /* +0x12 */
    uint16_t row_count;     /* +0x14 */
    uint16_t defaults[8];   /* +0x16 per-track defaults (0x7F7F=rest) */
    uint16_t checksum;      /* +0x26 */
    uint16_t ver_c;         /* +0x28 */
    uint16_t tempo;         /* +0x2A timer Hz, fed to set_tempo @0x422    */
    uint8_t  speed;         /* +0x2C initial ticks/row (driver @0x42C)    */
    uint8_t  order_count;   /* +0x4E */
    uint8_t  order[OSL1_MAX_ORDER];   /* +0x50 pattern order bytes    */
    uint32_t pos_ptr[OSL1_MAX_ORDER]; /* +0x150 file-absolute ptrs    */
} PatternBlock;

typedef struct {
    /* Old-format .RLD generation byte: 0xB4 (1991) or 0xB6 (1991-93), or 0
     * for a real OSL1 container. Chronological, not device-specific. */
    uint8_t      old_magic;
    /* Which instrument source actually supplied the patches (OldFmtFmSource,
     * resolved from AUTO). Meaningful only when old_magic != 0. */
    uint8_t      old_fm_source;
    uint16_t     old_rhythm_instr;  /* old-format instruments with rhythm != 0 */

    uint8_t      gen;           /* +0x07 format-generation (see OSL1_GEN_*) */
    uint8_t      version;       /* +0x04 */
    uint16_t     constant;      /* +0x05 */
    char         title[31];     /* +0x28 30-byte null-padded title    */
    uint32_t     block_off;     /* +0x48 pattern block offset         */
    uint16_t     instr_count;   /* +0x4C instrument count (table len) */
    /* Offset of the instrument pointer table. For OSL1 this is always 0x4E:
     * the table is `instr_count` file-absolute u32 record offsets starting
     * there, immediately followed by the records themselves (verified on
     * 304/322 corpus files: instr[0].offset == 0x4E + 4*instr_count exactly;
     * the remaining 18 have a NULL entry 0, i.e. an unused slot 0).
     *
     * This field used to be read as an "instr_size" from 0x4E, which is in
     * fact the low half of table entry 0. That made the whole table read one
     * entry late and silently discarded instrument 0 of every song - see the
     * note in osl1.c. Kept as a struct field because the old-format loaders
     * report a real fixed record size (256 for a B4/B6 instrument block,
     * 64 for an .ALB editor record). */
    uint16_t     instr_tab_off; /* OSL1: 0x4E. Old-format .RLD: 0.    */
    uint16_t     instr_size;    /* record span used for bounds checks */

    uint16_t     instr_total;   /* number of table entries parsed     */
    uint16_t     instr_valid;   /* number that passed validation      */
    uint16_t     fm_instr;      /* valid instruments with an FM patch */
    uint16_t     midi_instr;    /* valid MIDI/program-only instruments*/
    Osl1Kind     kind;          /* heuristic renderability class      */
    Instrument   instr[OSL1_MAX_INSTR];

    PatternBlock blk;

    uint8_t     *raw;           /* owned copy of the whole file       */
    size_t       size;
} Song;

/* Load and parse an OSL1 file. Returns 0 on success, non-zero on error
 * (errbuf, if non-NULL, receives a human-readable message). The Song owns a
 * heap copy of the file; call osl1_free() when done. */
int  osl1_load(const char *path, Song *song, char *errbuf, size_t errlen);
void osl1_free(Song *song);

/* Human-readable name for one of MED.EXE's five target devices. For
 * reference/labelling only - OSL1 stores no reliable device id (see the
 * Osl1Device comment); use osl1_kind_name() for what is actually renderable. */
const char *osl1_device_name(Osl1Device dev);

/* Human-readable name for the heuristic renderability classification. */
const char *osl1_kind_name(Osl1Kind kind);

/* Human-readable name for the format-generation byte (@0x07). */
const char *osl1_gen_name(uint8_t gen);

#endif /* MEDPLAY_OSL1_H */
