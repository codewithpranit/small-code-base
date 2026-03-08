#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping_tables.h"

/* ================================================================
 *  ACHARYA ENCODING PIPELINE
 *  Steps 6–11: Syllable construction, Acharya encode/decode,
 *              expansion, and verification.
 * ================================================================ */

/* --- Constants -------------------------------------------------- */
#define HALANT_BYTE   0xE8
#define NUKHTA_BYTE   0xE9
#define SCRIPT_SWITCH 0xEF
#define MAX_SYLLABLES 65536
#define MAX_SYL_LEN   16      /* max ISCII bytes in one syllable */

/* --- Syllable representation ------------------------------------ */
typedef struct {
    byte_t bytes[MAX_SYL_LEN]; /* constituent ISCII bytes          */
    int    len;                /* number of bytes in this syllable  */
    int    is_conjunct;        /* 1 if this is a conjunct cluster   */
} syllable_t;

/* --- Acharya code pair ------------------------------------------ */
typedef struct {
    byte_t hi;   /* high byte of 2-byte Acharya code */
    byte_t lo;   /* low  byte of 2-byte Acharya code */
} acharya_code_t;

/* --- Encode/decode tables (built during encoding) --------------- */
#define MAX_UNIQUE_SYLS 4096

/* encode table: maps syllable content → acharya code */
static syllable_t   enc_syls[MAX_UNIQUE_SYLS];
static acharya_code_t enc_codes[MAX_UNIQUE_SYLS];
static int           enc_count = 0;

/* decode table: maps acharya code → syllable */
static syllable_t    decode_table[65536];

/* ================================================================
 *  STEP 6 — SYLLABLE CONSTRUCTION
 *
 *  Scans the ISCII byte stream and groups bytes into syllables.
 *  A consonant followed by halant followed by another consonant
 *  forms a conjunct cluster treated as a single syllable.
 *  Vowels, matras, ASCII, and standalone characters are separate
 *  syllables.
 * ================================================================ */

static int is_consonant(byte_t b) {
    return (b >= 0xB3 && b <= 0xD8);
}

static int is_vowel(byte_t b) {
    return (b >= 0xA4 && b <= 0xB2);
}

static int is_matra(byte_t b) {
    return (b >= 0xDA && b <= 0xE7);
}

/* Check if two syllables have identical byte content */
static int syl_equal(const syllable_t *a, const syllable_t *b) {
    if (a->len != b->len) return 0;
    return memcmp(a->bytes, b->bytes, a->len) == 0;
}

static int construct_syllables(byte_t *iscii, int iscii_len,
                               syllable_t *syls, int max_syls)
{
    int nsyls = 0;
    int i = 0;

    while (i < iscii_len && nsyls < max_syls) {
        syllable_t *s = &syls[nsyls];
        memset(s, 0, sizeof(syllable_t));

        /* Script-switch bytes: treat the 2-byte sequence as one syl */
        if (iscii[i] == SCRIPT_SWITCH && i + 1 < iscii_len) {
            s->bytes[0] = iscii[i];
            s->bytes[1] = iscii[i + 1];
            s->len = 2;
            s->is_conjunct = 0;
            i += 2;
            nsyls++;
            continue;
        }

        /* Consonant: start potential conjunct cluster */
        if (is_consonant(iscii[i])) {
            s->bytes[s->len++] = iscii[i];
            i++;

            /* Absorb halant + consonant sequences (conjunct building) */
            while (i + 1 < iscii_len &&
                   iscii[i] == HALANT_BYTE &&
                   is_consonant(iscii[i + 1]))
            {
                s->bytes[s->len++] = iscii[i];     /* halant */
                s->bytes[s->len++] = iscii[i + 1]; /* next consonant */
                s->is_conjunct = 1;
                i += 2;
            }

            /* Absorb a trailing matra (vowel sign) if present */
            if (i < iscii_len && is_matra(iscii[i])) {
                s->bytes[s->len++] = iscii[i];
                i++;
            }

            /* Absorb halant at end (virama / pure halant form) */
            if (i < iscii_len && iscii[i] == HALANT_BYTE &&
                (i + 1 >= iscii_len || !is_consonant(iscii[i + 1])))
            {
                s->bytes[s->len++] = iscii[i];
                i++;
            }

            /* Absorb nukhta if present */
            if (i < iscii_len && iscii[i] == NUKHTA_BYTE) {
                s->bytes[s->len++] = iscii[i];
                i++;
            }

            nsyls++;
            continue;
        }

        /* Standalone vowel */
        if (is_vowel(iscii[i])) {
            s->bytes[s->len++] = iscii[i];
            i++;
            nsyls++;
            continue;
        }

        /* Matra appearing standalone */
        if (is_matra(iscii[i])) {
            s->bytes[s->len++] = iscii[i];
            i++;
            nsyls++;
            continue;
        }

        /* Halant appearing standalone (not after a consonant) */
        if (iscii[i] == HALANT_BYTE) {
            s->bytes[s->len++] = iscii[i];
            i++;
            nsyls++;
            continue;
        }

        /* Any other byte (ASCII, special, etc.) */
        s->bytes[s->len++] = iscii[i];
        i++;
        nsyls++;
    }

    return nsyls;
}

