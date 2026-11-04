// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2008-2011 Freescale Semiconductor, Inc.
 */

/* #define DEBUG */

#include <asm/global_data.h>

#include <command.h>
#include <env.h>
#include <env_internal.h>
#include <fdtdec.h>
#include <linux/stddef.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <search.h>
#include <scsi.h>
#include <errno.h>
#include <dm/ofnode.h>

DECLARE_GLOBAL_DATA_PTR;
static env_t envbuf;

struct env_scsi_info {
	struct blk_desc *blk;
	struct disk_partition part;
	int count;
};

static struct env_scsi_info env_part;

static inline struct env_scsi_info *env_scsi_get_part(void)
{
	static bool is_scsi_scanned;
	struct env_scsi_info *ep = &env_part;

	if (!is_scsi_scanned) {
		scsi_scan(false /* no verbose */);
		is_scsi_scanned = true;
	}

	if (CONFIG_ENV_SCSI_PART_UUID[0] == '\0') {
		if (blk_get_device_part_str("scsi", CONFIG_ENV_SCSI_HW_PARTITION,
					    &ep->blk, &ep->part, true) < 0)
			return NULL;
	} else {
		if (scsi_get_blk_by_uuid(CONFIG_ENV_SCSI_PART_UUID, &ep->blk, &ep->part))
			return NULL;
	}

	ep->count = CONFIG_ENV_SIZE / ep->part.blksz;

	/* The redundant copy directly follows the first one */
	if (IS_ENABLED(CONFIG_ENV_REDUNDANT) && ep->part.size < 2 * ep->count) {
		printf("SCSI partition too small for the redundant environment\n");
		return NULL;
	}

	return ep;
}

static int env_scsi_save(void)
{
	struct env_scsi_info *ep = env_scsi_get_part();
	lbaint_t start;
	int ret;

	if (!ep)
		return -ENOENT;

	ret = env_export(&envbuf);
	if (ret)
		return ret;

	/* Write the copy which is not valid, so a failed write loses nothing */
	start = ep->part.start;
	if (IS_ENABLED(CONFIG_ENV_REDUNDANT) && gd->env_valid == ENV_VALID)
		start += ep->count;

	if (blk_dwrite(ep->blk, start, ep->count, &envbuf) != ep->count)
		return -EIO;

	if (IS_ENABLED(CONFIG_ENV_REDUNDANT))
		gd->env_valid = gd->env_valid == ENV_VALID ? ENV_REDUND : ENV_VALID;

	return 0;
}

static int env_scsi_erase(void)
{
	struct env_scsi_info *ep = env_scsi_get_part();
	int ret;

	if (!ep)
		return -ENOENT;

	ret = (int)blk_derase(ep->blk, ep->part.start, ep->count);
	if (IS_ENABLED(CONFIG_ENV_REDUNDANT) && !ret)
		ret = (int)blk_derase(ep->blk, ep->part.start + ep->count,
				      ep->count);

	return ret;
}

#if defined(ENV_IS_EMBEDDED)
static int env_scsi_load(void)
{
	return 0;
}
#elif defined(CONFIG_ENV_REDUNDANT)
static int env_scsi_load(void)
{
	ALLOC_CACHE_ALIGN_BUFFER(char, env1, CONFIG_ENV_SIZE);
	ALLOC_CACHE_ALIGN_BUFFER(char, env2, CONFIG_ENV_SIZE);
	struct env_scsi_info *ep = env_scsi_get_part();
	int read1_fail, read2_fail;

	if (!ep) {
		if (CONFIG_ENV_SCSI_PART_UUID[0] == '\0')
			env_set_default("SCSI partition " CONFIG_ENV_SCSI_HW_PARTITION " not found", 0);
		else
			env_set_default(CONFIG_ENV_SCSI_PART_UUID " partition not found", 0);

		return -ENOENT;
	}

	read1_fail = blk_dread(ep->blk, ep->part.start, ep->count, env1) != ep->count;
	read2_fail = blk_dread(ep->blk, ep->part.start + ep->count, ep->count,
			       env2) != ep->count;

	return env_import_redund(env1, read1_fail, env2, read2_fail, H_EXTERNAL);
}
#else
static int env_scsi_load(void)
{
	struct env_scsi_info *ep = env_scsi_get_part();
	int ret;

	if (!ep) {
		if (CONFIG_ENV_SCSI_PART_UUID[0] == '\0')
			env_set_default("SCSI partition " CONFIG_ENV_SCSI_HW_PARTITION " not found", 0);
		else
			env_set_default(CONFIG_ENV_SCSI_PART_UUID " partition not found", 0);

		return -ENOENT;
	}

	if (blk_dread(ep->blk, ep->part.start, ep->count, &envbuf) != ep->count) {
		if (CONFIG_ENV_SCSI_PART_UUID[0] == '\0')
			env_set_default("SCSI partition " CONFIG_ENV_SCSI_HW_PARTITION " read failed", 0);
		else
			env_set_default(CONFIG_ENV_SCSI_PART_UUID " partition read failed", 0);

		return -EIO;
	}

	ret = env_import((char *)&envbuf, 1, H_EXTERNAL);
	if (ret) {
		debug("ENV import failed\n");
		env_set_default("Cannot load environment", 0);
	} else {
		gd->env_addr = (ulong)envbuf.data;
	}

	return ret;
}
#endif

U_BOOT_ENV_LOCATION(scsi) = {
	.location	= ENVL_SCSI,
	ENV_NAME("SCSI")
	.load		= env_scsi_load,
	.save		= ENV_SAVE_PTR(env_scsi_save),
	.erase		= ENV_ERASE_PTR(env_scsi_erase),
};
