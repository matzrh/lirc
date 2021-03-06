/****************************************************************************
 ** lircd.c *****************************************************************
 ****************************************************************************
 *
 * lircd - LIRC Decoder Daemon
 *
 * Copyright (C) 1996,97 Ralph Metzler <rjkm@thp.uni-koeln.de>
 * Copyright (C) 1998,99 Christoph Bartelmus <lirc@bartelmus.de>
 *
 *  =======
 *  HISTORY
 *  =======
 *
 * 0.1:  03/27/96  decode SONY infra-red signals
 *                 create mousesystems mouse signals on pipe /dev/lircm
 *       04/07/96  send ir-codes to clients via socket (see irpty)
 *       05/16/96  now using ir_remotes for decoding
 *                 much easier now to describe new remotes
 *
 * 0.5:  09/02/98 finished (nearly) complete rewrite (Christoph)
 *
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif


#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>

#include "lirc/lirc_log.h"

#define HOSTNAME_LEN 128
char hostname[HOSTNAME_LEN + 1];
FILE *lf = NULL;

loglevel_t loglevel = LIRC_NOLOG;

static int use_syslog = 1;

const char *syslogident = "lircd-" VERSION;
const char *logfile = "syslog";

char progname[128] = {'?','\0'};
static int nodaemon = 0;


static const char* prio2text(int prio)
{
	switch (prio) {
		case LIRC_DEBUG:	return "Debug";
		case LIRC_NOTICE:	return "Notice";
		case LIRC_INFO:		return "Info";
		case LIRC_WARNING:	return "Warning";
		case LIRC_ERROR:	return "Error";
		case LIRC_TRACE:        return "Trace";
		case LIRC_PEEP:         return "Trace1";
		case LIRC_STALK:        return "Trace2";
		default:		return "(Bad prio)";
	}
}


int lirc_log_use_syslog()
{
	return use_syslog;
}


void lirc_log_set_file(const char* s)
{
	if (strcmp(s, "syslog") == 0) {
	    use_syslog  = 1;
	} else {
	    logfile = s;
	    use_syslog = 0;
	}
}


int lirc_log_open(const char* _progname, int _nodaemon, loglevel_t level)
{
	strncpy(progname, _progname, sizeof(progname));
	nodaemon = _nodaemon;
	loglevel = level;

	if (use_syslog) {
		if (nodaemon) {
			openlog(syslogident, LOG_CONS | LOG_PID | LOG_PERROR, LOG_LOCAL0);
		} else {
			openlog(syslogident, LOG_CONS | LOG_PID, LOG_LOCAL0);
		}
	} else {
		lf = fopen(logfile, "a");
		if (lf == NULL) {
			fprintf(stderr, "%s: could not open logfile \"%s\"\n",
                                progname, logfile);
			perror(progname);
			return 1;
		}
		gethostname(hostname, HOSTNAME_LEN);
	}
	return 0;
}


int lirc_log_close()
{
	if (use_syslog){
		closelog();
		return(0);
	}
	else if (lf)
		return( fclose(lf));
	else
		return(0);
}


int lirc_log_reopen(void)
{
	struct stat s;

	if (use_syslog)
		/* Don't need to do anything; this is syslogd's task */
		return(0);

	logprintf(LIRC_INFO, "closing logfile");
	if (-1 == fstat(fileno(lf), &s)) {
		perror("Invalid logfile!");
		return -1;
	}
	fclose(lf);
	lf = fopen(logfile, "a");
	if (lf == NULL) {
		/* can't print any error messagees */
		perror("Can't open logfile");
		return -1;
	}
	logprintf(LIRC_INFO, "reopened logfile");
	if (-1 == fchmod(fileno(lf), s.st_mode)) {
		logprintf(LIRC_WARNING, "could not set file permissions");
		logperror(LIRC_WARNING, NULL);
	}
	return 0;
}


int lirc_log_setlevel(loglevel_t level)
{
	if (level >= LIRC_MIN_LOGLEVEL && level <= LIRC_MAX_LOGLEVEL){
		loglevel = level;
		return 1;
	}
	else
		return 0;
}