/* ================================================================
 *  STEP 7 — ACHARYA ENCODING
 *
 *  Each syllable is mapped to a 2-byte (16-bit) Acharya code.
 *  The encoding uses a collision-free sequential ID table:
 *    - Single-byte syllable:   hi = 0x00, lo = the byte itself
 *    - Script switch:          hi = 0xEF, lo = lang_code
 *    - Multi-byte syllable:    unique sequential code starting
 *                              from 0x0100, ensuring no collisions
 * ================================================================ */

static acharya_code_t find_or_assign_code(syllable_t *syl)
{
    acharya_code_t code;

    /* Single-byte syllable: direct mapping */
    if (syl->len == 1) {
        code.hi = 0x00;
        code.lo = syl->bytes[0];
        return code;
    }

    /* Script switch: direct mapping */
    if (syl->len == 2 && syl->bytes[0] == SCRIPT_SWITCH) {
        code.hi = 0xEF;
        code.lo = syl->bytes[1];
        return code;
    }

    /* Multi-byte syllable: look up in table or assign new code */
    for (int i = 0; i < enc_count; i++) {
        if (syl_equal(&enc_syls[i], syl)) {
            return enc_codes[i];
        }
    }

    /* Assign new unique code: 0x0100 + sequential ID */
    unsigned short new_id = 0x0100 + (unsigned short)enc_count;
    code.hi = (byte_t)(new_id >> 8);
    code.lo = (byte_t)(new_id & 0xFF);

    if (enc_count < MAX_UNIQUE_SYLS) {
        enc_syls[enc_count] = *syl;
        enc_codes[enc_count] = code;
        enc_count++;
    }

    return code;
}

static int encode_acharya(syllable_t *syls, int nsyls,
                          acharya_code_t *codes)
{
    /* Reset tables */
    memset(decode_table, 0, sizeof(decode_table));
    enc_count = 0;

    for (int i = 0; i < nsyls; i++) {
        codes[i] = find_or_assign_code(&syls[i]);

        /* Register in decode table */
        unsigned short key = ((unsigned short)codes[i].hi << 8) | codes[i].lo;
        decode_table[key] = syls[i];
    }
    return nsyls;
}

/* ================================================================
 *  STEP 8 — ACHARYA BYTE STREAM  (produced by encode step above)
 * ================================================================ */

/* ================================================================
 *  STEP 9 — ACHARYA DECODING
 *
 *  Each 2-byte Acharya code is looked up in the decode table
 *  to recover the syllable.
 * ================================================================ */

