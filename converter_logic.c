#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping_tables.h"
#include "acharya_logic.h"

/* Constants defining ISCII behavior */
#define HALANT_BYTE   0xE8
#define NUKHTA_BYTE   0xE9
#define SCRIPT_SWITCH 0xEF

/* -------------------------------------------------------------------
 *  Script name helper
 * ------------------------------------------------------------------- */
static const char* script_name_from_code(byte_t code) {
    switch (code) {
        case 0x42: return "Devanagari";
        case 0x43: return "Bengali";
        case 0x44: return "Gurmukhi";
        case 0x45: return "Gujarati";
        case 0x46: return "Oriya";
        case 0x47: return "Tamil";
        case 0x48: return "Telugu";
        case 0x49: return "Kannada";
        case 0x4A: return "Malayalam";
        default:   return "Unknown";
    }
}

/* Unicode range string for a script code */
static const char* script_unicode_range(byte_t code) {
    switch (code) {
        case 0x42: return "U+0900 \xe2\x80\x93 U+097F";
        case 0x43: return "U+0980 \xe2\x80\x93 U+09FF";
        case 0x44: return "U+0A00 \xe2\x80\x93 U+0A7F";
        case 0x45: return "U+0A80 \xe2\x80\x93 U+0AFF";
        case 0x46: return "U+0B00 \xe2\x80\x93 U+0B7F";
        case 0x47: return "U+0B80 \xe2\x80\x93 U+0BFF";
        case 0x48: return "U+0C00 \xe2\x80\x93 U+0C7F";
        case 0x49: return "U+0C80 \xe2\x80\x93 U+0CFF";
        case 0x4A: return "U+0D00 \xe2\x80\x93 U+0D7F";
        default:   return "Unknown";
    }
}

/* -------------------------------------------------------------------
 *  PART 2: UNIVERSAL SCRIPT TO ISCII
 *  Directly maps Unicode Code Points to ISCII bytes using relative
 *  offsets.
 * ------------------------------------------------------------------- */
