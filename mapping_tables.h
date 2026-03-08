#ifndef MAPPING_TABLES_H
#define MAPPING_TABLES_H

typedef unsigned char byte_t;

/**
 * UNIQUE VERIFICATION ID RANGES:
 * 0 - 127   : ASCII Pass-through (Unique mapping for English/Spaces/Punctuation)
 * 301 - 360 : Indic Consonants (Universal across scripts)
 * 401 - 430 : Standalone Vowels
 * 501 - 530 : Vowel Matras (Dependent Signs)
 * 99        : Halant / Virama (Special Linker)
 * 54        : Nukhta (Special Modifier)
 */

// --- 1. FORWARD MAPPING TABLES (IMLI ID -> ISCII BYTE) ---

// IMLI Consonant IDs to ISCII bytes
static const byte_t imli_to_iscii_con[] = {
    0,    0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 
    0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 
    0xC6, 0x20, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 
    0xCF, 0x20, 0x20, 0x20, 0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 
    0xD6, 0xD7, 0xD8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 
    0x20, 0x20, 0x20, 0x20, 0xE9 
};

// IMLI Standalone Vowel IDs to ISCII bytes
static const byte_t imli_to_iscii_vow[] = {
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 
    0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2
};

// IMLI Vowel Matra IDs to ISCII bytes
static const byte_t imli_to_iscii_vow_matras[] = {
    0x00, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE0, 
    0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7
};

// --- 2. REVERSE MAPPING ENGINE (ISCII BYTE -> UNIQUE ID) ---

static int iscii_to_imli_id[256];
static int reverse_map_ready = 0;

/**
 * initialize_reverse_mapping()
 * Populates a 1:1 lookup table for bit-perfect decoding verification.
 * Handles millions of characters by ensuring no two characters share an ID.
 */
static void initialize_reverse_mapping() {
    if (reverse_map_ready) return;

    // 1. UNIQUE ASCII MAPPING (0-127)
    // Every space [32], period [46], and letter is uniquely identified.
    for (int i = 0; i < 128; i++) {
        iscii_to_imli_id[i] = i; 
    }
    
    // Initialize the upper half (128-255) to 0 before populating
    for (int i = 128; i < 256; i++) {
        iscii_to_imli_id[i] = 0;
    }

    // 2. MAP INDIC CONSONANTS (ID Range 301 - 354)
    for (int i = 1; i <= 54; i++) {
        byte_t val = imli_to_iscii_con[i];
        if (val >= 128) { // Only map if it's in the ISCII extended range
            iscii_to_imli_id[val] = 300 + i; 
        }
    }

    // 3. MAP STANDALONE VOWELS (ID Range 401 - 415)
    for (int i = 0; i < 15; i++) {
        byte_t val = imli_to_iscii_vow[i];
        if (val >= 128) {
            iscii_to_imli_id[val] = 401 + i;
        }
    }

    // 4. MAP DEPENDENT MATRAS (ID Range 501 - 515)
    for (int i = 1; i < 15; i++) {
        byte_t val = imli_to_iscii_vow_matras[i];
        if (val >= 128) {
            iscii_to_imli_id[val] = 501 + i;
        }
    }

    // 5. SPECIAL STRUCTURAL MARKERS
    iscii_to_imli_id[0xE8] = 99; // Halant
    iscii_to_imli_id[0xE9] = 54; // Nukhta
    
    // Note: Script Switch (0xEF) is a control byte, not a syllable character,
    // so it is handled by the state machine in converter_logic.c.

    reverse_map_ready = 1;
}

#endif // MAPPING_TABLES_H