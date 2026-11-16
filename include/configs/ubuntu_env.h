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
 * 'slot_status'/'try_slot' handshake snapd arms for a trial of the other system
 * slot. 'kernel_status', 'snap_kernel' and 'snap_try_kernel' are snapd's.
 *
 * With ENV_WRITEABLE_LIST nothing else survives loading the stored env, and what
 * does not load is gone for good the first time U-Boot saves the env itself,
 * which it does on every try boot. The apps slot's state is not here: snapd keeps
 * it in modeenv, as U-Boot never boots it. 'kernel_filename' comes from the
 * default env alone, the kernel's file name in the slot is fixed. The
 * 'snapd_*_cmdline_args' are the kernel arguments snapd keeps, which the
 * 'snapd_cmdline' callback validates.
 */
#define CFG_ENV_FLAGS_LIST_STATIC \
	"kernel_status:sw," \
	"snap_kernel:sw," \
	"snap_try_kernel:sw," \
	"active_slot:sw," \
	"snapd_recovery_mode:sw," \
	"try_slot:sw," \
	"slot_status:sw," \
	"snapd_full_cmdline_args:sw," \
	"snapd_try_cmdline_args:sw"

#endif /* UBUNTU_ENV_H */
