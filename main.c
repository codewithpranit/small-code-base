#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* Forward declarations from converter_logic.c */
extern void run_file_test_bench(const char* filename);

/**
 * Display the header for the Multi-Stage Encoding Pipeline.
 */
void print_header() {
    printf("\n==================================================================\n");
    printf("   MULTI-STAGE ENCODING PIPELINE: UTF-8 → ISCII → ACHARYA       \n");
    printf("==================================================================\n");
}

void print_instructions() {
    printf("SYSTEM CAPABILITIES:\n");
    printf("1. Supports File Input up to 1,000,000+ characters.\n");
    printf("2. 9-Script Support: Devanagari, Bengali, Gurmukhi, Gujarati, \n");
    printf("   Oriya, Tamil, Telugu, Kannada, Malayalam.\n");
    printf("3. Multi-Stage Encoding Pipeline:\n");
    printf("     UTF-8 → Unicode → ISCII → Syllables → Acharya (2-byte)\n");
    printf("4. Full round-trip verification: encode → decode → verify.\n\n");
    printf("INSTRUCTIONS:\n");
    printf("1. Provide the filename (e.g., testbench10.txt) when prompted.\n");
    printf("2. Type 'EXIT' to terminate the session.\n");
    printf("------------------------------------------------------------------\n\n");
}

int main() {
    char filename_input[512];

    print_header();
    print_instructions();

    while (1) {
        printf("ENTER TEST FILE NAME > ");

        if (!fgets(filename_input, sizeof(filename_input), stdin)) {
            break;
        }

        /* Strip the trailing newline or carriage return */
        filename_input[strcspn(filename_input, "\r\n")] = 0;

        /* Exit Condition */
        if (strcasecmp(filename_input, "EXIT") == 0) {
            break;
        }

        /* Prevent empty input processing */
        if (strlen(filename_input) == 0) {
            continue;
        }

        /*
         * FULL PIPELINE EXECUTION:
         * 1. Opens the .txt file in Binary Mode
         * 2. Decodes UTF-8 → Unicode Code Points
         * 3. Encodes to ISCII (with script switching)
         * 4. Decodes ISCII back for verification
         * 5. Constructs syllables (conjunct clustering)
         * 6. Encodes to Acharya (2-byte per syllable)
         * 7. Decodes Acharya back to syllables
         * 8. Expands syllables to characters
         * 9. Verifies round-trip correctness
         */
        run_file_test_bench(filename_input);

        printf("\n--- Ready for next test file (or type 'EXIT') ---\n");
    }

    printf("\nExiting Encoder. Dhanyawaad!\n");
    return 0;
}
