/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Canonical
 */

#ifndef _UC_MINI_FDE_H
#define _UC_MINI_FDE_H

#include <linux/types.h>

struct bootm_headers;

/**
 * uc_mini_fde_ta_v2() - Whether the FDE TA seals keys to a measured boot
 *
 * Return: true when the TA of optee-uc-fde is there and is v2 or newer
 */
bool uc_mini_fde_ta_v2(void);

/**
 * uc_mini_fde_measure() - Measure the boot into the FDE TA
 * @images:	the images bootm boots
 *
 * With a v2 TA, extends its register with the SHA-256 of the FIT, then the
 * final bootargs. Without one, measures nothing.
 *
 * Return: 0 when the boot may go on, < 0 when a v2 TA could not be extended
 */
int uc_mini_fde_measure(struct bootm_headers *images);

#endif /* _UC_MINI_FDE_H */
