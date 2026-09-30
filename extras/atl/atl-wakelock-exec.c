/* atl-wakelock-exec CMD [ARGS...]
 * Runs CMD with CAP_BLOCK_SUSPEND as an ambient capability, so an app under
 * Android Translation Layer can take kernel wakelocks (/sys/power/wake_lock
 * needs that capability, not just write access). This binary carries the
 * capability as a file capability; ATL itself can't, because a file capability
 * makes the dynamic linker ignore LD_LIBRARY_PATH/LD_PRELOAD (secure exec).
 * Install: root:wheel 0750, security.capability = cap_block_suspend+p
 * (see install-atl-wakelock-exec.sh). */
#include <linux/capability.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	struct __user_cap_header_struct header = { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct data[2];

	if (argc < 2) {
		fprintf(stderr, "usage: %s CMD [ARGS...]\n", argv[0]);
		return 2;
	}
	/* ambient needs the capability in both the permitted and the inheritable set */
	if (syscall(SYS_capget, &header, data) == 0) {
		data[CAP_TO_INDEX(CAP_BLOCK_SUSPEND)].inheritable |= CAP_TO_MASK(CAP_BLOCK_SUSPEND);
		if (syscall(SYS_capset, &header, data) != 0 ||
		    prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_BLOCK_SUSPEND, 0, 0) != 0)
			perror("atl-wakelock-exec: no CAP_BLOCK_SUSPEND, running without kernel wakelocks");
	}
	execvp(argv[1], argv + 1);
	perror(argv[1]);
	return 127;
}
