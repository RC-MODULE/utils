#ifndef _MDVB_DBGMACRO_H_
#define _MDVB_DBGMACRO_H_

extern int g_verbosity;

#ifdef __cplusplus
extern "C"
{
#endif

#define dprintf(level, strm, fmt, args...) \
	do { \
		if (level <= g_verbosity) \
      fprintf(strm, "%s:%d %s: " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##args); \
	} while (0)

#define errorn(e, fmt, msg...)   dprintf(0, stderr, fmt ", errno %d (%s)", ##msg, e, strerror(e))
#define error(fmt, msg...)   dprintf(0, stderr, fmt, ##msg)
#define warning(fmt, msg...) dprintf(1, stderr, fmt, ##msg)
#define info(fmt, msg...)    dprintf(2, stderr, fmt, ##msg)
#define verbose(fmt, msg...) dprintf(3, stderr, fmt, ##msg)
#define debug(fmt, msg...)   dprintf(4, stderr, fmt, ##msg)

void print_usage(void);

inline static void die_usage_() 
{
#ifdef USAGE
    fprintf(stderr, USAGE);
#else
    print_usage();
#endif
		exit(-1);
}

#define die_usage(args...) \
  do { error(args); die_usage_(); } while(0)

#ifdef __cplusplus
}
#endif

#endif