static int decode_acharya(acharya_code_t *codes, int ncodes,
                          syllable_t *out_syls)
{
    for (int i = 0; i < ncodes; i++) {
        unsigned short key = ((unsigned short)codes[i].hi << 8) | codes[i].lo;
        out_syls[i] = decode_table[key];
    }
    return ncodes;
}

/* ================================================================
 *  STEP 10 — SYLLABLE EXPANSION
 *
 *  Expand each syllable back into its constituent ISCII bytes.
 * ================================================================ */

static int expand_syllables(syllable_t *syls, int nsyls,
                            byte_t *out_stream, int max_out)
{
    int p = 0;
    for (int i = 0; i < nsyls && p < max_out; i++) {
        for (int j = 0; j < syls[i].len && p < max_out; j++) {
            out_stream[p++] = syls[i].bytes[j];
        }
    }
    return p;
}

/* ================================================================
 *  HELPERS — ISCII byte → printable Unicode character
 *
 *  For Devanagari the mapping is:
 *    ISCII 0xA4–0xB2 → vowels  U+0904–U+0914
 *    ISCII 0xB3–0xD8 → consonants U+0915–U+0939 (approx)
 *    ISCII 0xDA–0xE7 → matras  U+093E–U+094C
 *    ISCII 0xE8       → halant  U+094D
 *    ISCII 0xE9       → nukhta  U+093C
 *  For simplicity we use Devanagari as default script for display.
 * ================================================================ */

/* Map ISCII byte to a Unicode codepoint (Devanagari).
 * Returns 0 if no mapping found. */
static unsigned int iscii_byte_to_unicode(byte_t b)
{
    /* Vowels: ISCII 0xA4..0xB2 → U+0904..U+0914 (approx fit) */
    if (b >= 0xA4 && b <= 0xB2)
        return 0x0900 + (b - 0xA4 + 0x04);

    /* Consonants: ISCII 0xB3..0xD8 → offset into Unicode block */
    if (b >= 0xB3 && b <= 0xD8) {
        /* The mapping parallels the ISCII → Unicode standard offset */
        static const unsigned int con_map[] = {
            /* 0xB3 */ 0x0915, /* ka */
            /* 0xB4 */ 0x0916, /* kha */
            /* 0xB5 */ 0x0917, /* ga */
            /* 0xB6 */ 0x0918, /* gha */
            /* 0xB7 */ 0x0919, /* nga */
            /* 0xB8 */ 0x091A, /* cha */
            /* 0xB9 */ 0x091B, /* chha */
            /* 0xBA */ 0x091C, /* ja */
            /* 0xBB */ 0x091D, /* jha */
            /* 0xBC */ 0x091E, /* nja */
            /* 0xBD */ 0x091F, /* ta */
            /* 0xBE */ 0x0920, /* tta */
            /* 0xBF */ 0x0921, /* dda */
            /* 0xC0 */ 0x0922, /* ddha */
            /* 0xC1 */ 0x0923, /* nna */
            /* 0xC2 */ 0x0924, /* tha */
            /* 0xC3 */ 0x0925, /* ttha */
            /* 0xC4 */ 0x0926, /* da */
            /* 0xC5 */ 0x0927, /* dha */
            /* 0xC6 */ 0x0928, /* na */
            /* 0xC7 */ 0x092A, /* pa */
            /* 0xC8 */ 0x092B, /* pha */
            /* 0xC9 */ 0x092C, /* ba */
            /* 0xCA */ 0x092D, /* bha */
            /* 0xCB */ 0x092E, /* ma */
            /* 0xCC */ 0x092F, /* ya */
            /* 0xCD */ 0x0930, /* ra */
            /* 0xCE */ 0x0931, /* hard ra */
            /* 0xCF */ 0x0932, /* la */
            /* 0xD0 */ 0x0933, /* lla */
            /* 0xD1 */ 0x0934, /* llla */
            /* 0xD2 */ 0x0935, /* va (zha placeholder) */
            /* 0xD3 */ 0x0935, /* va */
            /* 0xD4 */ 0x0936, /* sha */
            /* 0xD5 */ 0x0937, /* sha (hard) */
            /* 0xD6 */ 0x0938, /* sa */
            /* 0xD7 */ 0x0939, /* ha */
            /* 0xD8 */ 0x0939, /* ha (alt) */
        };
        return con_map[b - 0xB3];
    }

    /* Matras: ISCII 0xDA..0xE7 → U+093E..U+094C */
    if (b >= 0xDA && b <= 0xE7)
        return 0x093E + (b - 0xDA);

    /* Halant */
    if (b == 0xE8) return 0x094D;

    /* Nukhta */
    if (b == 0xE9) return 0x093C;

    /* ASCII range */
    if (b < 0x80) return b;

    return 0;
}

