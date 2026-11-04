// SPDX-License-Identifier: GPL-2.0+
/*
 * Query the secure boot state of Qualcomm SoCs from TrustZone, the equivalent
 * of reading the HAB/AHAB lifecycle on i.MX.
 *
 * This is the call Qualcomm's own bootloaders use: is_secure_boot_enable() in
 * LK, and ReadSecurityState() in ABL. The fuses themselves sit in QFPROM, whose
 * layout differs per SoC, while this call does not.
 */

#define LOG_CATEGORY LOGC_BOARD
#define pr_fmt(fmt) "QCOM: " fmt

#include <asm/global_data.h>
#include <init.h>
#include <linux/arm-smccc.h>
#include <linux/bitops.h>
#include <log.h>
#include <mach/secboot.h>

DECLARE_GLOBAL_DATA_PTR;

#define QCOM_SCM_SVC_INFO		0x06
#define QCOM_SCM_INFO_GET_SECURE_STATE	0x04

#define QCOM_SCM_GET_SECURE_STATE_FN \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_64, \
			   ARM_SMCCC_OWNER_SIP, \
			   (QCOM_SCM_SVC_INFO << 8) | \
			   QCOM_SCM_INFO_GET_SECURE_STATE)

/*
 * The first response word has one bit per check TrustZone did, and a set bit
 * means the check FAILED: a clear bit is the secure state.
 */
#define QCOM_SECSTATE_SECBOOT_FAILED	BIT(0)	/* secure boot is not enabled */
#define QCOM_SECSTATE_HW_KEY_FAILED	BIT(1)	/* HW key is not programmed */
#define QCOM_SECSTATE_DEBUG_FAILED	BIT(2)	/* debug is not disabled */

#define QCOM_SECSTATE_NO_FUSE_BLOWN	(QCOM_SECSTATE_SECBOOT_FAILED | \
					 QCOM_SECSTATE_HW_KEY_FAILED | \
					 QCOM_SECSTATE_DEBUG_FAILED)

enum qcom_secboot_state qcom_get_secboot_state(void)
{
	struct arm_smccc_res res;
	u32 state;

	/* No parameters, so the argument info in x1 is 0 */
	arm_smccc_smc(QCOM_SCM_GET_SECURE_STATE_FN, 0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0) {
		log_warning("Cannot read the secure state: %ld\n", (long)res.a0);
		return QCOM_SECBOOT_UNKNOWN;
	}

	state = res.a1;
	debug("%s: secure state 0x%x\n", __func__, state);

	if (!(state & QCOM_SECSTATE_SECBOOT_FAILED))
		return QCOM_SECBOOT_ENFORCED;

	/*
	 * Open means none of the fuses are blown, as LK tests for it. Secure boot
	 * off with any of the others blown is no state a device gets into on
	 * purpose, so do not take it as a licence to skip signature checks.
	 */
	if ((state & QCOM_SECSTATE_NO_FUSE_BLOWN) == QCOM_SECSTATE_NO_FUSE_BLOWN)
		return QCOM_SECBOOT_OPEN;

	log_warning("Inconsistent secure state 0x%x\n", state);

	return QCOM_SECBOOT_UNKNOWN;
}

bool qcom_device_is_locked(void)
{
	return qcom_get_secboot_state() != QCOM_SECBOOT_OPEN;
}

/*
 * Unlike qcom_device_is_locked(), this needs a positive answer. If the call does
 * not work on some firmware, signature checks still stay fatal, but a board which
 * then does not boot should not also go silent.
 */
int board_early_init_f(void)
{
	if (IS_ENABLED(CONFIG_DISABLE_CONSOLE) &&
	    qcom_get_secboot_state() == QCOM_SECBOOT_ENFORCED)
		gd->flags |= GD_FLG_DISABLE_CONSOLE;

	return 0;
}
