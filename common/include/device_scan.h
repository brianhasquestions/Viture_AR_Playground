/**********************************************************************
 * @file    device_scan.h
 * @brief   POSIX sysfs enumeration of connected VITURE XR glasses.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * This module walks the Linux sysfs USB tree (/sys/bus/usb/devices)
 * to locate a connected VITURE device and return its USB product ID.
 * Only the POSIX / Linux sysfs interface is used so that the scan is
 * portable across Linux systems without libusb or vendor helpers.
 *
 * Coding standard: Barr-C:2018 (Allman braces, Yoda comparisons,
 * fixed-width types, single point of exit via goto cleanup).
 **********************************************************************/

#ifndef DEVICE_SCAN_H
#define DEVICE_SCAN_H

#include <stdint.h>

/* USB Vendor ID assigned to VITURE Inc. */
#define VITURE_USB_VENDOR_ID    (0x35CAU)

/* Sentinel returned when no valid glasses product ID was located.   */
#define DEVICE_SCAN_NO_PID      (0)

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Scan the sysfs USB tree for a connected VITURE device.
 *
 * The caller supplies a validator callback (typically the SDK's
 * xr_device_provider_is_product_id_valid) so that this module stays
 * independent of the vendor library. The first product ID that both
 * matches the VITURE vendor ID and passes the validator is written to
 * *p_product_id.
 *
 * @param[in]  pf_is_valid    Validator; returns non-zero for a valid
 *                            glasses product ID. May be NULL to accept
 *                            any VITURE-vendor device.
 * @param[out] p_product_id   Receives the located product ID, or
 *                            DEVICE_SCAN_NO_PID when none is found.
 *
 * @return  0 on success (a device was found and written),
 *         -1 when no matching device is present,
 *         -2 on an invalid argument.
 **********************************************************************/
int device_scan_find_glasses(int (*pf_is_valid)(int product_id),
                             int * p_product_id);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_SCAN_H */
