/*
Summary: A file of sealed drawings. Each record pairs a drawing
         fingerprint with an opaque sealed blob from envelope_seal().
         The vault knows nothing about keys: the fingerprint selects a
         record (fuzzy match), and the caller opens the blob.

         On-disk format, integers big-endian:
           magic "SDV4"
           repeated: aad[8] view_count[1]
                     view_count x { view_len[4] fingerprint[view_len] }
                     blob_len[4] blob[blob_len]
         aad is the primary hash of the first view; several views of the
         same object (multi-shot enrolment) share one record.
*/

#ifndef VAULT_H
#define VAULT_H

#include "bytes.h"
#include "fingerprint.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VAULT_FOUND         (0)
#define VAULT_NO_MATCH      (1)
#define VAULT_ERROR         (-1)
#define VAULT_MAX_BLOB      (2097152)
#define VAULT_MAX_VIEWS     (8)

/*
Summary: What to look for.
Inputs:  p_live   - fingerprint of the current view.
         max_dist - hash-score threshold for texture-poor objects (see
                    fingerprint_compare).
Outputs: None.
*/
typedef struct
{
    const fingerprint_t * p_live;
    int                   max_dist;
} vault_query_t;

/*
Summary: The views to store in one record.
Inputs:  p_views - array of fingerprints, first one supplies the aad.
         count   - number of views, 1..VAULT_MAX_VIEWS.
Outputs: None.
*/
typedef struct
{
    const fingerprint_t * p_views;
    int                   count;
} vault_views_t;

/*
Summary: The best matching record.
Inputs:  None.
Outputs: phash   - the record's aad (primary hash of its first view).
         dist    - hash score of the closest view (lower is closer).
         inliers - keypoint inliers of the closest view.
         index   - record position in the file, 0-based.
         blob    - heap copy of the sealed blob; release with
                   vault_match_free().
*/
typedef struct
{
    uint8_t    phash[PHASH_BYTES];
    int        dist;
    int        inliers;
    int        index;
    byte_buf_t blob;
} vault_match_t;

/*
Summary: Append a sealed record to the vault, creating the file if new.
Inputs:  p_path  - vault file.
         p_views - fingerprints of the object; the first supplies the aad.
         blob    - sealed bytes from envelope_seal().
Outputs: 0 on success, -1 on I/O failure or oversize blob.
*/
int vault_store(const char * p_path, const vault_views_t * p_views,
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
Summary: Describe one record for listing.
Inputs:  None.
Outputs: index      - record position, 0-based.
         views      - number of stored views.
         blob_bytes - sealed blob size.
*/
typedef struct
{
    int    index;
    int    views;
    size_t blob_bytes;
} vault_entry_t;

/*
Summary: Enumerate the records in a vault.
Inputs:  p_path    - vault file.
         p_entries - array receiving up to cap entries.
         cap       - capacity of p_entries.
Outputs: Number of records in the file (may exceed cap), -1 if the
         file is corrupt, 0 if absent.
*/
int vault_list(const char * p_path, vault_entry_t * p_entries, int cap);

/*
Summary: Remove one record by index, rewriting the file.
Inputs:  p_path - vault file.
         index  - record position from vault_list().
Outputs: 0 on success, -1 if the index is out of range or on I/O
         failure.
*/
int vault_forget(const char * p_path, int index);

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
