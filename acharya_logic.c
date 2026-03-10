#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mapping_tables.h"

/* ================================================================
 *  ACHARYA ENCODING PIPELINE
 *  Steps 7+: Syllable construction, Acharya encode/decode,
 *              ISCII verification, UTF-8 roundtrip verification.
 * ================================================================ */

/* --- Constants -------------------------------------------------- */
#define HALANT_BYTE   0xE8
#define NUKHTA_BYTE   0xE9
#define SCRIPT_SWITCH 0xEF
#define MAX_SYLLABLES 65536
#define MAX_SYL_LEN   16

/* --- Syllable representation ------------------------------------ */
typedef struct {
    byte_t bytes[MAX_SYL_LEN];
    int    len;
    int    is_conjunct;
} syllable_t;

/* --- Acharya code pair ------------------------------------------ */
typedef struct {
    byte_t hi;
    byte_t lo;
} acharya_code_t;

/* --- Encode/decode tables --------------------------------------- */
#define MAX_UNIQUE_SYLS 4096
static syllable_t    enc_syls[MAX_UNIQUE_SYLS];
static acharya_code_t enc_codes[MAX_UNIQUE_SYLS];
static int           enc_count = 0;
static syllable_t    decode_table[65536];

/* ================================================================
 *  MANUAL UTF-8 ENCODE (user-provided formula)
 *  Encodes an array of Unicode codepoints (uint32_t) to UTF-8 bytes.
 *  Returns the number of bytes written.
 * ================================================================ */
