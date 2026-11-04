/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Secure boot state of Qualcomm SoCs, as reported by TrustZone.
 */

#ifndef __MACH_SECBOOT_H
#define __MACH_SECBOOT_H

#include <stdbool.h>

/**
 * enum qcom_secboot_state - What TrustZone reports about the secure boot fuses
 *
 * @QCOM_SECBOOT_UNKNOWN: the call failed, or the answer is not a clean open or
 *                        enforcing device
 * @QCOM_SECBOOT_OPEN: none of the secure boot, HW key and debug fuses are blown
 * @QCOM_SECBOOT_ENFORCED: secure boot is fused on
 */
enum qcom_secboot_state {
	QCOM_SECBOOT_UNKNOWN,
	QCOM_SECBOOT_OPEN,
	QCOM_SECBOOT_ENFORCED,
};

/**
 * qcom_get_secboot_state() - Ask TrustZone for the secure boot state
 *
 * Return: the state, see &enum qcom_secboot_state
 */
enum qcom_secboot_state qcom_get_secboot_state(void);

/**
 * qcom_device_is_locked() - Tell whether signed boot has to be enforced
 *
 * Anything but a device TrustZone positively reports as open counts as locked,
 * as this decides if a failed image signature check is fatal.
 *
 * Return: false only if the device is known to be open, true otherwise
 */
bool qcom_device_is_locked(void);

#endif /* __MACH_SECBOOT_H */
