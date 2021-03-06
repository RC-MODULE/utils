#include <linux/dvb/dmx.h>
#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>

int g_verbosity = 2;
#define USAGE "\
Sets QPSK DVB frontend's parameters and waits for lock\n\
Usage:\n\
\ttuneqpsk OPTS [-M] FREQ POL SYMRATE FEC\n\n\
\t\t-a N\t\tuse /dev/adapterN (default 0)\n\
\t\t-f N\t\tuse /dev/adapter?/frontendN (default 0)\n\
\t\t-l N\t\tlimit number of attemts to N\n\
\t\t-s DISEQC\tset diseqc position (0..3)\n\
\t\t-i INV\t\tset invertion (0-off 1-on 2-auto)\n\
\t\t-x\t\texit after lock\n\
\t\t-L LNB\t\tlnb type index (default 0, ? for list)\n\
\t\t-q\t\tquiet mode\n\
\t\tFREQ\t\tfrequency [MHz with -M, kHz without -M]\n\
\t\tPOL\t\tpolarisation ('H'|'V'|'L'|'R')\n\
\t\tSYMRATE\t\tsymbol rate [Sym/sec]\n\
\t\tFEC\t\tforward error correction mode (NONE,1_2,3_4...8_9,AUTO)\n\
Files:\n\
\t\t/var/tuneqpsk\tLast executed command line\n\
"

#include "dbgmacro.h"

struct diseqc_cmd {
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};

struct diseqc_cmd switch_cmds[] = {
  // diseqc 0 low_band V
	{ { { 0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xf2, 0x00, 0x00 }, 4 }, 0 },
  //              hi_band  V
	{ { { 0xe0, 0x10, 0x38, 0xf1, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xf3, 0x00, 0x00 }, 4 }, 0 },
  // diseqc 1 low_band V
	{ { { 0xe0, 0x10, 0x38, 0xf4, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xf6, 0x00, 0x00 }, 4 }, 0 },
  //              hi_band  V
	{ { { 0xe0, 0x10, 0x38, 0xf5, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xf7, 0x00, 0x00 }, 4 }, 0 },
  // diseqc 2 low_band V
	{ { { 0xe0, 0x10, 0x38, 0xf8, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xfa, 0x00, 0x00 }, 4 }, 0 },
  //              hi_band  V
	{ { { 0xe0, 0x10, 0x38, 0xf9, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xfb, 0x00, 0x00 }, 4 }, 0 },
  // diseqc 3 low_band V
	{ { { 0xe0, 0x10, 0x38, 0xfc, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xfe, 0x00, 0x00 }, 4 }, 0 },
  //              hi_band  V
	{ { { 0xe0, 0x10, 0x38, 0xfd, 0x00, 0x00 }, 4 }, 0 },
  //                       H
	{ { { 0xe0, 0x10, 0x38, 0xff, 0x00, 0x00 }, 4 }, 0 }
};

static inline void msleep(uint32_t msec)
{
  struct timespec req = { msec / 1000, 1000000 * (msec % 1000) };
  while (nanosleep(&req, &req)) {}
}

inline const char* tone2str(fe_sec_tone_mode_t t)
{
  switch(t) {
    case SEC_TONE_OFF: return "SEC_TONE_OFF";
    case SEC_TONE_ON: return "SEC_TONE_ON";
    default: return "?";
  }
}

inline const char* burst2str(fe_sec_mini_cmd_t t)
{
  switch(t) {
    case SEC_MINI_A: return "SEC_MINI_A";
    case SEC_MINI_B: return "SEC_MINI_B";
    default: return "?";
  }
}

