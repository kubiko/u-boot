// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2012
 * Joe Hershberger, National Instruments, joe.hershberger@ni.com
 */

#include <env.h>
#include <env_internal.h>
#include <asm/global_data.h>
#include <linux/ctype.h>

/*
 * Look up a callback function pointer by name
 */
static struct env_clbk_tbl *find_env_callback(const char *name)
{
	struct env_clbk_tbl *clbkp;
	int i;
	int num_callbacks = ll_entry_count(struct env_clbk_tbl, env_clbk);

	if (name == NULL)
		return NULL;

	/* look up the callback in the linker-list */
	for (i = 0, clbkp = ll_entry_start(struct env_clbk_tbl, env_clbk);
	     i < num_callbacks;
	     i++, clbkp++) {
		if (strcmp(name, clbkp->name) == 0)
			return clbkp;
	}

	return NULL;
}

static int first_call = 1;
static const char *callback_list;

/*
 * Look for a possible callback for a newly added variable
 * This is called specifically when the variable did not exist in the hash
 * previously, so the blanket update did not find this variable.
 */
void env_callback_init(struct env_entry *var_entry)
{
	const char *var_name = var_entry->key;
	char callback_name[256] = "";
	struct env_clbk_tbl *clbkp;
	int ret = 1;

	if (first_call) {
		callback_list = env_get(ENV_CALLBACK_VAR);
		first_call = 0;
	}

	var_entry->callback = NULL;

	/* look in the ".callbacks" var for a reference to this variable */
	if (callback_list != NULL)
		ret = env_attr_lookup(callback_list, var_name, callback_name);

	/* only if not found there, look in the static list */
	if (ret)
		ret = env_attr_lookup(ENV_CALLBACK_LIST_STATIC, var_name,
			callback_name);

	/* if an association was found, set the callback pointer */
	if (!ret && strlen(callback_name)) {
		clbkp = find_env_callback(callback_name);
		if (clbkp != NULL)
			var_entry->callback = clbkp->callback;
	}
}

/*
 * Called on each existing env var prior to the blanket update since removing
 * a callback association should remove its callback.
 */
static int clear_callback(struct env_entry *entry)
{
	entry->callback = NULL;

	return 0;
}

/*
 * Call for each element in the list that associates variables to callbacks
 */
static int set_callback(const char *name, const char *value, void *priv)
{
	struct env_entry e, *ep;
	struct env_clbk_tbl *clbkp;

	e.key	= name;
	e.data	= NULL;
	e.callback = NULL;
	hsearch_r(e, ENV_FIND, &ep, &env_htab, 0);

	/* does the env variable actually exist? */
	if (ep != NULL) {
		/* the assocaition delares no callback, so remove the pointer */
		if (value == NULL || strlen(value) == 0)
			ep->callback = NULL;
		else {
			/* assign the requested callback */
			clbkp = find_env_callback(value);
			if (clbkp != NULL)
				ep->callback = clbkp->callback;
		}
	}

	return 0;
}

static int on_callbacks(const char *name, const char *value, enum env_op op,
	int flags)
{
	/* remove all callbacks */
	hwalk_r(&env_htab, clear_callback);

	/* configure any static callback bindings */
	env_attr_walk(ENV_CALLBACK_LIST_STATIC, set_callback, NULL);
	/* configure any dynamic callback bindings */
	env_attr_walk(value, set_callback, NULL);

	return 0;
}
U_BOOT_ENV_CALLBACK(callbacks, on_callbacks);

static int on_snapd_mode(const char *name, const char *value, enum env_op op,
	int flags)
{
	if (strcmp(name, "snapd_recovery_mode") == 0) {
		/* only run/install/recover/factory-reset values are permitted */
		if (strcmp(value, "run") == 0)
			return 0;
		if (strcmp(value, "install") == 0)
			return 0;
		if (strcmp(value, "recover") == 0)
			return 0;
		if (strcmp(value, "factory-reset") == 0)
			return 0;
	}
	return 1;
}
U_BOOT_ENV_CALLBACK(snapd_mode, on_snapd_mode);

static int on_kernel_status(const char *name, const char *value, enum env_op op,
	int flags)
{
	/* only empty/try/trying values are permitted */
	if (strcmp(name, "kernel_status") == 0) {
		if (strcmp(value, "try") == 0)
			return 0;
		if (strcmp(value, "trying") == 0)
			return 0;
		if (strlen(value) == 0)
			return 0;
	}
	return 1;
}
U_BOOT_ENV_CALLBACK(kernel_status, on_kernel_status);

/* snap name has to be validated against "[a-z0-9-]*[a-z][a-z0-9-]*" */
static int validate_snap_name(const char *start, const char *end) {
    if (start == end)
		return 0;
    bool has_letter = 0;

    for (const char *p = start; p < end; p++) {
        if (islower(*p)) {
            has_letter = 1;
        } else if (!isdigit(*p) && *p != '-') {
            return 0; // Invalid character
        }
    }
    return has_letter;
}

/* revision  has to be validated against "[a-zA-Z0-9]+" */
static int validate_snap_revision(const char *start, const char *end) {
    if (start == end)
		return 0; /* can't be empty*/

    for (const char *p = start; p < end; p++) {
        if (!isalnum(*p)) {
            return 0;
        }
    }
    return 1;
}