int convert_to_iscii(const char* input_text, byte_t* out_stream) {
    int p = 0;
    byte_t current_script = 0x42; /* Default Devanagari */

    const unsigned char* text = (const unsigned char*)input_text;
    for (int i = 0; text[i] != '\0'; ) {
        unsigned int cp = 0;
        int len = 0;

        /* UTF-8 Decoding logic */
        if (text[i] < 0x80) {
            cp = text[i]; len = 1;
        }
        else if ((text[i] & 0xE0) == 0xC0) {
            cp = ((text[i] & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2;
        }
        else if ((text[i] & 0xF0) == 0xE0) {
            cp = ((text[i] & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3;
        }
        else { i++; continue; }

        /* Script Detection */
        byte_t target_script = 0;
        if      (cp >= 0x0900 && cp <= 0x097F) target_script = 0x42;
        else if (cp >= 0x0980 && cp <= 0x09FF) target_script = 0x43;
        else if (cp >= 0x0A00 && cp <= 0x0A7F) target_script = 0x44;
        else if (cp >= 0x0A80 && cp <= 0x0AFF) target_script = 0x45;
        else if (cp >= 0x0B00 && cp <= 0x0B7F) target_script = 0x46;
        else if (cp >= 0x0B80 && cp <= 0x0BFF) target_script = 0x47;
        else if (cp >= 0x0C00 && cp <= 0x0C7F) target_script = 0x48;
        else if (cp >= 0x0C80 && cp <= 0x0CFF) target_script = 0x49;
        else if (cp >= 0x0D00 && cp <= 0x0D7F) target_script = 0x4A;

        /* Handle Script Switching */
        if (target_script != 0 && target_script != current_script) {
            out_stream[p++] = SCRIPT_SWITCH;
            out_stream[p++] = target_script;
            current_script = target_script;
        }

        if (target_script != 0) {
            int offset = cp & 0x7F;
            if      (offset >= 0x15 && offset <= 0x39) out_stream[p++] = imli_to_iscii_con[offset - 0x14];
            else if (offset >= 0x3E && offset <= 0x4C) out_stream[p++] = imli_to_iscii_vow_matras[offset - 0x3D];
            else if (offset == 0x4D)                   out_stream[p++] = HALANT_BYTE;
            else if (offset >= 0x05 && offset <= 0x14) out_stream[p++] = imli_to_iscii_vow[offset - 0x04];
            else if (offset == 0x3C)                   out_stream[p++] = NUKHTA_BYTE;
        } else {
            out_stream[p++] = (byte_t)cp; /* Standard ASCII */
        }
        i += len;
    }
    return p;
}

/* -------------------------------------------------------------------
 *  Detect the first script in UTF-8 text
 * ------------------------------------------------------------------- */
static byte_t detect_first_script(const char* text) {
    const unsigned char* t = (const unsigned char*)text;
    for (int i = 0; t[i] != '\0'; ) {
        unsigned int cp = 0;
        int len = 0;
        if (t[i] < 0x80)                      { cp = t[i]; len = 1; }
        else if ((t[i] & 0xE0) == 0xC0)       { cp = ((t[i]&0x1F)<<6)|(t[i+1]&0x3F); len = 2; }
        else if ((t[i] & 0xF0) == 0xE0)       { cp = ((t[i]&0x0F)<<12)|((t[i+1]&0x3F)<<6)|(t[i+2]&0x3F); len = 3; }
        else { i++; continue; }

        if      (cp >= 0x0900 && cp <= 0x097F) return 0x42;
        else if (cp >= 0x0980 && cp <= 0x09FF) return 0x43;
        else if (cp >= 0x0A00 && cp <= 0x0A7F) return 0x44;
        else if (cp >= 0x0A80 && cp <= 0x0AFF) return 0x45;
        else if (cp >= 0x0B00 && cp <= 0x0B7F) return 0x46;
        else if (cp >= 0x0B80 && cp <= 0x0BFF) return 0x47;
        else if (cp >= 0x0C00 && cp <= 0x0C7F) return 0x48;
        else if (cp >= 0x0C80 && cp <= 0x0CFF) return 0x49;
        else if (cp >= 0x0D00 && cp <= 0x0D7F) return 0x4A;
        i += len;
    }
    return 0x42; /* Default Devanagari */
}

/* -------------------------------------------------------------------
 *  Detect whether there is a mid-file script switch
 * ------------------------------------------------------------------- */
static int detect_script_switches(const char* text,
                                  byte_t first_script,
                                  byte_t *second_script) {
    const unsigned char* t = (const unsigned char*)text;
    int found_first = 0;
    *second_script = 0;
    for (int i = 0; t[i] != '\0'; ) {
        unsigned int cp = 0;
        int len = 0;
        if (t[i] < 0x80)                { cp = t[i]; len = 1; }
        else if ((t[i] & 0xE0) == 0xC0) { cp = ((t[i]&0x1F)<<6)|(t[i+1]&0x3F); len = 2; }
        else if ((t[i] & 0xF0) == 0xE0) { cp = ((t[i]&0x0F)<<12)|((t[i+1]&0x3F)<<6)|(t[i+2]&0x3F); len = 3; }
        else { i++; continue; }

        byte_t s = 0;
        if      (cp >= 0x0900 && cp <= 0x097F) s = 0x42;
        else if (cp >= 0x0980 && cp <= 0x09FF) s = 0x43;
        else if (cp >= 0x0A00 && cp <= 0x0A7F) s = 0x44;
        else if (cp >= 0x0A80 && cp <= 0x0AFF) s = 0x45;
        else if (cp >= 0x0B00 && cp <= 0x0B7F) s = 0x46;
        else if (cp >= 0x0B80 && cp <= 0x0BFF) s = 0x47;
        else if (cp >= 0x0C00 && cp <= 0x0C7F) s = 0x48;
        else if (cp >= 0x0C80 && cp <= 0x0CFF) s = 0x49;
        else if (cp >= 0x0D00 && cp <= 0x0D7F) s = 0x4A;

        if (s != 0) {
            if (!found_first) {
                found_first = 1;
            } else if (s != first_script) {
                *second_script = s;
                return 1;
            }
        }
        i += len;
    }
    return 0;
}

/* -------------------------------------------------------------------
 *  Print raw UTF-8 bytes of the file, one codepoint per line
 *  e.g. "E0 A4 95\n"
 * ------------------------------------------------------------------- */
static void print_raw_utf8_bytes(const char* text) {
    printf("\nREADING THE TESTBENCH FILE[UTF-8 BYTES]\n");
    printf("------------------------------------------------------------\n");

    const unsigned char* t = (const unsigned char*)text;
    for (int i = 0; t[i] != '\0'; ) {
        int len = 0;
        if      (t[i] < 0x80)                { len = 1; }
        else if ((t[i] & 0xE0) == 0xC0)      { len = 2; }
        else if ((t[i] & 0xF0) == 0xE0)      { len = 3; }
        else if ((t[i] & 0xF8) == 0xF0)      { len = 4; }
        else { i++; continue; }

        for (int j = 0; j < len; j++) {
            if (j > 0) printf(" ");
            printf("%02X", t[i + j]);
        }
        printf("\n");
        i += len;
    }
}

/* -------------------------------------------------------------------
 *  Print UTF-8 decoding trace for first few characters
 * ------------------------------------------------------------------- */
static void print_utf8_trace(const char* text, int max_chars) {
    printf("\n\n[3] UTF-8 DECODING TRACE\n");
    printf("------------------------------------------------------------\n");
    printf("UTF-8 Bytes \xe2\x86\x92 Unicode Codepoints\n\n");

    const unsigned char* t = (const unsigned char*)text;
    int count = 0;
    for (int i = 0; t[i] != '\0' && count < max_chars; ) {
        unsigned int cp = 0;
        int len = 0;
        if (t[i] < 0x80)                { cp = t[i]; len = 1; }
        else if ((t[i] & 0xE0) == 0xC0) { cp = ((t[i]&0x1F)<<6)|(t[i+1]&0x3F); len = 2; }
        else if ((t[i] & 0xF0) == 0xE0) { cp = ((t[i]&0x0F)<<12)|((t[i+1]&0x3F)<<6)|(t[i+2]&0x3F); len = 3; }
        else { i++; continue; }

        printf("[");
        for (int j = 0; j < len; j++) {
            if (j > 0) printf(" ");
            printf("0x%02X", t[i + j]);
        }
        printf("] \xe2\x86\x92 U+%04X \xe2\x86\x92 ", cp);

        /* Print the actual character as UTF-8 */
        for (int j = 0; j < len; j++)
            putchar(t[i + j]);
        printf("\n");

        i += len;
        count++;
    }
    if (count >= max_chars)
        printf("... (showing first %d characters)\n", max_chars);
}

/* -------------------------------------------------------------------
 *  Print ISCII encoding trace — bracket style [B3] [E8] ...
 * ------------------------------------------------------------------- */
static void print_iscii_encoding_trace(byte_t *stream, int len) {
    printf("\n\n[4] ISCII ENCODING\n");
    printf("------------------------------------------------------------\n");
    printf("ISCII Byte Stream\n");

    for (int i = 0; i < len; i++) {
        printf("[%02X] ", stream[i]);
    }

    printf("\n\nOutput Size : %d bytes\n", len);
}

/* -------------------------------------------------------------------
 *  PART 3: FILE-BASED TEST BENCH
 *  Full multi-stage encoding pipeline.
 * ------------------------------------------------------------------- */
void run_file_test_bench(const char* filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("[Error] Cannot open: %s\n", filename);
        return;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char*)malloc(length + 1);
    byte_t *temp_iscii = (byte_t*)malloc(length * 2 + 10);

    if (!buffer || !temp_iscii) {
        printf("[Error] Memory allocation failed\n");
        if (buffer) free(buffer);
        if (temp_iscii) free(temp_iscii);
        fclose(file);
        return;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    /* ============================================================ */
    printf("\n============================================================\n");
    printf("             MULTI-STAGE ENCODING PIPELINE REPORT\n");
    printf("============================================================\n");

    /* [1] INPUT FILE VERIFICATION */
    printf("\n\n[1] INPUT FILE VERIFICATION\n");
    printf("------------------------------------------------------------\n");
    printf("Input File        : %s\n", filename);
    printf("File Size         : %ld bytes\n", length);
    printf("Encoding Detected : UTF-8\n");
    printf("File Status       : Successfully loaded\n");

    /* [2] SCRIPT DETECTION */
    byte_t first_script = detect_first_script(buffer);
    byte_t second_script = 0;
    int has_switch = detect_script_switches(buffer, first_script, &second_script);

    printf("\n\n[2] SCRIPT DETECTION\n");
    printf("------------------------------------------------------------\n");
    printf("Detected Script Block : %s\n", script_name_from_code(first_script));
    printf("Unicode Range         : %s\n", script_unicode_range(first_script));
    if (has_switch)
        printf("Script Switch         : Detected (\xe2\x86\x92 %s)\n",
               script_name_from_code(second_script));
    else
        printf("Script Switch         : None detected\n");

    /* READING THE TESTBENCH FILE [UTF-8 BYTES] */
    print_raw_utf8_bytes(buffer);

    /* [3] UTF-8 DECODING TRACE */
    print_utf8_trace(buffer, 20);

    /* [4] ISCII ENCODING */
    int iscii_len = convert_to_iscii(buffer, temp_iscii);
    print_iscii_encoding_trace(temp_iscii, iscii_len);

    /* [7-...] ACHARYA PIPELINE (includes sections 7, 8, verification, summary) */
    run_acharya_pipeline(temp_iscii, iscii_len, buffer, length);

    free(buffer);
    free(temp_iscii);
}
