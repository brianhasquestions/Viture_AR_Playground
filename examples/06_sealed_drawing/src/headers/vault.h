/*
Summary: A file of sealed drawings. Each record pairs a drawing
         fingerprint with an opaque sealed blob from envelope_seal().
         The vault knows nothing about keys: the fingerprint selects a
         record (fuzzy match), and the caller opens the blob.

         On-disk format, integers big-endian:
           magic "SDV3"
           repeated: primary[8] count[1] variants[count*8]
                     colour[COLORSIG_BINS] blob_len[4] blob[blob_len]
*/

#ifndef VAULT_H
#define VAULT_H

#include "bytes.h"
#include "phash.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VAULT_FOUND         (0)
#define VAULT_NO_MATCH      (1)
#define VAULT_ERROR         (-1)
#define VAULT_MAX_BLOB      (2097152)

/*
Summary: What to look for.
Inputs:  p_live   - fingerprint set of the current view.
         max_dist - largest effective distance (phash_set_match) still
                    treated as a match.
Outputs: None.
*/
typedef struct
{
    const phash_set_t * p_live;
    int                 max_dist;
} vault_query_t;

/*
Summary: The best matching record.
Inputs:  None.
Outputs: phash - the primary fingerprint as stored (the sealing aad).
         dist  - Hamming distance from the query.
         blob  - heap copy of the sealed blob; release with
                 vault_match_free().
*/
typedef struct
{
    uint8_t    phash[PHASH_BYTES];
    int        dist;
    byte_buf_t blob;
} vault_match_t;

/*
Summary: Append a sealed record to the vault, creating the file if new.
Inputs:  p_path - vault file.
         p_set  - object fingerprint set (primary is the sealing aad).
         blob   - sealed bytes from envelope_seal().
Outputs: 0 on success, -1 on I/O failure or oversize blob.
*/
int vault_store(const char * p_path, const phash_set_t * p_set,
                byte_span_t blob);

/*
Summary: Find the record whose fingerprint is closest to the query.
Inputs:  p_path  - vault file.
         p_query - fingerprint and threshold.
         p_match - receives the winning record on VAULT_FOUND.
Outputs: VAULT_FOUND, VAULT_NO_MATCH (no file or nothing within
         threshold), or VAULT_ERROR on a corrupt file.
*/
int vault_find(const char * p_path, const vault_query_t * p_query,
               vault_match_t * p_match);

/*
Summary: Release the heap blob inside a match. Safe on NULL or empty.
Inputs:  p_match - match to clear.
Outputs: None.
*/
void vault_match_free(vault_match_t * p_match);

#ifdef __cplusplus
}
#endif

#endif