static int on_snap_file_name(const char *name, const char *value, enum env_op op,
	int flags)
{
	printf("## Validating snap file: [%s] [%s]\n", name, value);
	/* find underscore, it cannot be at the start */
	const char *underscore = strrchr(value, '_');
	if (!underscore || underscore == value)
		return 1;

	/* check for .snap suffix and that it's at the very end */
	const char *suffix = strstr(underscore, ".snap");
	if (!suffix || strcmp(suffix, ".snap") != 0)
		return 1;

	/* validate name part */
	if (!validate_snap_name(value, underscore))
		return 1;
	/* validate revision part */
	if (!validate_snap_revision(underscore + 1, suffix))
		return 1;

	return 0;
}
U_BOOT_ENV_CALLBACK(snap_file, on_snap_file_name);

/* snapd system  has to be validated against "^[a-z0-9](?:-?[a-z0-9])*$" */
static int on_snapd_system(const char *name, const char *value, enum env_op op,
	int flags)
{
	if (value == NULL || *value == '\0')
        return 1;

    // Must start with alphanumeric (lowercase/digit)
    if (!islower(*value) && !isdigit(*value))
        return 1;

    const char *p = value + 1;
    while (*p != '\0') {
        if (*p == '-') {
            // A hyphen must be followed by an alphanumeric character
            // This prevents "abc-" (end hyphen) and "abc--def" (double hyphen)
            char next = *(p + 1);
            if (!islower(next) && !isdigit(next)) {
                return 1;
            }
            p += 2;
        } else if (islower(*p) || isdigit(*p)) {
            p++;
        } else {
            /* any other character is invalid */
            return 1;
        }
    }
	return 0;
}
U_BOOT_ENV_CALLBACK(snapd_system, on_snapd_system);

/*
 * The kernel arguments snapd keeps for an A/B system, which the boot env appends
 * to the command line it builds itself: snapd_full_cmdline_args, and
 * snapd_try_cmdline_args for the boot of a trial slot, named as in snapd's grub
 * configuration.
 *
 * Only their shape is checked here; which arguments a gadget allows is snapd's
 * to decide. U-Boot measures what it boots, so an argument snapd did not seal to
 * only fails the unlock of the data. What this keeps an env edited offline from
 * doing without the data: setting what the boot itself passes, the gate word
 * fixup_secure_linux() appends last, or what runs before the initrd measures
 * the slot, but for systemd-bootchart as init. A value refused when the stored
 * env is loaded is dropped alone.
 */
#define SNAPD_CMDLINE_MAX_LEN	1024

static const char *const snapd_cmdline_refused[] = {
	/* what the boot env passes */
	"snapd_recovery_mode", "snapd_recovery_system", "snapd_system_disk",
	"uc_slot", "uc_fde", "system_a", "system_b", "apps_a", "apps_b",
	"dm-mod.create",
	/* what runs before the slot is measured, but SNAPD_CMDLINE_BOOTCHART */
	"init", "rdinit",
	NULL
};

/*
 * The one init that may be given: systemd-bootchart, which starts systemd
 * itself once it has started recording, so a system can be booted to have its
 * boot charted.
 */
#define SNAPD_CMDLINE_BOOTCHART	"init=/lib/systemd/systemd-bootchart"

/* compare a parameter name with '-' and '_' as one, as the kernel does */
static bool snapd_cmdline_param_is(const char *param, size_t len,
				   const char *name)
{
	size_t i;

	if (strlen(name) != len)
		return false;
	for (i = 0; i < len; i++) {
		char a = param[i] == '-' ? '_' : param[i];
		char b = name[i] == '-' ? '_' : name[i];

		if (a != b)
			return false;
	}
	return true;
}

static int on_snapd_cmdline(const char *name, const char *value,
			    enum env_op op, int flags)
{
	const char *p, *word, *end;
	size_t len;
	int i;

	if (op == env_op_delete || !value)
		return 0;

	if (strlen(value) > SNAPD_CMDLINE_MAX_LEN) {
		printf("## %s: refusing arguments over %d bytes\n", name,
		       SNAPD_CMDLINE_MAX_LEN);
		return 1;
	}
	/*
	 * Printable characters, and none a shell, a quoted kernel argument or a
	 * substitution in bootargs would give a meaning of its own
	 */
	for (p = value; *p; p++) {
		if (*p < 0x20 || *p > 0x7e || strchr("\"'`;\\$", *p)) {
			printf("## %s: refusing the character 0x%02x\n", name,
			       (unsigned char)*p);
			return 1;
		}
	}

	for (word = value; *word; word = end) {
		while (*word == ' ')
			word++;
		if (!*word)
			break;
		for (end = word; *end && *end != ' '; end++)
			;
		/* the gate word, which fixup_secure_linux() appends last */
		if ((end - word == 9 && !strncmp(word, "dangerous", 9)) ||
		    (end - word == 8 && !strncmp(word, "panic=-1", 8))) {
			printf("## %s: refusing \"%.*s\", which the boot appends last\n",
			       name, (int)(end - word), word);
			return 1;
		}
		if ((size_t)(end - word) == strlen(SNAPD_CMDLINE_BOOTCHART) &&
		    !strncmp(word, SNAPD_CMDLINE_BOOTCHART, end - word))
			continue;
		for (len = 0; word + len < end && word[len] != '='; len++)
			;
		for (i = 0; snapd_cmdline_refused[i]; i++) {
			if (snapd_cmdline_param_is(word, len,
						   snapd_cmdline_refused[i])) {
				printf("## %s: refusing \"%.*s\", which the boot sets\n",
				       name, (int)len, word);
				return 1;
			}
		}
	}
	return 0;
}
U_BOOT_ENV_CALLBACK(snapd_cmdline, on_snapd_cmdline);
