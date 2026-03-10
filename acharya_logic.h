#ifndef ACHARYA_LOGIC_H
#define ACHARYA_LOGIC_H

#include "mapping_tables.h"

/**
 * Run the full Acharya encoding pipeline (Steps 7 onward):
 *   Syllable construction → Acharya encoding → Acharya decoding
 *   → ISCII correctness verification → UTF-8 roundtrip verification
 *   → Final summary
 *
 * @param iscii_stream   The ISCII byte stream produced by the encoder
 * @param iscii_len      Number of bytes in the ISCII stream
 * @param original_text  The original UTF-8 input text (for verification)
 * @param original_size  File size in bytes (for final summary)
 */
void run_acharya_pipeline(byte_t *iscii_stream, int iscii_len,
                          const char *original_text, long original_size);

#endif /* ACHARYA_LOGIC_H */