int diseqc_send_msg (int fd, fe_sec_voltage_t v, struct diseqc_cmd **cmd,
  fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b)
{
  int err;

  debug("disabling the tone");
  if ((err = ioctl(fd, FE_SET_TONE, SEC_TONE_OFF))) {
    errorn(errno, "Unable to turn off the tone, %d", err);
    return err;
  }

  debug("performing FE_SET_VOLTAGE syscall, value %d", v);
  if ((err = ioctl(fd, FE_SET_VOLTAGE, v))) {
    errorn(errno, "Unable to set voltage mode %d, %d", v, err);
    return err;
  }

  msleep(15);
  while (*cmd) {
    debug("DiSEqC: %02x %02x %02x %02x %02x %02x",
      (*cmd)->cmd.msg[0], (*cmd)->cmd.msg[1],
      (*cmd)->cmd.msg[2], (*cmd)->cmd.msg[3],
      (*cmd)->cmd.msg[4], (*cmd)->cmd.msg[5]);

    if ((err = ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &(*cmd)->cmd))) {
      errorn(errno, "Unable send FE_DISEQC_SEND_MASTER_CMD, %d", err);
      return err;
    }

    msleep((*cmd)->wait);
    cmd++;
  }

  msleep(15);

  debug("performing FE_DISEQC_SEND_BURST syscall, value %d (%s)", b, burst2str(b));
  if ((err = ioctl(fd, FE_DISEQC_SEND_BURST, b))) {
    errorn(errno, "unable to send FE_DISEQC_SEND_BURST, %d", err);
    return err;
  }

  msleep(15);

  debug("performing FE_SET_TONE syscall, value %d (%s)", t, tone2str(t));
  if((err = ioctl(fd, FE_SET_TONE, t))) {
    errorn(errno, "unable to send FE_SET_TONE, %d", err);
    return err;
  }

  return 0;
}

int diseqc_setup_switch (int frontend_fd, int switch_pos, int voltage_18, int hiband)
{
  struct diseqc_cmd *cmd[2] = { NULL, NULL };
  int i = 4 * switch_pos + 2 * hiband + (voltage_18 ? 1 : 0);

  verbose("switch pos %i, %sV, %sband (index %d)",
    switch_pos, voltage_18 ? "18" : "13", hiband ? "hi" : "lo", i);

  if (i < 0 || i >= (int) (sizeof(switch_cmds)/sizeof(struct diseqc_cmd))) {
    error("invalid diseqc command index %d", i);
    return -EINVAL;
  }

  cmd[0] = &switch_cmds[i];

  return diseqc_send_msg (frontend_fd,
    // FIXME: check voltage_18 compatibility
    i % 2 ? SEC_VOLTAGE_18 : SEC_VOLTAGE_13,
    cmd,
    (i/2) % 2 ? SEC_TONE_ON : SEC_TONE_OFF,
    (i/4) % 2 ? SEC_MINI_B : SEC_MINI_A);
}

struct lnb_type {
	char	*name;
	const char	*desc;
	unsigned long	low_val; /* kHz */
	unsigned long	high_val;	/* kHz, zero indicates no hiband */
	unsigned long	switch_val;	/* kHz, zero indicates no hiband */
};

static const char *univ_desc2 = "Europe/Russia\n"
  "\t10800 to 11800 MHz and 11600 to 12700 Mhz\n"
  "\tDual LO, loband 9750, hiband 10750 MHz";

static const char *univ_desc = "Europe\n"
  "\t10800 to 11800 MHz and 11600 to 12700 Mhz\n"
  "\tDual LO, loband 9750, hiband 10600 MHz";

static const char *dbs_desc =
  "\tExpressvu, North America\n"
  "\t12200 to 12700 MHz\n"
  "\tSingle LO, 11250 MHz";

static const char *standard_desc =
  "\t10945 to 11450 Mhz\n"
  "\tSingle LO, 10000 Mhz";

static const char *enhan_desc =
  "\tAstra\n"
  "\t10700 to 11700 MHz\n"
  "\tSingle LO, 9750 MHz";

static const char *cband_desc =
  "\tBig Dish\n"
  "\t3700 to 4200 MHz\n"
  "\tSingle LO, 5150 Mhz";

typedef enum polarisation {
	POLARISATION_HORIZONTAL     = 0x00,
	POLARISATION_VERTICAL       = 0x01,
	POLARISATION_CIRCULAR_LEFT  = 0x02,
	POLARISATION_CIRCULAR_RIGHT = 0x03
} polaris_t;

struct tune_request {
  /* frontend params */
  uint32_t freq_khz;
  fe_spectral_inversion_t inversion;
  int32_t symbol_rate;
  fe_code_rate_t coderate;

  /* diseqc related */
  polaris_t polaris;
  int diseqc_pos;
};

