/**********************************************************************
 * @file    xr_args.h
 * @brief   Checked command-line value parsing.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * atoi() and atof() report no errors: they return 0 for text that is not
 * a number at all, and their behaviour on overflow is undefined. That
 * turns a typo like "--fov 6O" (letter O) into a silent 0-degree field
 * of view rather than a complaint. These wrappers use strtol/strtof,
 * reject trailing garbage and out-of-range values, and let the caller
 * fail loudly instead.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef XR_ARGS_H
#define XR_ARGS_H

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Parse a whole decimal integer.
 *
 * The entire string must be consumed, so "12abc" and "" are rejected.
 *
 * @param[in]  p_text  NUL-terminated candidate text.
 * @param[out] p_out   Receives the value; untouched on failure.
 *
 * @return  0 on success, -1 if p_text is not a valid int.
 **********************************************************************/
int xr_args_parse_int(const char * p_text, int * p_out);

/**********************************************************************
 * @brief  Parse a finite float.
 *
 * The entire string must be consumed. Infinities and NaN are rejected,
 * since every float option here feeds geometry maths where they would
 * poison the whole matrix chain.
 *
 * @param[in]  p_text  NUL-terminated candidate text.
 * @param[out] p_out   Receives the value; untouched on failure.
 *
 * @return  0 on success, -1 if p_text is not a finite float.
 **********************************************************************/
int xr_args_parse_float(const char * p_text, float * p_out);

#ifdef __cplusplus
}
#endif

#endif /* XR_ARGS_H */
