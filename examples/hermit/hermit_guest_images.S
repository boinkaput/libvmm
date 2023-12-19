/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if defined(GUEST_KERNEL_IMAGE_PATH)
.section .guest_kernel_image, "aw", @progbits
.global _guest_kernel_image, _guest_kernel_image_end
_guest_kernel_image:
.incbin GUEST_KERNEL_IMAGE_PATH
_guest_kernel_image_end:
#endif

#if defined(GUEST_LOADER_IMAGE_PATH)
.section .guest_loader_image, "aw", @progbits
.global _guest_loader_image, _guest_loader_image_end
_guest_loader_image:
.incbin GUEST_LOADER_IMAGE_PATH
_guest_loader_image_end:
#endif

#if defined(GUEST_DTB_IMAGE_PATH)
.section .guest_dtb_image, "aw", @progbits
.global _guest_dtb_image, _guest_dtb_image_end
_guest_dtb_image:
.incbin GUEST_DTB_IMAGE_PATH
_guest_dtb_image_end:
#endif

#if defined(GUEST_INITRD_IMAGE_PATH)
.section .guest_initrd_image, "aw", @progbits
.global _guest_initrd_image, _guest_initrd_image_end
_guest_initrd_image:
.incbin GUEST_INITRD_IMAGE_PATH
_guest_initrd_image_end:
#endif

