/**********************************************************************
 * @file    device_scan.c
 * @brief   POSIX sysfs enumeration of connected VITURE XR glasses.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * Implementation notes:
 *  - Uses only POSIX (opendir/readdir) plus the Linux sysfs pseudo
 *    filesystem, so no vendor USB dependency is required to enumerate.
 *  - Every function has a single point of exit reached via goto so
 *    that all heap allocations and file handles are released in one
 *    place, per the Barr-C cleanup idiom.
 *  - Comparisons use Yoda notation and all locals are initialised to
 *    their type's null equivalent (NULL / 0).
 **********************************************************************/

#include "device_scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Root of the Linux USB device tree in sysfs. */
#define SYSFS_USB_ROOT      "/sys/bus/usb/devices"

/* Upper bound for constructed sysfs path strings. */
#define SYSFS_PATH_MAX      (512U)

/* Small scratch buffer for reading a single hex attribute value. */
#define SYSFS_ATTR_MAX      (64U)

/**********************************************************************
 * @brief  Read a hexadecimal integer from a sysfs attribute file.
 *
 * @param[in]  p_path   Absolute path to the sysfs attribute.
 * @param[out] p_value  Receives the parsed value on success.
 *
 * @return  0 on success, -1 on any failure.
 **********************************************************************/
static int read_hex_attr(const char * p_path, int * p_value)
{
    int      result   = -1;
    FILE *   p_file    = NULL;
    char *   p_buffer  = NULL;
    char *   p_end     = NULL;
    long     parsed    = 0L;

    if ((NULL == p_path) || (NULL == p_value))
    {
        goto cleanup;
    }

    /* Heap allocation preferred over a stack array for the scratch    */
    /* buffer, per project convention.                                 */
    p_buffer = (char *)calloc(SYSFS_ATTR_MAX, sizeof(char));
    if (NULL == p_buffer)
    {
        goto cleanup;
    }

    p_file = fopen(p_path, "r");
    if (NULL == p_file)
    {
        goto cleanup;
    }

    if (NULL == fgets(p_buffer, (int)SYSFS_ATTR_MAX, p_file))
    {
        goto cleanup;
    }

    parsed = strtol(p_buffer, &p_end, 16);
    if (p_end == p_buffer)
    {
        /* No digits were converted. */
        goto cleanup;
    }

    *p_value = (int)parsed;
    result   = 0;

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }
    if (NULL != p_buffer)
    {
        free(p_buffer);
    }
    return result;
}

/**********************************************************************
 * @brief  Scan the sysfs USB tree for a connected VITURE device.
 *
 * See device_scan.h for the full contract.
 **********************************************************************/
int device_scan_find_glasses(int (*pf_is_valid)(int product_id),
                             int * p_product_id)
{
    int             result    = -1;
    DIR *           p_dir      = NULL;
    struct dirent * p_entry    = NULL;
    char *          p_vid_path = NULL;
    char *          p_pid_path = NULL;

    if (NULL == p_product_id)
    {
        result = -2;
        goto cleanup;
    }

    *p_product_id = DEVICE_SCAN_NO_PID;

    /* Allocate the two path buffers on the heap up front and reuse    */
    /* them for every directory entry.                                 */
    p_vid_path = (char *)calloc(SYSFS_PATH_MAX, sizeof(char));
    p_pid_path = (char *)calloc(SYSFS_PATH_MAX, sizeof(char));
    if ((NULL == p_vid_path) || (NULL == p_pid_path))
    {
        result = -2;
        goto cleanup;
    }

    p_dir = opendir(SYSFS_USB_ROOT);
    if (NULL == p_dir)
    {
        goto cleanup;
    }

    for (p_entry = readdir(p_dir);
         NULL != p_entry;
         p_entry = readdir(p_dir))
    {
        int vendor_id  = 0;
        int product_id = 0;

        /* Skip dot entries and USB interface nodes (which contain a   */
        /* ':' such as "1-1:1.0"); only whole devices carry idVendor.  */
        if (('.' == p_entry->d_name[0]) ||
            (NULL != strchr(p_entry->d_name, ':')))
        {
            continue;
        }

        (void)snprintf(p_vid_path, SYSFS_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/idVendor", p_entry->d_name);
        (void)snprintf(p_pid_path, SYSFS_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/idProduct", p_entry->d_name);

        if (0 != read_hex_attr(p_vid_path, &vendor_id))
        {
            continue;
        }
        if ((int)VITURE_USB_VENDOR_ID != vendor_id)
        {
            continue;
        }
        if (0 != read_hex_attr(p_pid_path, &product_id))
        {
            continue;
        }

        /* Apply the optional validator supplied by the caller. */
        if ((NULL != pf_is_valid) && (0 == pf_is_valid(product_id)))
        {
            continue;
        }

        /* Found a matching glasses device. */
        *p_product_id = product_id;
        result        = 0;
        goto cleanup;
    }

cleanup:
    if (NULL != p_dir)
    {
        (void)closedir(p_dir);
    }
    if (NULL != p_pid_path)
    {
        free(p_pid_path);
    }
    if (NULL != p_vid_path)
    {
        free(p_vid_path);
    }
    return result;
}
