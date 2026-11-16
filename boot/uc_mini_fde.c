// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Canonical
 *
 * The boot of Ubuntu Core mini into the FDE TA of OP-TEE (optee-uc-fde).
 *
 * TA v2 seals the keys of the encrypted volumes to a measured boot: an
 * extend-only register in the TA, which U-Boot extends first and the initrd
 * then with the system slot it maps, as systemd-stub and the initrd extend a
 * PCR of a TPM. Before Linux starts, U-Boot extends it with, in this order:
 *
 *  - the SHA-256 of the FIT it boots, the whole flattened tree, which holds
 *    the hashes of the kernel, the ramdisk and the device trees it verified;
 *  - the final bootargs, after fixup_secure_linux() appended the gate word, as
 *    its words with a single space between them, which is how snapd predicts
 *    them: the env composes them over lines, and an empty variable leaves
 *    spaces of its own.
 *
 * Nothing that runs later can take U-Boot's place: an argument the keys were
 * not sealed to only fails their unseal. With a v1 TA, or none, nothing is
 * measured and the boot is as it was. A v2 TA that cannot be extended stops
 * the boot: later code could extend it with what U-Boot would have.
 *
 * 'ucfde args <var>' sets <var> to snapd's kernel arguments for this boot,
 * which the boot env appends to the command line: snapd_try_cmdline_args on
 * the boot of a trial, when snapd set it, snapd_full_cmdline_args on any other.
 * It does so only with a v2 TA, which bootm measures the command line into:
 * with a v1 TA, or none, nothing would measure an argument the env gives, so
 * the arguments are refused and <var> is left empty, and the command line is
 * the env's own. 'ucfde v2' succeeds when the TA is v2.
 */

#include <command.h>
#include <env.h>
#include <errno.h>
#include <image.h>
#include <malloc.h>
#include <tee.h>
#include <uc_mini_fde.h>
#include <linux/ctype.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <u-boot/sha256.h>

/* fd1b2a86-3668-11eb-adc1-0242ac120002 */
#define UC_FDE_TA_UUID { 0xfd1b2a86, 0x3668, 0x11eb, \
	{ 0xad, 0xc1, 0x02, 0x42, 0xac, 0x12, 0x00, 0x02 } }

#define UC_FDE_CMD_TA_VERSION	6
#define UC_FDE_CMD_EXTEND	7
/* the major version is in the upper half */
#define UC_FDE_TA_MAJOR(v)	((v) >> 16)

struct uc_fde_session {
	struct udevice *tee;
	u32 session;
};

static int uc_fde_open(struct uc_fde_session *s)
{
	const struct tee_optee_ta_uuid uuid = UC_FDE_TA_UUID;
	struct tee_open_session_arg arg;
	int rc;

	s->tee = tee_find_device(NULL, NULL, NULL, NULL);
	if (!s->tee)
		return -ENODEV;
	memset(&arg, 0, sizeof(arg));
	tee_optee_ta_uuid_to_octets(arg.uuid, &uuid);
	rc = tee_open_session(s->tee, &arg, 0, NULL);
	if (rc)
		return rc;
	if (arg.ret != TEE_SUCCESS)
		return -ENOENT;
	s->session = arg.session;
	return 0;
}

static void uc_fde_close(struct uc_fde_session *s)
{
	tee_close_session(s->tee, s->session);
}

/* the major version of the TA, 0 when it cannot be asked */
static u32 uc_fde_major(struct uc_fde_session *s)
{
	struct tee_invoke_arg arg = { };
	struct tee_param param = { };

	arg.func = UC_FDE_CMD_TA_VERSION;
	arg.session = s->session;
	param.attr = TEE_PARAM_ATTR_TYPE_VALUE_OUTPUT;
	if (tee_invoke_func(s->tee, &arg, 1, &param) || arg.ret != TEE_SUCCESS)
		return 0;
	return UC_FDE_TA_MAJOR((u32)param.u.value.a);
}

