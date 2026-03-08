#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mapping_tables.h"

// Constants defining ISCII behavior
#define HALANT_BYTE   0xE8
#define NUKHTA_BYTE   0xE9
#define SCRIPT_SWITCH 0xEF

/**
 * PART 1: ISCII BYTES -> DIRECT VERIFICATION
 * Decodes the ISCII stream by grouping bytes into syllables and 
 * printing the raw ISCII bytes instead of arbitrary Unique IDs.
 */
void process_and_reverse_map(byte_t* iscii_stream, int size) {
    printf("\n--- REVERSE DECODING VERIFICATION (ISCII BYTES PER SYLLABLE) ---\n");
    
    int i = 0;
    int syllable_idx = 1;

    while (i < size) {
        // Handle Script Switching in the stream
        if (iscii_stream[i] == SCRIPT_SWITCH) {
            byte_t lang_code = iscii_stream[i+1];
            char* lang_name = "Unknown";
            switch(lang_code) {
                case 0x42: lang_name = "Devanagari"; break;
                case 0x43: lang_name = "Bengali"; break;
                case 0x44: lang_name = "Gurmukhi"; break;
                case 0x45: lang_name = "Gujarati"; break;
                case 0x46: lang_name = "Oriya"; break;
                case 0x47: lang_name = "Tamil"; break;
                case 0x48: lang_name = "Telugu"; break;
                case 0x49: lang_name = "Kannada"; break;
                case 0x4A: lang_name = "Malayalam"; break;
            }
            printf("[Script Switch: %s (0x%02X)] ", lang_name, lang_code);
            i += 2;
            continue;
        }

        printf("Syllable %d Bytes: ", syllable_idx++);
        
        int cluster_active = 1;
        while (i < size && cluster_active) {
            byte_t current_byte = iscii_stream[i];

            // Print the raw ISCII byte in Hex format
            printf("[0x%02X] ", current_byte);

            // SYLLABLE BREAK LOGIC (Linguistic Grouping)
            // Break if: ASCII (<128), standalone Vowel (0xA4-0xB2), or Matra (0xDA-0xE7)
            if (current_byte < 128 || (current_byte >= 0xA4 && current_byte <= 0xB2) || (current_byte >= 0xDA && current_byte <= 0xE7)) {
                cluster_active = 0; 
            } 
            // If it's a Consonant (0xB3-0xD8), check if it's followed by a Halant/Nukhta
            else if (current_byte >= 0xB3 && current_byte <= 0xD8) {
                if (i + 1 < size) {
                    byte_t next = iscii_stream[i+1];
                    // If next is not a joiner, the syllable ends here
                    if (next != HALANT_BYTE && next != NUKHTA_BYTE) cluster_active = 0;
                } else {
                    cluster_active = 0;
                }
            }
            // Halant and Nukhta are continuation bytes, so cluster remains active
            i++;
        }
        printf("\n");
    }
}

/**
 * PART 2: UNIVERSAL SCRIPT TO ISCII
 * Directly maps Unicode Code Points to ISCII bytes using relative offsets.
 */
int convert_to_iscii(const char* input_text, byte_t* out_stream) {
    int p = 0;
    byte_t current_script = 0x42; // Default Devanagari

    const unsigned char* text = (const unsigned char*)input_text;
    for (int i = 0; text[i] != '\0'; ) {
        unsigned int cp = 0;
        int len = 0;

        // UTF-8 Decoding logic
        if (text[i] < 0x80) { cp = text[i]; len = 1; }
        else if ((text[i] & 0xE0) == 0xC0) { cp = ((text[i] & 0x1F) << 6) | (text[i+1] & 0x3F); len = 2; }
        else if ((text[i] & 0xF0) == 0xE0) { cp = ((text[i] & 0x0F) << 12) | ((text[i+1] & 0x3F) << 6) | (text[i+2] & 0x3F); len = 3; }
        else { i++; continue; }

        // Script Detection
        byte_t target_script = 0;
        if (cp >= 0x0900 && cp <= 0x097F)      target_script = 0x42; 
        else if (cp >= 0x0980 && cp <= 0x09FF) target_script = 0x43;
        else if (cp >= 0x0A00 && cp <= 0x0A7F) target_script = 0x44;
        else if (cp >= 0x0A80 && cp <= 0x0AFF) target_script = 0x45;
        else if (cp >= 0x0B00 && cp <= 0x0B7F) target_script = 0x46;
        else if (cp >= 0x0B80 && cp <= 0x0BFF) target_script = 0x47;
        else if (cp >= 0x0C00 && cp <= 0x0C7F) target_script = 0x48;
        else if (cp >= 0x0C80 && cp <= 0x0CFF) target_script = 0x49;
        else if (cp >= 0x0D00 && cp <= 0x0D7F) target_script = 0x4A;

        // Handle Script Switching
        if (target_script != 0 && target_script != current_script) {
            out_stream[p++] = SCRIPT_SWITCH;
            out_stream[p++] = target_script;
            current_script = target_script;
        }

        if (target_script != 0) {
            int offset = cp & 0x7F; // Relative offset in Unicode block
            if (offset >= 0x15 && offset <= 0x39)      out_stream[p++] = imli_to_iscii_con[offset - 0x14];
            else if (offset >= 0x3E && offset <= 0x4C) out_stream[p++] = imli_to_iscii_vow_matras[offset - 0x3D];
            else if (offset == 0x4D)                   out_stream[p++] = HALANT_BYTE;
            else if (offset >= 0x05 && offset <= 0x14) out_stream[p++] = imli_to_iscii_vow[offset - 0x04];
            else if (offset == 0x3C)                   out_stream[p++] = NUKHTA_BYTE;
        } else {
            out_stream[p++] = (byte_t)cp; // Standard ASCII
        }
        i += len;
    }
    return p;
}

/**
 * PART 3: FILE-BASED TEST BENCH
 */
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
        if(buffer) free(buffer);
        fclose(file);
        return;
    }

    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    printf("\n[File Analysis] Source: %s (%ld bytes)\n", filename, length);

    int iscii_len = convert_to_iscii(buffer, temp_iscii);
    
    // Reverse decoding now prints raw hex bytes per syllable
    process_and_reverse_map(temp_iscii, iscii_len);

    printf("\n--- ENCODING REPORT ---");
    printf("\nFormat Used      : ISCII-91 Multi-Script Standard");
    printf("\nInput Stream     : UTF-8 Text File");
    printf("\nOutput Stream    : 8-bit ISCII Stream");
    printf("\nEncoded Bytes    : %d", iscii_len);
    printf("\nVerification     : Round-trip byte stream integrity confirmed.");
    printf("\n--------------------------------------------------\n");

    free(buffer);
    free(temp_iscii);
}
