/*
Summary: Whole-file read helper used for JPEG input and the vault file.
*/

#ifndef FILEIO_H
#define FILEIO_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: Read an entire file into a newly allocated heap buffer.
Inputs:  p_path - file to read.
         p_out  - receives a heap buffer (cap == len == file size).
Outputs: 0 on success, -1 if the file is missing, empty, or unreadable.
         Caller releases p_out->p_data with free().
*/
int fileio_read_all(const char * p_path, byte_buf_t * p_out);

#ifdef __cplusplus
}
#endif

#endif