static int tune_to_transponder(int frontend_fd,
  struct tune_request *rq,
  struct lnb_type *lnb_type,
  int attempts /* -1 to infinite */,
  int break_on_lock)
{
  struct dvb_frontend_parameters p;
  fe_status_t s;
  int i;
  int err;
  int locked;
  struct dvb_frontend_info fe_info = {
    .type = -1
  };
  int adjusted_freq_khz;
  int freq_khz = rq->freq_khz;

	if ((err = ioctl(frontend_fd, FE_GET_INFO, &fe_info))) {
		error("ioctl FE_GET_INFO failed: %d", err);
    return err;
  }

  if(fe_info.type != FE_QPSK) {
		error("non-QPSK frontends are not supported (got %d)", fe_info.type);
    return -EINVAL;
  }

  if (lnb_type->high_val) {
    if (lnb_type->switch_val) {
      int hiband = 0;

      if (freq_khz >= lnb_type->switch_val)
        hiband = 1;

      info("using voltage-controlled switch frequency correction (%s)",
        hiband ? "hiband" : "loband" );

      if((err = diseqc_setup_switch (frontend_fd,
          rq->diseqc_pos,
          rq->polaris == POLARISATION_VERTICAL ? 0 : 1,
          hiband))) {
        error("failed to setup diseqc switch, %d", err);
        return err;
      }

      usleep(50000);
      if (hiband)
        adjusted_freq_khz = abs(freq_khz - lnb_type->high_val);
      else
        adjusted_freq_khz = abs(freq_khz - lnb_type->low_val);
    }
    else {
      /* lnb_type.switch_val == 0 */
      info("using C-Band Multipoint LNBf frequency correction");
      adjusted_freq_khz = abs(freq_khz - 
        (rq->polaris == POLARISATION_VERTICAL ?
          lnb_type->low_val : lnb_type->high_val));
    }
  } 
  else { 
    /* lnb_type.high_val == 0 */
    info("using monopoint LNBf without switch frequency correction");
    adjusted_freq_khz = abs(freq_khz - lnb_type->low_val);
  }

  p.frequency = adjusted_freq_khz;
  p.inversion = rq->inversion;
  p.u.qpsk.symbol_rate = rq->symbol_rate;
  p.u.qpsk.fec_inner = rq->coderate;
  debug("setting qpsk_params: freq %d (kHz), inversion %d, symrate %d, fec %d",
    p.frequency, p.inversion, p.u.qpsk.symbol_rate, p.u.qpsk.fec_inner);

  if ((err = ioctl(frontend_fd, FE_SET_FRONTEND, &p))) {
    errorn(errno, "setting frontend parameters failed");
    return err;
  }

  locked = 0;
  while(1) {
    if ((err = ioctl(frontend_fd, FE_READ_STATUS, &s))) {
      errorn(errno, "unable to get frontend status");
      return err;
    }

    locked = s & FE_HAS_LOCK;

    info("tuning status: 0x%02x %s", s,
      locked ? "LOCKED" : "");

    if(break_on_lock) {
      if (locked)
        break;
    }

    if(attempts > 0) {
      attempts--;
      if(attempts == 0)
        break;
    }

    usleep (200000);
  }

  if(attempts == 0) {
    error("tuning timeout reached");
    return -1;
  }

  return 0;
}

/*{{{ main program*/
struct strtab {
	const char *str;
	int val;
};

static const char* enum2str(int val, const struct strtab *tab, const char *deflt)
{
	while (tab->str) {
		if (tab->val == val)
			return tab->str;
		tab++;
	}
	return deflt;
}

static int str2enum(const char *str, const struct strtab *tab, int deflt)
{
	while (tab->str) {
		if (!strcmp(tab->str, str))
			return tab->val;
		tab++;
	}
	return deflt;
}

struct strtab fectab[] = {
  { "NONE", FEC_NONE },
  { "1/2",  FEC_1_2 },
  { "1_2",  FEC_1_2 },
  { "2/3",  FEC_2_3 },
  { "2_3",  FEC_2_3 },
  { "3/4",  FEC_3_4 },
  { "3_4",  FEC_3_4 },
  { "4/5",  FEC_4_5 },
  { "4_5",  FEC_4_5 },
  { "5/6",  FEC_5_6 },
  { "5_6",  FEC_5_6 },
  { "6/7",  FEC_6_7 },
  { "6_7",  FEC_6_7 },
  { "7/8",  FEC_7_8 },
  { "7_8",  FEC_7_8 },
  { "8/9",  FEC_8_9 },
  { "8_9",  FEC_8_9 },
  { "AUTO", FEC_AUTO },
  { NULL, 0 }
};

