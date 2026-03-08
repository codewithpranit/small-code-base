#ifndef ACHARYA_LOGIC_H
#define ACHARYA_LOGIC_H

#include "mapping_tables.h"

/**
 * Run the full Acharya encoding pipeline (Steps 6–11):
 *   Syllable construction → Acharya encoding → Acharya decoding
 *   → Syllable expansion → Verification
 *
 * @param iscii_stream   The ISCII byte stream produced by the encoder
 * @param iscii_len      Number of bytes in the ISCII stream
 * @param original_text  The original UTF-8 input text (for verification)
 */
void run_acharya_pipeline(byte_t *iscii_stream, int iscii_len,
                          const char *original_text);

#endif /* ACHARYA_LOGIC_H */