/* Print a Unicode codepoint as UTF-8 */
static void print_unicode_char(unsigned int cp)
{
    if (cp == 0) {
        printf("?");
        return;
    }
    if (cp < 0x80) {
        putchar(cp);
    } else if (cp < 0x800) {
        putchar(0xC0 | (cp >> 6));
        putchar(0x80 | (cp & 0x3F));
    } else {
        putchar(0xE0 | (cp >> 12));
        putchar(0x80 | ((cp >> 6) & 0x3F));
        putchar(0x80 | (cp & 0x3F));
    }
}

/* Print a syllable as readable characters */
static void print_syllable_chars(syllable_t *syl)
{
    for (int j = 0; j < syl->len; j++) {
        unsigned int cp = iscii_byte_to_unicode(syl->bytes[j]);
        print_unicode_char(cp);
    }
}

/* ================================================================
 *  STEP 6 REPORT — Print syllable construction output
 * ================================================================ */
static void print_syllable_construction(syllable_t *syls, int nsyls)
{
    printf("\n============================================================\n");
    printf("         SYLLABLE PROCESSING (ACHARYA PIPELINE)\n");
    printf("============================================================\n");

    printf("\n\n[6] SYLLABLE CONSTRUCTION\n");
    printf("------------------------------------------------------------\n");
    printf("Character Stream Analysis\n\n");

    int syl_num = 1;
    for (int i = 0; i < nsyls; i++) {
        /* Skip script switches in display */
        if (syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;

        if (syls[i].is_conjunct) {
            /* Show conjunct formation */
            for (int j = 0; j < syls[i].len; j++) {
                if (j > 0) printf(" + ");
                unsigned int cp = iscii_byte_to_unicode(syls[i].bytes[j]);
                print_unicode_char(cp);
            }
            printf(" → conjunct cluster\n");
        }
    }

    printf("\nDetected Syllables:\n\n");
    for (int i = 0; i < nsyls; i++) {
        if (syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;

        printf("Syllable %d : ", syl_num++);
        print_syllable_chars(&syls[i]);
        if (syls[i].is_conjunct)
            printf("  (conjunct)");
        printf("\n");
    }
}

/* ================================================================
 *  STEP 7 REPORT — Print Acharya encoding output
 * ================================================================ */
static void print_acharya_encoding(syllable_t *syls, int nsyls,
                                   acharya_code_t *codes)
{
    printf("\n\n[7] ACHARYA ENCODING\n");
    printf("------------------------------------------------------------\n");
    printf("Syllable → Acharya Code (16-bit)\n\n");

    int acharya_bytes = 0;
    for (int i = 0; i < nsyls; i++) {
        if (syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;
        print_syllable_chars(&syls[i]);
        printf(" → [0x%02X 0x%02X]\n", codes[i].hi, codes[i].lo);
        acharya_bytes += 2;
    }

    printf("\nAcharya Encoded Byte Stream\n");
    for (int i = 0; i < nsyls; i++) {
        if (syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;
        printf("[%02X %02X] ", codes[i].hi, codes[i].lo);
    }

    printf("\n\nOutput Size : %d bytes\n", acharya_bytes);
}

/* ================================================================
 *  STEP 8/9 REPORT — Print Acharya decoding output
 * ================================================================ */
static void print_acharya_decoding(syllable_t *dec_syls, int nsyls,
                                   acharya_code_t *codes)
{
    printf("\n\n[8] ACHARYA DECODING\n");
    printf("------------------------------------------------------------\n");
    printf("Acharya Code → Syllable\n\n");

    for (int i = 0; i < nsyls; i++) {
        if (dec_syls[i].len == 2 && dec_syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;
        printf("[%02X %02X] → ", codes[i].hi, codes[i].lo);
        print_syllable_chars(&dec_syls[i]);
        printf("\n");
    }

    printf("\nDecoded Syllables\n");
    for (int i = 0; i < nsyls; i++) {
        if (dec_syls[i].len == 2 && dec_syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;
        print_syllable_chars(&dec_syls[i]);
        printf(" ");
    }
    printf("\n");
}

/* ================================================================
 *  STEP 10 REPORT — Print syllable expansion output
 * ================================================================ */
static void print_syllable_expansion(syllable_t *dec_syls, int nsyls)
{
    printf("\n\n[9] SYLLABLE EXPANSION\n");
    printf("------------------------------------------------------------\n");
    printf("Syllable → Character Sequence\n\n");

    for (int i = 0; i < nsyls; i++) {
        if (dec_syls[i].len == 2 && dec_syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;

        if (dec_syls[i].is_conjunct || dec_syls[i].len > 1) {
            print_syllable_chars(&dec_syls[i]);
            printf(" → ");
            for (int j = 0; j < dec_syls[i].len; j++) {
                if (j > 0) printf(" ");
                unsigned int cp = iscii_byte_to_unicode(dec_syls[i].bytes[j]);
                print_unicode_char(cp);
            }
            printf("\n");
        } else {
            print_syllable_chars(&dec_syls[i]);
            printf(" → ");
            print_syllable_chars(&dec_syls[i]);
            printf("\n");
        }
    }

    printf("\nReconstructed Character Stream\n");
    for (int i = 0; i < nsyls; i++) {
        if (dec_syls[i].len == 2 && dec_syls[i].bytes[0] == SCRIPT_SWITCH)
            continue;
        for (int j = 0; j < dec_syls[i].len; j++) {
            unsigned int cp = iscii_byte_to_unicode(dec_syls[i].bytes[j]);
            print_unicode_char(cp);
            printf(" ");
        }
    }
    printf("\n");
}

/* ================================================================
 *  STEP 11 — VERIFICATION
 *
 *  Compare original ISCII stream with expanded (reconstructed)
 *  ISCII stream for byte-exact match.
 * ================================================================ */
static void print_verification(byte_t *original, int orig_len,
                               byte_t *reconstructed, int recon_len)
{
    printf("\n\n============================================================\n");
    printf("             CORRECTNESS VERIFICATION\n");
    printf("============================================================\n");

    printf("\nOriginal Character Stream:\n");
    for (int i = 0; i < orig_len; i++) {
        if (original[i] == SCRIPT_SWITCH && i + 1 < orig_len) {
            i++; /* skip script switch */
            continue;
        }
        unsigned int cp = iscii_byte_to_unicode(original[i]);
        print_unicode_char(cp);
        printf(" ");
    }

    printf("\n\nReconstructed Character Stream:\n");
    for (int i = 0; i < recon_len; i++) {
        if (reconstructed[i] == SCRIPT_SWITCH && i + 1 < recon_len) {
            i++; /* skip script switch */
            continue;
        }
        unsigned int cp = iscii_byte_to_unicode(reconstructed[i]);
        print_unicode_char(cp);
        printf(" ");
    }

    /* Compare */
    int match = (orig_len == recon_len);
    if (match) {
        for (int i = 0; i < orig_len; i++) {
            if (original[i] != reconstructed[i]) {
                match = 0;
                break;
            }
        }
    }

    printf("\n\nVerification Result:\n");
    if (match) {
        printf("✔ SUCCESS — Decoded output matches original input\n");
    } else {
        printf("✘ MISMATCH — Decoded output differs from original input\n");
        printf("  Original length    : %d bytes\n", orig_len);
        printf("  Reconstructed length: %d bytes\n", recon_len);
        /* Show first mismatch */
        int min_len = orig_len < recon_len ? orig_len : recon_len;
        for (int i = 0; i < min_len; i++) {
            if (original[i] != reconstructed[i]) {
                printf("  First mismatch at byte %d: 0x%02X vs 0x%02X\n",
                       i, original[i], reconstructed[i]);
                break;
            }
        }
    }
}

/* ================================================================
 *  FINAL ENCODING SUMMARY
 * ================================================================ */
static void print_final_summary(long input_size, int iscii_len,
                                int acharya_bytes, int verified)
{
    printf("\n\n============================================================\n");
    printf("                  FINAL ENCODING SUMMARY\n");
    printf("============================================================\n");
    printf("\nInput Encoding        : UTF-8");
    printf("\nIntermediate Encoding : ISCII-91");
    printf("\nFinal Encoding        : Acharya (2-byte syllable encoding)");
    printf("\n\nInput Size            : %ld bytes", input_size);
    printf("\nISCII Output Size     : %d bytes", iscii_len);
    printf("\nAcharya Output Size   : %d bytes", acharya_bytes);
    printf("\n\nIntegrity Check       : %s", verified ? "PASSED" : "FAILED");
    printf("\nPipeline Status       : %s", verified ? "SUCCESSFUL" : "FAILED");
    printf("\n\n============================================================\n");
}

/* ================================================================
 *  PUBLIC ENTRY POINT
 * ================================================================ */
void run_acharya_pipeline(byte_t *iscii_stream, int iscii_len,
                          const char *original_text)
{
    /* Allocate working buffers */
    syllable_t    *syls     = (syllable_t *)   malloc(MAX_SYLLABLES * sizeof(syllable_t));
    acharya_code_t *codes   = (acharya_code_t *)malloc(MAX_SYLLABLES * sizeof(acharya_code_t));
    syllable_t    *dec_syls = (syllable_t *)   malloc(MAX_SYLLABLES * sizeof(syllable_t));
    byte_t        *recon    = (byte_t *)       malloc(iscii_len * 4);

    if (!syls || !codes || !dec_syls || !recon) {
        printf("[Error] Acharya pipeline: memory allocation failed\n");
        free(syls); free(codes); free(dec_syls); free(recon);
        return;
    }

    /* Step 6: Syllable Construction */
    int nsyls = construct_syllables(iscii_stream, iscii_len, syls, MAX_SYLLABLES);
    print_syllable_construction(syls, nsyls);

    /* Step 7: Acharya Encoding */
    int ncodes = encode_acharya(syls, nsyls, codes);
    print_acharya_encoding(syls, nsyls, codes);

    /* Step 8 already produced above (byte stream) */

    /* Step 9: Acharya Decoding */
    decode_acharya(codes, ncodes, dec_syls);
    print_acharya_decoding(dec_syls, nsyls, codes);

    /* Step 10: Syllable Expansion */
    int recon_len = expand_syllables(dec_syls, nsyls, recon, iscii_len * 4);
    print_syllable_expansion(dec_syls, nsyls);

    /* Step 11: Verification */
    print_verification(iscii_stream, iscii_len, recon, recon_len);

    /* Count Acharya bytes for summary */
    int acharya_bytes = 0;
    for (int i = 0; i < nsyls; i++) {
        if (!(syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH))
            acharya_bytes += 2;
    }

    int verified = (iscii_len == recon_len) &&
                   (memcmp(iscii_stream, recon, iscii_len) == 0);

    /* Compute original file size from the UTF-8 text */
    long input_size = (long)strlen(original_text);

    print_final_summary(input_size, iscii_len, acharya_bytes, verified);

    free(syls);
    free(codes);
    free(dec_syls);
    free(recon);
}
