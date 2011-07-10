#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/hardirq.h>

#include "cch_index_debug.h"

#define TRACE_BUF_SIZE 512
static char trace_buf[TRACE_BUF_SIZE];
static DEFINE_SPINLOCK(trace_buf_lock);

static inline int get_current_tid(void)
{
	/* Code should be the same as in sys_gettid() */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
	return current->pid;
#else
	if (in_interrupt()) {
		/*
		 * Unfortunately, task_pid_vnr() isn't IRQ-safe, so otherwise
		 * it can oops. ToDo.
		 */
		return 0;
	}
	return task_pid_vnr(current);
#endif
}

/**
 * debug_print_prefix() - print debug prefix for a log line
 *
 * Prints, if requested by trace_flag, debug prefix for each log line
 */
int cch_debug_print_prefix(unsigned long trace_flag,
	const char *prefix, const char *func, int line)
{
	int i = 0;
	unsigned long flags;
	int pid = get_current_tid();

	spin_lock_irqsave(&trace_buf_lock, flags);

	trace_buf[0] = '\0';

	if (trace_flag & TRACE_PID)
		i += snprintf(&trace_buf[i], TRACE_BUF_SIZE, "[%d]: ", pid);
	if (prefix != NULL)
		i += snprintf(&trace_buf[i], TRACE_BUF_SIZE - i, "%s: ",
			      prefix);
	if (trace_flag & TRACE_FUNCTION)
		i += snprintf(&trace_buf[i], TRACE_BUF_SIZE - i, "%s:", func);
	if (trace_flag & TRACE_LINE)
		i += snprintf(&trace_buf[i], TRACE_BUF_SIZE - i, "%i:", line);

	PRINTN(KERN_INFO, "%s", trace_buf);

	spin_unlock_irqrestore(&trace_buf_lock, flags);

	return i;
}
EXPORT_SYMBOL(cch_debug_print_prefix);
