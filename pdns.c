/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
/*
 * Handle incoming data from PowerDNS.
 */

#include	<sys/socket.h>

#include	<string.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<unistd.h>
#include	<errno.h>
#include	<port.h>
#include	<syslog.h>

#include	"wita.h"

static void decode_pdns(void);

static char pdnsbuf[1024];	/* Incoming data from PowerDNS */
static int pdnsnb;		/* Amount of data read so far */

static enum {
	PD_HELO,
	PD_RUN
} pdns_state = PD_HELO;

static void
cmd_helo()
{
char *version;
	if ((version = strtok(NULL, "\t")) == NULL) {
		(void) printf("FAIL\tMissing argument to HELO\n");
		(void) fflush(stdout);
		return;
	}

	if (strcmp(version, "1") != 0) {
		(void) printf("FAIL\tUnrecognised protocol version\n");
		(void) fflush(stdout);
		return;
	}

	(void) printf("OK\twita ready\n");
	(void) fflush(stdout);
	pdns_state = PD_RUN;
}

static void
cmd_axfr()
{
	(void) printf("FAIL\tAXFR not supported\n");
	(void) fflush(stdout);
}

static void
cmd_q()
{
char	*qname, *qclass, *qtype, *id, *ip;
char	*grnam;
char	*p;
group_t	*group;
int	 i, printed = 0;

	if (	(qname = strtok(NULL, "\t")) == NULL ||
		(qclass = strtok(NULL, "\t")) == NULL ||
		(qtype = strtok(NULL, "\t")) == NULL ||
		(id = strtok(NULL, "\t")) == NULL ||
		(ip = strtok(NULL, "\t")) == NULL) {
		
		(void) printf("FAIL\tNot enough arguments to query\n");
		(void) fflush(stdout);
		return;
	}

	if (strcmp(qclass, "IN") != 0) {
		(void) printf("FAIL\tOnly IN class is supported\n");
		(void) fflush(stdout);
		return;
	}

	if (strcmp(qtype, "A") != 0 && strcmp(qtype, "ANY") != 0) {
		(void) printf("END\n");
		(void) fflush(stdout);
		return;
	}
	
	/*
	 * Strip the fqdn from the name and use it as the group
	 * name.
	 */
	if ((grnam = strdup(qname)) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		(void) write(STDOUT_FILENO, "END\n", 4);
		return;
	}

	if ((p = strchr(grnam, '.')) != NULL)
		*p = 0;

	if ((group = find_group(curconf, grnam)) == NULL) {
		syslog(LOG_INFO, "request for group %s, which does not exist",
				grnam);
		(void) printf("END\n");
		(void) fflush(stdout);
		return;
	}

	/*
	 * Print the address of each server in the group that's
	 * up.  We use a small TTL (10 seconds) because server
	 * status can change quickly.
	 */
	for (i = 0; i < group->gr_nservers; i++) {
		if (group->gr_servers[i]->sg_backup)
			continue;
		if (!group->gr_servers[i]->sg_server->sr_online)
			continue;
		(void) printf("DATA\t%s\tIN\tA\t10\t-1\t%s\n",
			qname, group->gr_servers[i]->sg_server->sr_address);
		printed++;
	}

	/*
	 * If we haven't printed anything, all primary servers
	 * in the group are down.  Print the backup servers
	 * instead.
	 */
	if (!printed) {
		for (i = 0; i < group->gr_nservers; i++) {
			if (!group->gr_servers[i]->sg_backup)
				continue;
			if (!group->gr_servers[i]->sg_server->sr_online)
				continue;
			(void) printf("DATA\t%s\tIN\tA\t10\t-1\t%s\n",
				qname, group->gr_servers[i]->sg_server->sr_address);
		}
	}

	(void) printf("END\n");
	(void) fflush(stdout);
}

static void
decode_pdns()
{
char	line[1024];
char	*p;
char	*cmd;

	for (;;) {
		/* Have we got a line yet? */
		if ((p = memchr(pdnsbuf, '\n', pdnsnb)) == NULL) {
			if (pdnsnb == sizeof pdnsbuf) {
				/*
				 * More than 1024 bytes without a \n... must be some kind of bug.
				 */
				syslog(LOG_ERR, "read too much data from PowerDNS");
				exit(1);
			}

			return;
		}

		(void) memcpy(line, pdnsbuf, p - pdnsbuf);
		line[p - pdnsbuf] = 0;
		(void) memmove(pdnsbuf, p + 1, pdnsnb - (p - pdnsbuf));
		pdnsnb -= (p - pdnsbuf) + 1;

		cmd = strtok(line, "\t");
		
		switch (pdns_state) {
		case PD_HELO:
			if (strcmp(cmd, "HELO") == 0)
				cmd_helo();
			else {
				(void) printf("FAIL\tUnknown command (or invalid for this state)\n");
				(void) fflush(stdout);
			}
			break;

		case PD_RUN:
			if (strcmp(cmd, "AXFR") == 0)
				cmd_axfr();
			else if (strcmp(cmd, "Q") == 0) 
				cmd_q();
			else {
				(void) printf("FAIL\tUnknown command (or invalid for this state)\n");
				(void) fflush(stdout);
			}
			break;

		default:
			abort();
		}
	}
}

void
handle_pdns()
{
	for (;;) {
	int	nb = (sizeof pdnsbuf - pdnsnb);
	int	i;
		if (nb == 0) {	/* No space in buffer */
			/*
			 * More than 1024 bytes without a \n... must be some kind of bug.
			 */
			syslog(LOG_ERR, "read too much data from PowerDNS");
			exit(1);
		}

		switch (i = read(STDIN_FILENO, pdnsbuf + pdnsnb, nb)) {
		case -1:
			if (errno == EAGAIN) {
				if (port_associate(port, PORT_SOURCE_FD, STDIN_FILENO, POLLIN, NULL) == -1) {
					syslog(LOG_ERR, "handle_pdns: cannot associate port: "
							"port_associate(STDIN_FILE, POLLIN): %m");
					exit(1);
				}
				return;
			}

			syslog(LOG_ERR, "handle_pdns: read from PowerDNS failed: %m");
			exit(1);
		
		case 0:	/* EOF */
			exit(0);

		default:
			pdnsnb += i;
			decode_pdns();
			break;
		}
	}
}