static loglevel_t symbol2loglevel(const char* levelstring)
{
	static const struct {const char* label; int value;} options[] = {
		{"STALK" 	, LIRC_STALK},
		{"PEEP" 	, LIRC_PEEP},
		{"TRACE" 	, LIRC_TRACE},
		{"DEBUG" 	, LIRC_DEBUG},
		{"INFO" 	, LIRC_INFO},
		{"NOTICE"	, LIRC_NOTICE},
		{"WARNING"	, LIRC_WARNING},
		{"ERROR" 	, LIRC_ERROR},
		{0,0}
	};

	char label[128];
	int i;

   	if (levelstring == NULL || ! *levelstring)
		return LIRC_BADLEVEL;
	for (i = 0; i < sizeof(label) && levelstring[i]; i += 1)
		label[i] = toupper(levelstring[i]);
	label[i] = '\0';
	i = 0;
	while (options[i].label && strcmp(options[i].label, label) != 0)
              i += 1;
	return options[i].label ? options[i].value : -1;
}


loglevel_t lirc_log_defaultlevel(void)
// Try to parse LIRC_LOGLEVEL in environment, fall back to DEFAULT_LOGLEVEL.
{
	loglevel_t try;
	const char* const level = getenv("LIRC_LOGLEVEL");

	if (level != NULL) {
		try = string2loglevel(level);
		return try == LIRC_BADLEVEL ? DEFAULT_LOGLEVEL : try;
	} else {
		return DEFAULT_LOGLEVEL;
	}
}


loglevel_t string2loglevel(const char* s)
{
	long level = LONG_MAX;

	if (s == NULL || *s == '\0')
		return LIRC_BADLEVEL;
	while (isspace(*s) && *s)
		s++;
	if (isdigit(*s)) {
		level = strtol(s, NULL, 10);
		if (level > LIRC_MAX_LOGLEVEL || level < LIRC_MIN_LOGLEVEL)
			return LIRC_BADLEVEL;
		else
			return level;
	} else {
		return symbol2loglevel(s);
	}
}


/**
 * Prints the log request to the log, if the priority fits.
 * @param prio Priority of log request
 * @param format_str Format string in the usual C sense.
 * @param ... Additional vararg parameters.
 */
void logprintf(loglevel_t prio, const char *format_str, ...)
{
	int save_errno = errno;
	va_list ap;

	if (nodaemon && prio <= loglevel) {
		fprintf(stderr, "%s: %s ", progname, prio2text(prio));
		va_start(ap, format_str);
		vfprintf(stderr, format_str, ap);
		va_end(ap);
		fputc('\n', stderr);
		fflush(stderr);
	}
	if (use_syslog) {
		va_start(ap, format_str);
		vsyslog(prio, format_str, ap);
		va_end(ap);
	} else if (lf && prio <= loglevel) {
		time_t current;
		char *currents;

		current = time(&current);
		currents = ctime(&current);

		fprintf(lf, "%15.15s %s %s: ",
			currents + 4, hostname, progname);
		fprintf(lf, "%s: ", prio2text(prio));
		va_start(ap, format_str);
		vfprintf(lf, format_str, ap);
		va_end(ap);
		fputc('\n', lf);
		fflush(lf);
	}
	errno = save_errno;
}

/**
 * Prints a description of the last error to the log.
 * @param prio Priority of log request.
 * @param s Text string, typically the name of the function invoking the log request.
 */
void logperror(loglevel_t prio, const char *s)
{
	if (use_syslog){
		if ((s) != NULL)
			syslog(prio, "%s: %m\n", (char *)s);
		else
			syslog(prio, "%m\n");
	} else {
		if (s != NULL) {
			logprintf(prio, "%s: %s", s, strerror(errno));
		} else {
			logprintf(prio, "%s", strerror(errno));
		}
	}
}

void hexdump(char *prefix, unsigned char* buf, int len)
// Dump a byte array as hex code, adding a prefix.
{
	int i;
	char str[1024];
	int pos = 0;
	if (prefix != NULL) {
		strncpy(str, prefix, sizeof(str));
		pos = strnlen(str, sizeof(str));
	}
	if (len > 0) {
		for (i = 0; i < len; i++) {
			if (pos + 3 >= sizeof(str)) {
				break;
			}

			if (!(i % 8)) {
				str[pos++] = ' ';
			}

			sprintf(str + pos, "%02x ", buf[i]);

			pos += 3;
		}
	} else {
		strncpy(str + pos, "NO DATA", sizeof(str));
	}
	LOGPRINTF(1, "%s", str);
}