static size_t utf8_encode(const uint32_t *codepoints, size_t len,
                          unsigned char *out)
{
    size_t out_len = 0;
    size_t i;
    for (i = 0; i < len; ++i) {
        uint32_t cp = codepoints[i];
        if (cp <= 0x7F) {
            out[out_len++] = (unsigned char)cp;
        } else if (cp <= 0x7FF) {
            out[out_len++] = (unsigned char)(0xC0 | (cp >> 6));
            out[out_len++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out[out_len++] = (unsigned char)(0xE0 | (cp >> 12));
            out[out_len++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[out_len++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out[out_len++] = (unsigned char)(0xF0 | (cp >> 18));
            out[out_len++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            out[out_len++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[out_len++] = (unsigned char)(0x80 | (cp & 0x3F));
        }
    }
    return out_len;
}

/* ================================================================
 *  ISCII → Unicode reverse map
 *
 *  Built programmatically from the forward mapping tables to
 *  guarantee a byte-perfect inverse of convert_to_iscii().
 *
 *  convert_to_iscii() computes:
 *    offset = cp & 0x7F
 *    consonant: imli_to_iscii_con[offset - 0x14]
 *    vow matra: imli_to_iscii_vow_matras[offset - 0x3D]
 *    vowel:     imli_to_iscii_vow[offset - 0x04]
 *    halant:    0xE8 (offset == 0x4D)
 *    nukhta:    0xE9 (offset == 0x3C)
 *
 *  Inverse: for each forward-table index i that maps to ISCII byte b,
 *    iscii_to_cp[b] = 0x0900 | (base_offset + i)
 * ================================================================ */

static unsigned int iscii_to_cp[256];
static int iscii_reverse_ready = 0;

static void build_iscii_reverse_map(void)
{
    int i;
    if (iscii_reverse_ready) return;

    for (i = 0; i < 256; i++) iscii_to_cp[i] = 0;

    /* ASCII passthrough */
    for (i = 0; i < 0x80; i++) iscii_to_cp[i] = (unsigned int)i;

    /* Consonants: index i, cp offset = 0x14 + i */
    for (i = 1; i <= 54; i++) {
        byte_t iscii = imli_to_iscii_con[i];
        if (iscii >= 0x80) {
            iscii_to_cp[iscii] = 0x0900u | ((unsigned int)(0x14 + i) & 0xFFu);
        }
    }

    /* Standalone vowels: index i, cp offset = 0x04 + i */
    for (i = 0; i < 15; i++) {
        byte_t iscii = imli_to_iscii_vow[i];
        if (iscii >= 0x80) {
            iscii_to_cp[iscii] = 0x0900u | (unsigned int)(0x04 + i);
        }
    }

    /* Vowel matras: index i (1..14), cp offset = 0x3D + i */
    for (i = 1; i < 15; i++) {
        byte_t iscii = imli_to_iscii_vow_matras[i];
        if (iscii >= 0x80) {
            iscii_to_cp[iscii] = 0x0900u | (unsigned int)(0x3D + i);
        }
    }

    /* Special markers */
    iscii_to_cp[0xE8] = 0x094D; /* Halant / Virama */
    iscii_to_cp[0xE9] = 0x093C; /* Nukhta          */

    iscii_reverse_ready = 1;
}

static unsigned int iscii_byte_to_unicode(byte_t b)
{
    if (!iscii_reverse_ready) build_iscii_reverse_map();
    return iscii_to_cp[b];
}

/* ================================================================
 *  SYLLABLE CONSTRUCTION
 * ================================================================ */

static int is_consonant(byte_t b) { return (b >= 0xB3 && b <= 0xD8); }
static int is_vowel(byte_t b)     { return (b >= 0xA4 && b <= 0xB2); }
static int is_matra(byte_t b)     { return (b >= 0xDA && b <= 0xE7); }

static int syl_equal(const syllable_t *a, const syllable_t *b) {
    if (a->len != b->len) return 0;
    return memcmp(a->bytes, b->bytes, (size_t)a->len) == 0;
}

static int construct_syllables(byte_t *iscii, int iscii_len,
                               syllable_t *syls, int max_syls)
{
    int nsyls = 0, i = 0;
    while (i < iscii_len && nsyls < max_syls) {
        syllable_t *s = &syls[nsyls];
        memset(s, 0, sizeof(syllable_t));

        if (iscii[i] == SCRIPT_SWITCH && i + 1 < iscii_len) {
            s->bytes[0] = iscii[i]; s->bytes[1] = iscii[i+1];
            s->len = 2; i += 2; nsyls++; continue;
        }

        if (is_consonant(iscii[i])) {
            s->bytes[s->len++] = iscii[i++];
            while (i + 1 < iscii_len && iscii[i] == HALANT_BYTE && is_consonant(iscii[i+1])) {
                s->bytes[s->len++] = iscii[i];
                s->bytes[s->len++] = iscii[i+1];
                s->is_conjunct = 1; i += 2;
            }
            if (i < iscii_len && is_matra(iscii[i]))   s->bytes[s->len++] = iscii[i++];
            if (i < iscii_len && iscii[i] == HALANT_BYTE &&
                (i+1 >= iscii_len || !is_consonant(iscii[i+1])))
                    s->bytes[s->len++] = iscii[i++];
            if (i < iscii_len && iscii[i] == NUKHTA_BYTE) s->bytes[s->len++] = iscii[i++];
            nsyls++; continue;
        }

        if (is_vowel(iscii[i]) || is_matra(iscii[i]) || iscii[i] == HALANT_BYTE) {
            s->bytes[s->len++] = iscii[i++]; nsyls++; continue;
        }

        s->bytes[s->len++] = iscii[i++]; nsyls++;
    }
    return nsyls;
}

/* ================================================================
 *  ACHARYA ENCODING
 * ================================================================ */

static acharya_code_t find_or_assign_code(syllable_t *syl)
{
    acharya_code_t code;
    int i;

    if (syl->len == 1) { code.hi = 0x00; code.lo = syl->bytes[0]; return code; }
    if (syl->len == 2 && syl->bytes[0] == SCRIPT_SWITCH) {
        code.hi = 0xEF; code.lo = syl->bytes[1]; return code;
    }
    for (i = 0; i < enc_count; i++) {
        if (syl_equal(&enc_syls[i], syl)) return enc_codes[i];
    }
    {
        unsigned short new_id = (unsigned short)(0x0100 + enc_count);
        code.hi = (byte_t)(new_id >> 8);
        code.lo = (byte_t)(new_id & 0xFF);
    }
    if (enc_count < MAX_UNIQUE_SYLS) {
        enc_syls[enc_count] = *syl;
        enc_codes[enc_count] = code;
        enc_count++;
    }
    return code;
}

static int encode_acharya(syllable_t *syls, int nsyls, acharya_code_t *codes)
{
    int i;
    memset(decode_table, 0, sizeof(decode_table));
    enc_count = 0;
    for (i = 0; i < nsyls; i++) {
        codes[i] = find_or_assign_code(&syls[i]);
        decode_table[((unsigned short)codes[i].hi << 8) | codes[i].lo] = syls[i];
    }
    return nsyls;
}

/* ================================================================
 *  ACHARYA DECODING
 * ================================================================ */
static int decode_acharya(acharya_code_t *codes, int ncodes, syllable_t *out)
{
    int i;
    for (i = 0; i < ncodes; i++) {
        out[i] = decode_table[((unsigned short)codes[i].hi << 8) | codes[i].lo];
    }
    return ncodes;
}

/* ================================================================
 *  SYLLABLE EXPANSION
 * ================================================================ */
static int expand_syllables(syllable_t *syls, int nsyls, byte_t *out, int max)
{
    int p = 0, i, j;
    for (i = 0; i < nsyls && p < max; i++)
        for (j = 0; j < syls[i].len && p < max; j++)
            out[p++] = syls[i].bytes[j];
    return p;
}

/* ================================================================
 *  [7] ACHARYA ENCODING REPORT
 *  Prints the byte stream as "HHLL  HHLL  ..." (hi+lo concatenated)
 * ================================================================ */
static int print_acharya_encoding(syllable_t *syls, int nsyls,
                                  acharya_code_t *codes)
{
    int i, acharya_bytes = 0, first = 1;
    printf("\n\n[7] ACHARYA ENCODING\n");
    printf("------------------------------------------------------------\n");
    printf("\nAcharya Encoded Byte Stream\n");
    for (i = 0; i < nsyls; i++) {
        if (syls[i].len == 2 && syls[i].bytes[0] == SCRIPT_SWITCH) continue;
        if (!first) printf("  ");
        printf("%02X%02X", codes[i].hi, codes[i].lo);
        first = 0;
        acharya_bytes += 2;
    }
    printf("\n\nOutput Size : %d bytes\n", acharya_bytes);
    return acharya_bytes;
}

/* ================================================================
 *  [8] ACHARYA → ISCII + CORRECTNESS VERIFICATION (ISCII level)
 * ================================================================ */
static void print_acharya_to_iscii_verification(
    byte_t *orig_iscii, int orig_len,
    byte_t *recon_iscii, int recon_len)
{
    int i, match;
    printf("\n\n[8] ACHARYA -> ISCII BYTES DECODING\n");
    printf("\n============================================================\n");
    printf("             CORRECTNESS VERIFICATION\n");
    printf("============================================================\n");

    printf("Original Byte Stream:\n\n");
    for (i = 0; i < orig_len; i++) printf("[%02X] ", orig_iscii[i]);

    printf("\n\nReconstructed Byte Stream :\n\n");
    for (i = 0; i < recon_len; i++) printf("[%02X] ", recon_iscii[i]);
    printf("\n");

    match = (orig_len == recon_len);
    if (match) {
        for (i = 0; i < orig_len; i++) {
            if (orig_iscii[i] != recon_iscii[i]) { match = 0; break; }
        }
    }
    printf("\n\nVerification Result:\n");
    if (match)
        printf("\xe2\x9c\x94 SUCCESS \xe2\x80\x94 Decoded output binary stream matches original input\n");
    else
        printf("\xe2\x9c\x98 MISMATCH \xe2\x80\x94 Decoded output differs from original input\n");
}

/* ================================================================
 *  UTF-8 ROUNDTRIP VERIFICATION
 *
 *  ISCII bytes → Unicode codepoints (via iscii_byte_to_unicode)
 *  → UTF-8 bytes (via manual utf8_encode) → compare vs original
 * ================================================================ */
static void print_utf8_roundtrip_verification(
    const char *original_utf8,
    byte_t     *recon_iscii,
    int         recon_len)
{
    int i;
    size_t ri, orig_len_bytes, recon_utf8_len;
    int match;
    const unsigned char *orig = (const unsigned char *)original_utf8;

    /* Step A: ISCII bytes → codepoints */
    uint32_t *codepoints = (uint32_t *)malloc((size_t)recon_len * sizeof(uint32_t));
    if (!codepoints) return;
    int ncp = 0;
    for (i = 0; i < recon_len; i++) {
        if (recon_iscii[i] == SCRIPT_SWITCH && i + 1 < recon_len) { i++; continue; }
        unsigned int cp = iscii_byte_to_unicode(recon_iscii[i]);
        if (cp != 0) codepoints[ncp++] = (uint32_t)cp;
    }

    /* Step B: codepoints → UTF-8 using manual formula */
    unsigned char *recon_utf8 = (unsigned char *)malloc((size_t)ncp * 4 + 1);
    if (!recon_utf8) { free(codepoints); return; }
    recon_utf8_len = utf8_encode(codepoints, (size_t)ncp, recon_utf8);
    free(codepoints);

    printf("\n============================================================\n");
    printf("             CORRECTNESS VERIFICATION\n");
    printf("============================================================\n");

    /* Display original bytes per codepoint */
    printf("Orginal UTF-8 byte stream after reading the input txt file:\n");
    for (i = 0; orig[i] != '\0'; ) {
        int len = 0;
        if      (orig[i] < 0x80)              len = 1;
        else if ((orig[i] & 0xE0) == 0xC0)    len = 2;
        else if ((orig[i] & 0xF0) == 0xE0)    len = 3;
        else if ((orig[i] & 0xF8) == 0xF0)    len = 4;
        else { i++; continue; }
        for (int j = 0; j < len; j++) { if (j > 0) printf(" "); printf("%02X", orig[i+j]); }
        printf("\n");
        i += len;
    }

    /* Display reconstructed bytes per codepoint */
    printf("\nReconstructed  UTF-8 byte stream using the formula:\n");
    for (ri = 0; ri < recon_utf8_len; ) {
        int len = 0;
        if      (recon_utf8[ri] < 0x80)              len = 1;
        else if ((recon_utf8[ri] & 0xE0) == 0xC0)    len = 2;
        else if ((recon_utf8[ri] & 0xF0) == 0xE0)    len = 3;
        else if ((recon_utf8[ri] & 0xF8) == 0xF0)    len = 4;
        else { ri++; continue; }
        for (int j = 0; j < len; j++) { if (j > 0) printf(" "); printf("%02X", recon_utf8[ri+j]); }
        printf("\n");
        ri += (size_t)len;
    }

    /* Byte-exact comparison */
    orig_len_bytes = 0;
    while (orig[orig_len_bytes] != '\0') orig_len_bytes++;
    match = (orig_len_bytes == recon_utf8_len);
    if (match) {
        for (ri = 0; ri < orig_len_bytes; ri++) {
            if (orig[ri] != recon_utf8[ri]) { match = 0; break; }
        }
    }

    printf("\nVerification Result:\n");
    if (match)
        printf("\xe2\x9c\x94 SUCCESS \xe2\x80\x94 Decoded output binary stream matches original input\n");
    else
        printf("\xe2\x9c\x98 MISMATCH \xe2\x80\x94 Decoded output differs from original input\n");

    free(recon_utf8);
}

/* ================================================================
 *  FINAL ENCODING SUMMARY
 * ================================================================ */
static void print_final_summary(long input_size, int iscii_len,
                                int acharya_bytes, int verified)
{
    printf("\n============================================================\n");
    printf("                  FINAL ENCODING SUMMARY\n");
    printf("============================================================\n");
    printf("\nInput Encoding        : UTF-8\n");
    printf("Intermediate Encoding : ISCII-91\n");
    printf("Final Encoding        : Acharya (2-byte syllable encoding)\n");
    printf("\nInput Size            : %ld bytes\n", input_size);
    printf("ISCII Output Size     : %d bytes\n", iscii_len);
    printf("Acharya Output Size   : %d bytes\n", acharya_bytes);
    printf("\nIntegrity Check       : %s\n", verified ? "PASSED" : "FAILED");
    printf("Pipeline Status       : %s\n", verified ? "SUCCESSFUL" : "FAILED");
    printf("\n============================================================\n\n");
}

/* ================================================================
 *  PUBLIC ENTRY POINT
 * ================================================================ */
void run_acharya_pipeline(byte_t *iscii_stream, int iscii_len,
                          const char *original_text, long original_size)
{
    syllable_t    *syls     = (syllable_t *)   malloc(MAX_SYLLABLES * sizeof(syllable_t));
    acharya_code_t *codes   = (acharya_code_t *)malloc(MAX_SYLLABLES * sizeof(acharya_code_t));
    syllable_t    *dec_syls = (syllable_t *)   malloc(MAX_SYLLABLES * sizeof(syllable_t));
    byte_t        *recon    = (byte_t *)       malloc((size_t)iscii_len * 4 + 4);

    if (!syls || !codes || !dec_syls || !recon) {
        printf("[Error] Acharya pipeline: memory allocation failed\n");
        free(syls); free(codes); free(dec_syls); free(recon);
        return;
    }

    /* Syllable Construction (internal) */
    int nsyls = construct_syllables(iscii_stream, iscii_len, syls, MAX_SYLLABLES);

    /* [7] Acharya Encoding */
    int ncodes = encode_acharya(syls, nsyls, codes);
    int acharya_bytes = print_acharya_encoding(syls, nsyls, codes);

    /* Acharya Decoding (internal) */
    decode_acharya(codes, ncodes, dec_syls);

    /* Syllable Expansion → reconstructed ISCII */
    int recon_len = expand_syllables(dec_syls, nsyls, recon, iscii_len * 4);

    /* [8] ISCII correctness verification */
    print_acharya_to_iscii_verification(iscii_stream, iscii_len, recon, recon_len);

    /* UTF-8 roundtrip verification */
    print_utf8_roundtrip_verification(original_text, recon, recon_len);

    /* Overall correctness */
    int verified = (iscii_len == recon_len) &&
                   (memcmp(iscii_stream, recon, (size_t)iscii_len) == 0);

    /* Final summary */
    print_final_summary(original_size, iscii_len, acharya_bytes, verified);

    free(syls); free(codes); free(dec_syls); free(recon);
}
