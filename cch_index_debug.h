#ifndef CCH_INDEX_DEBUG_H
#define CCH_INDEX_DEBUG_H

#define CCH_INDEX_DEBUG

#define TRACE_NULL           0x00000000
#define TRACE_DEBUG          0x00000001
#define TRACE_FUNCTION       0x00000002
#define TRACE_LINE           0x00000004
#define TRACE_PID            0x00000008

#ifdef CCH_INDEX_DEBUG

#define sBUG() do {						\
	printk(KERN_CRIT "BUG at %s:%d\n",			\
	       __FILE__, __LINE__);				\
	local_irq_enable();					\
	while (in_softirq())					\
		local_bh_enable();				\
	BUG();							\
} while (0)

#define sBUG_ON(p) do {						\
	if (unlikely(p)) {					\
		printk(KERN_CRIT "BUG at %s:%d (%s)\n",		\
		       __FILE__, __LINE__, #p);			\
		local_irq_enable();				\
		while (in_softirq())				\
			local_bh_enable();			\
		BUG();						\
	}							\
} while (0)

#else

#define sBUG() BUG()
#define sBUG_ON(p) BUG_ON(p)

#endif /* CCH_INDEX_DEBUG */

/*
 * Note: in the next two printk() statements the KERN_CONT macro is only
 * present to suppress a checkpatch warning (KERN_CONT is defined as "").
 */
#define PRINT(log_flag, format, args...)  \
		printk(log_flag format "\n", ## args)
#define PRINTN(log_flag, format, args...) \
		printk(log_flag format, ## args)

#ifdef LOG_PREFIX
#define __LOG_PREFIX	LOG_PREFIX
#else
#define __LOG_PREFIX	NULL
#endif

int debug_print_prefix(unsigned long trace_flag,
	const char *prefix, const char *func, int line);

#ifdef CCH_INDEX_DEBUG

#define TRACE(trace, format, args...)					\
do {									\
	if (___unlikely(trace_flag & (trace))) {			\
		debug_print_prefix(trace_flag, __LOG_PREFIX,		\
				       __func__, __LINE__);		\
		PRINT(KERN_CONT, format, args);				\
	}								\
} while (0)

#ifdef LOG_PREFIX

#define PRINT_INFO(format, args...)				\
do {								\
	PRINT(KERN_INFO, "%s: " format, LOG_PREFIX, args);	\
} while (0)

#define PRINT_WARNING(format, args...)          \
do {                                            \
	PRINT(KERN_INFO, "%s: ***WARNING***: "	\
	      format, LOG_PREFIX, args);	\
} while (0)

#define PRINT_ERROR(format, args...)            \
do {                                            \
	PRINT(KERN_INFO, "%s: ***ERROR***: "	\
	      format, LOG_PREFIX, args);	\
} while (0)

#define PRINT_CRIT_ERROR(format, args...)       \
do {                                            \
	PRINT(KERN_INFO, "%s: ***CRITICAL ERROR***: "	\
		format, LOG_PREFIX, args);		\
} while (0)

#else

#define PRINT_INFO(format, args...)		\
do {                                            \
	PRINT(KERN_INFO, format, args);		\
} while (0)

#define PRINT_WARNING(format, args...)          \
do {                                            \
	PRINT(KERN_INFO, "***WARNING***: "	\
		format, args);			\
} while (0)

#define PRINT_ERROR(format, args...)		\
do {                                            \
	PRINT(KERN_ERR, "***ERROR***: "		\
		format, args);			\
} while (0)

#define PRINT_CRIT_ERROR(format, args...)		\
do {							\
	PRINT(KERN_CRIT, "***CRITICAL ERROR***: "	\
		format, args);				\
} while (0)

#endif /* LOG_PREFIX */

#define TRACE_ENTRY() do {				\
		PRINT(KERN_INFO, "ENTRY %s", __func__);	\
	} while (0)

#define TRACE_EXIT() do {				\
		PRINT(KERN_INFO, "ENTRY %s", __func__);	\
	} while (0)

#else

#define TRACE(trace, format, args...) do {} while(0);

#define TRACE_ENTRY() do {} while (0);

#define PRINT_BUFFER(message, buff, len)                            \
do {                                                                \
	PRINT(KERN_INFO, "%s:", message);			    \
	debug_print_buffer(buff, len);				    \
} while (0)

#endif

#endif /* CCH_INDEX_DEBUG_H */