static int str2fec(const char *fec)
{
	return str2enum(fec, fectab, -1);
}

static const char* fec2str(int fec)
{
	return enum2str(fec, fectab, "UNKNOWN");
}

int frontend_open(int adapter, int frontend)
{
    char frontend_devname [80];
    int fd;

    snprintf (frontend_devname, sizeof(frontend_devname),
        "/dev/dvb/adapter%i/frontend%i", adapter, frontend);

    if ((fd = open(frontend_devname, O_RDWR)) < 0) {
      error("failed to open '%s', errno %d", frontend_devname, errno);
      return -1;
    }

    return fd;
}

int frontend_query(
  int frontend_fd,
  struct lnb_type *lnb_type)
{
  int adjusted_freq_khz;
  int freq_khz;
  int ret;
  struct dvb_frontend_parameters p;
  struct dvb_frontend_info fe_info = {
    .type = -1
  };

	if ((ret = ioctl(frontend_fd, FE_GET_INFO, &fe_info))) {
		error("ioctl FE_GET_INFO failed: %d", ret);
    return ret;
  }

  if(fe_info.type != FE_QPSK) {
		error("non-QPSK frontends are not supported (got %d)", fe_info.type);
    return -EINVAL;
  }

  if ((ret = ioctl(frontend_fd, FE_GET_FRONTEND, &p))) {
    errorn(errno, "getting frontend parameters failed");
    return ret;
  }

  adjusted_freq_khz = p.frequency;

  if (lnb_type->high_val) {
    if (lnb_type->switch_val) {
      int hiband = 0;

      if (freq_khz >= lnb_type->switch_val)
        hiband = 1;

      info("using voltage-controlled switch frequency correction (%s)",
        hiband ? "hiband" : "loband" );

      if (hiband)
        /* adjusted_freq_khz = abs(freq_khz - lnb_type->high_val); */
        freq_khz = adjusted_freq_khz + lnb_type->high_val;
      else
        /* adjusted_freq_khz = abs(freq_khz - lnb_type->low_val); */
        freq_khz = adjusted_freq_khz + lnb_type->low_val;
    }
    else {
      /* lnb_type.switch_val == 0 */
      error("Querying while in C-Band Multipoint LNBf frequency mode "
        "requires polarisation to be known. This mode is not implemented");
      return -1;
    }
  } 
  else { 
    /* lnb_type.high_val == 0 */
    info("using monopoint LNBf without switch frequency correction");
    /*adjusted_freq_khz = abs(freq_khz - lnb_type->low_val);*/
    freq_khz = adjusted_freq_khz + lnb_type->low_val;
  }

  printf("freq_khz %d symrate %d fec %s inversion %d\n",
    p.frequency,
    p.u.qpsk.symbol_rate,
    fec2str(p.u.qpsk.fec_inner),
    p.inversion);

  return 0;
}

