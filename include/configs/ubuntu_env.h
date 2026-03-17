/*
 * Copyright 2025 Canonical
 *
 * SPDX-License-Identifier:     GPL-2.0+
 */

#ifndef UBUNTU_ENV_H
#define UBUNTU_ENV_H

/*
 * The variables a stored env may set. Ubuntu Core mini boots through an initrd
 * which reads the dm-verity root hash and geometry of the base snap out of the
 * slot itself, so the env only says which slot to boot: 'active_slot', and the
 * 'try_mode'/'try_slot' handshake an A/B refresh arms. 'kernel_status',
 * 'snap_kernel' and 'snap_try_kernel' are snapd's.
 *
 * With ENV_WRITEABLE_LIST nothing else survives loading the stored env, and what
 * does not load is gone for good the first time U-Boot saves the env itself,
 * which it does on every try boot. So the variables userspace keeps here are
 * listed even though U-Boot never reads them: 'try_rev', and the apps slot's own
 * A/B record, without which a try boot of the system slot would silently put the
 * apps slot back on 'a'.
 */
#define CFG_ENV_FLAGS_LIST_STATIC \
	"kernel_status:sw," \
	"snap_kernel:sw," \
	"snap_try_kernel:sw," \
	"active_slot:sw," \
	"kernel_filename:sw," \
	"snapd_recovery_mode:sw," \
	"try_mode:sw," \
	"try_slot:sw," \
	"try_rev:sw," \
	"active_apps_slot:sw," \
	"apps_try_mode:sw," \
	"apps_try_slot:sw," \
	"apps_try_rev:sw"

#endif /* UBUNTU_ENV_H */
