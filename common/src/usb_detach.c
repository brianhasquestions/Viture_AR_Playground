/**********************************************************************
 * @file    usb_detach.c
 * @brief   Detach the kernel driver (uvcvideo) from the VITURE camera.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * Implementation notes:
 *  - Uses POSIX open/ioctl plus the Linux usbfs interface only.
 *  - USBDEVFS_DISCONNECT is delivered through the USBDEVFS_IOCTL
 *    wrapper, which targets a single interface number.
 *  - ENODATA / EINVAL from the ioctl simply mean "no driver bound to
 *    that interface", which is a success condition for our purposes.
 *  - Single point of exit via goto cleanup; Yoda comparisons; all
 *    locals initialised to their type's null value; heap allocation
 *    for the path buffers.
 **********************************************************************/

#include "usb_detach.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Root of the Linux USB device tree in sysfs. */
#define SYSFS_USB_ROOT      "/sys/bus/usb/devices"

/* Upper bound for constructed path strings. */
#define USB_PATH_MAX        (512U)

/* Small scratch buffer for reading a single sysfs attribute. */
#define USB_ATTR_MAX        (64U)

/**********************************************************************
 * @brief  Read an integer from a sysfs attribute file.
 *
 * @param[in]  p_path   Absolute path to the sysfs attribute.
 * @param[in]  base     Numeric base (10 for busnum, 16 for idVendor).
 * @param[out] p_value  Receives the parsed value on success.
 *
 * @return  0 on success, -1 on any failure.
 **********************************************************************/
static int read_int_attr(const char * p_path, int base, int * p_value)
{
    int    result   = -1;
    FILE * p_file    = NULL;
    char * p_buffer  = NULL;
    char * p_end     = NULL;
    long   parsed    = 0L;

    if ((NULL == p_path) || (NULL == p_value))
    {
        goto cleanup;
    }

    p_buffer = (char *)calloc(USB_ATTR_MAX, sizeof(char));
    if (NULL == p_buffer)
    {
        goto cleanup;
    }

    p_file = fopen(p_path, "r");
    if (NULL == p_file)
    {
        goto cleanup;
    }

    if (NULL == fgets(p_buffer, (int)USB_ATTR_MAX, p_file))
    {
        goto cleanup;
    }

    parsed = strtol(p_buffer, &p_end, base);
    if (p_end == p_buffer)
    {
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
 * @brief  Ask the kernel to release one interface of an open device.
 *
 * @param[in]  fd             Open file descriptor on the usbfs node.
 * @param[in]  interface_num  Interface number to disconnect.
 *
 * @return  0 if the interface is now free (or never was bound),
 *         -1 if the kernel refused to release it.
 **********************************************************************/
static int disconnect_interface(int fd, int interface_num)
{
    int                  result  = -1;
    int                  rc      = -1;
    struct usbdevfs_ioctl command = { 0 };

    command.ifno       = interface_num;
    command.ioctl_code = (int)USBDEVFS_DISCONNECT;
    command.data       = NULL;

    rc = ioctl(fd, USBDEVFS_IOCTL, &command);
    if (0 == rc)
    {
        /* A driver was bound and has now been detached. */
        result = 0;
        goto cleanup;
    }

    /* ENODATA/EINVAL simply mean nothing was bound to this interface, */
    /* which is exactly the state we want it in.                       */
    if ((ENODATA == errno) || (EINVAL == errno))
    {
        result = 0;
        goto cleanup;
    }

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Detach any bound kernel driver from a USB device.
 *
 * See usb_detach.h for the full contract.
 **********************************************************************/
int usb_detach_kernel_driver(int vendor_id, int product_id)
{
    int             result      = -1;
    int             fd          = -1;
    int             busnum      = 0;
    int             devnum      = 0;
    int             found       = 0;
    int             index       = 0;
    DIR *           p_dir       = NULL;
    struct dirent * p_entry     = NULL;
    char *          p_attr_path = NULL;
    char *          p_node_path = NULL;

    p_attr_path = (char *)calloc(USB_PATH_MAX, sizeof(char));
    p_node_path = (char *)calloc(USB_PATH_MAX, sizeof(char));
    if ((NULL == p_attr_path) || (NULL == p_node_path))
    {
        goto cleanup;
    }

    p_dir = opendir(SYSFS_USB_ROOT);
    if (NULL == p_dir)
    {
        goto cleanup;
    }

    /* Locate the device's bus and device numbers via sysfs. */
    for (p_entry = readdir(p_dir);
         NULL != p_entry;
         p_entry = readdir(p_dir))
    {
        int this_vid = 0;
        int this_pid = 0;

        if (('.' == p_entry->d_name[0]) ||
            (NULL != strchr(p_entry->d_name, ':')))
        {
            continue;
        }

        (void)snprintf(p_attr_path, USB_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/idVendor", p_entry->d_name);
        if (0 != read_int_attr(p_attr_path, 16, &this_vid))
        {
            continue;
        }
        if (vendor_id != this_vid)
        {
            continue;
        }

        (void)snprintf(p_attr_path, USB_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/idProduct", p_entry->d_name);
        if (0 != read_int_attr(p_attr_path, 16, &this_pid))
        {
            continue;
        }
        if (product_id != this_pid)
        {
            continue;
        }

        (void)snprintf(p_attr_path, USB_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/busnum", p_entry->d_name);
        if (0 != read_int_attr(p_attr_path, 10, &busnum))
        {
            continue;
        }

        (void)snprintf(p_attr_path, USB_PATH_MAX,
                       SYSFS_USB_ROOT "/%s/devnum", p_entry->d_name);
        if (0 != read_int_attr(p_attr_path, 10, &devnum))
        {
            continue;
        }

        found = 1;
        break;
    }

    if (0 == found)
    {
        /* Device is not plugged in. */
        goto cleanup;
    }

    (void)snprintf(p_node_path, USB_PATH_MAX, "/dev/bus/usb/%03d/%03d",
                   busnum, devnum);

    /* Write access is required for USBDEVFS_DISCONNECT; the project    */
    /* udev rule grants it to the logged-in user.                       */
    fd = open(p_node_path, O_RDWR);
    if (0 > fd)
    {
        (void)fprintf(stderr,
                      "[usb] cannot open %s for writing: %s\n",
                      p_node_path, strerror(errno));
        (void)fprintf(stderr,
                      "[usb] install scripts/70-viture.rules?\n");
        result = -2;
        goto cleanup;
    }

    /* Release every interface the kernel may have claimed. */
    result = 0;
    for (index = 0; index < USB_DETACH_MAX_INTERFACES; index++)
    {
        if (0 != disconnect_interface(fd, index))
        {
            result = -1;
        }
    }

    if (0 == result)
    {
        (void)printf("[usb] kernel driver detached from %s\n",
                     p_node_path);
    }

cleanup:
    if (0 <= fd)
    {
        (void)close(fd);
    }
    if (NULL != p_dir)
    {
        (void)closedir(p_dir);
    }
    if (NULL != p_node_path)
    {
        free(p_node_path);
    }
    if (NULL != p_attr_path)
    {
        free(p_attr_path);
    }
    return result;
}
