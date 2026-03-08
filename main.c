#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Direct inclusion of converter_logic.c.
 * Ensure converter_logic.c now outputs raw ISCII bytes per syllable
 * instead of the old Unique ID system.
 */
#include "converter_logic.c"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/**
 * UPDATED: Display the header for the Direct Byte-Mapping Encoder.
 */
void print_header() {
    printf("\n==================================================================\n");
    printf("   UNIVERSAL 9-SCRIPT ENCODER: DIRECT ISCII BYTE MAPPING          \n");
    printf("==================================================================\n");
}

void print_instructions() {
    printf("SYSTEM CAPABILITIES:\n");
    printf("1. Supports File Input up to 1,000,000+ characters.\n");
    printf("2. 9-Script Support: Devanagari, Bengali, Gurmukhi, Gujarati, \n");
    printf("   Oriya, Tamil, Telugu, Kannada, Malayalam.\n");
    printf("3. Direct Encoding: UTF-8 -> 8-bit Raw ISCII Bytes.\n");
    printf("4. Byte Verification: Reports exact ISCII hex values per syllable.\n\n");
    printf("INSTRUCTIONS:\n");
    printf("1. Provide the filename (e.g., testbench.txt) when prompted.\n");
    printf("2. Type 'EXIT' to terminate the session.\n");
    printf("------------------------------------------------------------------\n\n");
}

int main() {
    char filename_input[512];

    /* * NOTE: init_reverse_map() has been removed as we are now mapping 
     * UTF-8 characters directly to ISCII bytes without intermediate IDs.
     */
    
    print_header();
    print_instructions();

    while (1) {
        printf("ENTER TEST FILE NAME > ");
        
        // Capture input including potential spaces in filenames
        if (!fgets(filename_input, sizeof(filename_input), stdin)) {
            break;
        }

        // Strip the trailing newline or carriage return
        filename_input[strcspn(filename_input, "\r\n")] = 0;

        // Exit Condition
        if (strcasecmp(filename_input, "EXIT") == 0) {
            break;
        }

        // Prevent empty input processing
        if (strlen(filename_input) == 0) {
            continue;
        }

        /* * LOGIC PIPELINE EXECUTION:
         * 1. Opens the .txt file in Binary Mode.
         * 2. Maps Unicode Code Points to relative ISCII offsets.
         * 3. Injects 0xEF Script Switches where necessary.
         * 4. Groups and displays raw ISCII bytes [0xXX] for verification.
         */
        run_file_test_bench(filename_input);
        
        printf("\n--- Ready for next test file (or type 'EXIT') ---\n");
    }

    printf("\nExiting Encoder. Dhanyawaad!\n");
    return 0;
}