static int uc_fde_extend(struct uc_fde_session *s, const void *data,
			 size_t size)
{
	struct tee_invoke_arg arg = { };
	struct tee_param param = { };
	struct tee_shm *shm;
	int rc;

	rc = tee_shm_alloc(s->tee, size ? size : 1, 0, &shm);
	if (rc)
		return rc;
	memcpy(shm->addr, data, size);
	arg.func = UC_FDE_CMD_EXTEND;
	arg.session = s->session;
	param.attr = TEE_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param.u.memref.shm = shm;
	param.u.memref.size = size;
	rc = tee_invoke_func(s->tee, &arg, 1, &param);
	tee_shm_free(shm);
	if (rc)
		return rc;
	return arg.ret == TEE_SUCCESS ? 0 : -EIO;
}

/* the words of s, with a single space between them */
static char *uc_fde_words(const char *s)
{
	char *out = malloc(strlen(s) + 1);
	char *p = out;

	if (!out)
		return NULL;
	while (*s) {
		while (*s && isspace(*s))
			s++;
		if (!*s)
			break;
		if (p != out)
			*p++ = ' ';
		while (*s && !isspace(*s))
			*p++ = *s++;
	}
	*p = '\0';
	return out;
}

bool uc_mini_fde_ta_v2(void)
{
	struct uc_fde_session s;
	u32 major;

	if (uc_fde_open(&s))
		return false;
	major = uc_fde_major(&s);
	uc_fde_close(&s);
	return major >= 2;
}

int uc_mini_fde_measure(struct bootm_headers *images)
{
	u8 fit_digest[SHA256_SUM_LEN];
	struct uc_fde_session s;
	const char *bootargs;
	char *words;
	void *fit = images->fit_hdr_os;
	int rc;

	if (uc_fde_open(&s))
		return 0;
	if (uc_fde_major(&s) < 2) {
		uc_fde_close(&s);
		return 0;
	}

	if (!fit || fdt_check_header(fit)) {
		printf("uc-fde: the boot is not of a FIT, refusing to boot it unmeasured\n");
		rc = -EINVAL;
		goto out;
	}
	sha256_csum_wd(fit, fdt_totalsize(fit), fit_digest, CHUNKSZ_SHA256);
	rc = uc_fde_extend(&s, fit_digest, sizeof(fit_digest));
	if (rc)
		goto fail;

	bootargs = env_get("bootargs");
	words = uc_fde_words(bootargs ? bootargs : "");
	if (!words) {
		rc = -ENOMEM;
		goto fail;
	}
	rc = uc_fde_extend(&s, words, strlen(words));
	free(words);
	if (rc)
		goto fail;
	printf("uc-fde: measured the FIT and the kernel command line\n");
	goto out;

fail:
	printf("uc-fde: cannot measure the boot (%d), refusing to boot it\n", rc);
out:
	uc_fde_close(&s);
	return rc;
}

/* whether an env value holds anything but spaces, as snapd writes " " */
static bool uc_fde_blank(const char *s)
{
	for (; s && *s; s++) {
		if (!isspace(*s))
			return false;
	}
	return true;
}

static int uc_fde_args(const char *var)
{
	const char *full = env_get("snapd_full_cmdline_args");
	const char *try = env_get("snapd_try_cmdline_args");
	const char *status = env_get("slot_status");
	const char *args = full;

	/* the boot env has just moved a trial it boots to "trying" */
	if (status && !strcmp(status, "trying") && !uc_fde_blank(try))
		args = try;
	if (!uc_mini_fde_ta_v2()) {
		if (!uc_fde_blank(full) || !uc_fde_blank(try))
			printf("uc-fde: the FDE TA measures no kernel command line, refusing snapd's arguments\n");
		args = NULL;
	}
	if (uc_fde_blank(args))
		args = NULL;
	return env_set(var, args) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int do_ucfde(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	if (argc == 2 && !strcmp(argv[1], "v2"))
		return uc_mini_fde_ta_v2() ? CMD_RET_SUCCESS : CMD_RET_FAILURE;
	if (argc == 3 && !strcmp(argv[1], "args"))
		return uc_fde_args(argv[2]);
	return CMD_RET_USAGE;
}

U_BOOT_CMD(ucfde, 3, 0, do_ucfde,
	   "the FDE TA of Ubuntu Core mini",
	   "v2 - succeed if the TA seals keys to a measured boot\n"
	   "ucfde args <var> - set <var> to snapd's kernel arguments for this boot,\n"
	   "    only with a v2 TA, which measures the command line\n");
