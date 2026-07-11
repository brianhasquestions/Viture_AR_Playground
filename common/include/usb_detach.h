/**********************************************************************
 * @file    usb_detach.h
 * @brief   Detach the kernel driver (uvcvideo) from the VITURE camera.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * The VITURE SDK speaks UVC directly over libusb rather than going
 * through V4L2. When the kernel's uvcvideo driver is bound to the
 * camera's interfaces it owns the UVC control endpoints, and the SDK's
 * attempt to negotiate a stream format fails with
 *
 *     Failed to negotiate 1920x1080@30fps MJPEG: Invalid mode
 *     (VITURE_GLASSES_ERROR_NOT_SUPPORTED, -4)
 *
 * This module issues the USBDEVFS_DISCONNECT ioctl on the camera's
 * usbfs node, which asks the kernel to release each interface. Given
 * write permission on /dev/bus/usb/<bus>/<dev> (granted by the project
 * udev rule in scripts/70-viture.rules) this requires no root.
 *
 * The kernel re-binds uvcvideo automatically when the device is
 * replugged, so the change is non-destructive and self-healing.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef USB_DETACH_H
#define USB_DETACH_H

#include <stdint.h>

/* Number of USB interfaces exposed by the camera (VideoControl +      */
/* VideoStreaming). Detaching is attempted on each in turn.            */
#define USB_DETACH_MAX_INTERFACES   (8)

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Detach any bound kernel driver from a USB device.
 *
 * Locates the device by vendor/product ID via sysfs, opens its usbfs
 * node, and requests disconnection of every claimed interface.
 *
 * @param[in]  vendor_id   USB vendor ID of the target device.
 * @param[in]  product_id  USB product ID of the target device.
 *
 * @return  0 when the device was found and no interface remains bound
 *            (including the case where none was bound to begin with),
 *         -1 when the device is not present,
 *         -2 when the usbfs node could not be opened for writing
 *            (udev rule not installed?).
 **********************************************************************/
int usb_detach_kernel_driver(int vendor_id, int product_id);

#ifdef __cplusplus
}
#endif

#endif /* USB_DETACH_H */