int main(int argc, char **argv)
{
	int err;
	int fd0;
  char pol[20];
  char fec[20];
	int opt;
  int frontend;
  int adapter;
  uint32_t freq;
  fe_spectral_inversion_t inversion;
  int32_t symbol_rate;
  int coderate;
  polaris_t polaris;
  int diseqc_pos;
  int attempts;
  int break_on_lock;
  int quiet;
  enum{kHz, MHz} measure;
  struct lnb_type *lnb;
  int lnb_index;
  int query = 0;

  struct lnb_type lnbs[] = {
    {"UNIVERSAL2", univ_desc2,		  9750*1000,   10750*1000,  11700*1000 },
    {"UNIVERSAL",	 univ_desc,		    9750*1000,   10600*1000,  11700*1000 },
    {"DBS",		     dbs_desc, 		   11250*1000,   0,           0 },
    {"STANDARD",	 standard_desc,  10000*1000,   0,           0 },
    {"ENHANCED",	 enhan_desc,		  9750*1000,   0,           0 },
    {"C-BAND",	   cband_desc,		  5150*1000,   0,           0 }
  };

  g_verbosity = 2;
  diseqc_pos = 0;
  adapter = 0;
  frontend = 0;
  attempts = -1;
  diseqc_pos = 0;
  break_on_lock = 0;
  measure = kHz;
  lnb = &lnbs[0];
  quiet = 0;
  lnb_index = 0;
  inversion = 0;

  while ((opt = getopt(argc, argv, "L:qhMvf:xl:a:s:Q")) != -1) {
		switch (opt) {
    case 'h':
      fprintf(stderr, "%s", USAGE);
      exit(-1);
      break;
		case 'a':
			adapter = strtoul(optarg, NULL, 0);
			break;
		case 's':
			diseqc_pos = strtoul(optarg, NULL, 0);
			break;
    case 'l':
			attempts = strtoul(optarg, NULL, 0);
			break;
    case 'x':
			break_on_lock = 1;
			break;
    case 'f':
			frontend = strtoul(optarg, NULL, 0);
			break;
    case 'v':
      g_verbosity++;
      break;
    case 'q':
      quiet = 1;
      break;
    case 'M':
      measure = MHz;
      break;
    case 'L':
      if(optarg[0] == '?') {
        printf("Supported LNB types:\n");
        for(lnb_index=0;lnb_index<sizeof(lnbs)/sizeof(lnbs[0]); lnb_index++) {
          lnb = &lnbs[lnb_index];
          printf("%d\t%s\t%s\tlow:%lu hi:%lu switch:%lu\n", lnb_index,
            lnb->name, lnb->desc, lnb->low_val, lnb->high_val, lnb->switch_val);
        }
        exit(-1);
      }
			lnb_index = strtoul(optarg, NULL, 0);
      break;
    case 'Q':
      query = 1;
      break;
		default:
      die_usage_();
    }
  }

  if(lnb_index<0 || lnb_index>=(sizeof(lnbs)/sizeof(lnbs[0]))) {
    die_usage("Invalid lnb-type index");
  }
  lnb = &lnbs[lnb_index];

  if(quiet) {
    g_verbosity = 0;
  }

  if(query) {
    int fd;

    fd = frontend_open(adapter, frontend);
    if(fd < 0) {
      error("unable to open frontend");
      return -1;
    }
    err = frontend_query(fd, lnb);
    return err;
  }

	if(argc < optind+4 ) {
    die_usage("too few arguments provided (got %d, neead at least %d more)",
      argc, (optind+4 - argc));
	}

  sscanf(argv[optind], "%u", &freq);
  sscanf(argv[optind+1], "%1[HVLR]", pol);
  switch(pol[0]) {
    case 'H':
    case 'R':
      polaris = POLARISATION_HORIZONTAL;
      argv[optind+1] = "R";
      break;
    case 'V':
    case 'L':
      polaris = POLARISATION_VERTICAL;
      argv[optind+1] = "L";
      break;
    default:
      die_usage("invalid polarisation value %c", pol[0]);
  }

  sscanf(argv[optind+2], "%u", &symbol_rate);
  sscanf(argv[optind+3], "%4s", fec);
  coderate = str2fec(fec);
  if(coderate < 0) {
    die_usage("invalid fec");
  }

  do {
    struct tune_request rq = {
      .freq_khz = (measure == MHz ? freq*1000 : freq),
      .inversion = inversion,
      .symbol_rate = symbol_rate,
      .coderate = (fe_code_rate_t)coderate,
      .polaris = polaris,
      .diseqc_pos = diseqc_pos,
    };

    int fd;

    fd = frontend_open(adapter, frontend);
    if(fd < 0) {
      error("unable to open frontend");
      return -1;
    }

    debug("using lnb #%d, %s", lnb_index, lnb->desc);

    if((err = tune_to_transponder(fd, &rq, lnb, attempts, break_on_lock))) {
      error("tuning failed, error %d", err);
      return -1;
    }
  } while(0);


  /* Save command line to /var/tuneqpsk file */
  do {
    int cf;
    int i;

    cf = open("/var/tuneqpsk", O_WRONLY|O_CREAT|O_TRUNC, 00400);
    if(cf < 0)
      error("unable to open /var/tuneqpsk");

    for(i=0; i<argc; i++) {
      write(cf, argv[i], strlen(argv[i]));
      write(cf," ",1);
    }

    write(cf,"\n",1);
    close(cf);

  } while(0);

	return 0;
}
/*}}}*/

