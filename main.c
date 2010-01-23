/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
/*
 * wita: PowerDNS backend to return a list of A records based on which
 * servers are online.
 */

/*
 * Wita configures each server into 'groups'.  Each group can contain
 * one or more servers, and each server can be in one of more groups.
 * Each group corresponds to one hostname in DNS.  For example, if you
 * have two groups, 'sql-s1-fast' and 'sql-s1', and you configure
 * PowerDNS to send requests to wita for wita.example.com, wita will serve
 * sql-s1-fast.wita.example.com and sql-s1.wita.example.com.
 *
 * Wita doesn't actually care what the FQDN of the query is; it will strip
 * off all except the first element of the FQDN and use that as the group name.
 *
 * Wita uses a simple configuration format that looks like this:
 *
 *     <group> <server> [<server> ...]
 *
 * for example:
 *
 *     sql-s1-fast thyme !rosemary
 *     sql-s1 rosemary !thyme
 *
 * Placing an '!' in front of the server name indicates that this server
 * should be returned only if all other servers in the group are down.
 * You can include a port name for a server:
 *
 *     sql-s1-fast thyme:3307 !rosemary
 *
 * But you should not configure the same server more than once with different
 * ports (it will not work properly).
 *
 * Server names will be resolved to IPs at startup time and cached.
 *
 * For each configured server, wita connects to it every 5 seconds.
 * If the connection succeeds, it then waits 5 seconds for the server
 * to write some data.  If either of these time out, the server is
 * considered to be down.  Otherwise, it's up.  When PowerDNS requests
 * RRs for a particular group, wita returns the addresses of the servers
 * in that group that are up.
 */

#include	<sys/socket.h>

#include	<netdb.h>
#include	<stdlib.h>
#include	<assert.h>
#include	<string.h>
#include	<stdio.h>
#include	<errno.h>
#include	<signal.h>
#include	<time.h>
#include	<port.h>
#include	<fcntl.h>
#include	<unistd.h>
#include	<syslog.h>

#include	"wita.h"

char const	*cfg = "/etc/opt/ts/wita.cfg";
int		 port;	/* Solaris event port */

/*
 * Handle async signal delivery and send the signal as
 * an event to our event port in main().
 */
void
sighandle(int sig)
{
	if (port_send(port, sig, NULL) == -1) {
		syslog(LOG_ERR, "sighandle: cannot send message: port_send: %m");
		exit(1);
	}
}

int
main(argc, argv)
	int 	  argc;
	char	**argv;
{
int		 i, fl;
port_event_t	 ev;
server_t	*sr;
int		 c;

	openlog("wita", LOG_PID, LOG_DAEMON);

	while ((c = getopt(argc, argv, "c:")) != -1) {
		switch(c) {
		case 'c':
			cfg = optarg;
			break;

		default:
			syslog(LOG_ERR, "usage: wita [-c cfg]");
			(void) fprintf(stderr, "usage: wita [-c cfg]\n");
			return 1;
		}
	}

	if ((port = port_create()) == -1) {
		syslog(LOG_ERR, "cannot create event port: %m");
		return 1;
	}

	(void) signal(SIGINT, sighandle);
	(void) signal(SIGHUP, sighandle);
	(void) signal(SIGTERM, sighandle);

	if (load_configuration(cfg) == -1) {
		syslog(LOG_ERR, "cannot load configuration");
		return 1;
	}

	/*
	 * Start the initial check for each server.
	 */
	for (i = 0; i < curconf->nservers; i++)
		server_start_connect_check(curconf->servers[i]);

	/*
	 * Register for events from PowerDNS on stdin.
	 */
	if ((fl = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1) {
		syslog(LOG_ERR, "fcntl(0, F_GETFL): %m");
		return 1;
	}

	if (fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK) == -1) {
		syslog(LOG_ERR, "fcntl(0, F_SETFL): %m");
		return 1;
	}

	if (port_associate(port, PORT_SOURCE_FD, STDIN_FILENO, POLLIN, NULL) == -1) {
		syslog(LOG_ERR, "port_associate(0, POLLIN): %m");
		return 1;
	}

	/*
	 * Handle any pending events on stdin.
	 */
	handle_pdns();

	/*
	 * Main event loop.
	 */
	while ((i = port_get(port, &ev, NULL)) != -1 || errno == EINTR) {
		if (i == -1 && errno == EINTR)
			continue;

		switch (ev.portev_source) {

		/*
		 * Timer event: this can either mean our connect() or read()
		 * timed out, or the server is due for another check.  We
		 * decide which based on server's current state.
		 */
		case PORT_SOURCE_TIMER:
			sr = (server_t *) ev.portev_user;
			assert(sr);
			server_handle_timer(sr);
			break;

		/*
		 * FD event: this can come from a connect() or read() to a
		 * server either succeeding or returning an error.  If it's on
		 * fd 0, it's a question from PowerDNS.
		 */
		case PORT_SOURCE_FD:
			if (ev.portev_object == 0) {
				handle_pdns();
				break;
			}

			sr = (server_t *) ev.portev_user;
			assert(sr);
			server_handle_fd(sr);
			break;


		/*
		 * PORT_SOURCE_USER is a signal delivery from sighandle().
		 */
		case PORT_SOURCE_USER:
			(void) signal(ev.portev_events, sighandle);

			switch (ev.portev_events) {
			case SIGHUP:
				syslog(LOG_INFO, "SIGHUP received, reloading configuration");
				if (load_configuration(cfg) == -1)
					syslog(LOG_ERR, "cannot reload configuration");

				/*
				 * Restart check timers for all servers with the new configuration.
				 */
				for (i = 0; i < curconf->nservers; i++)
					server_start_connect_check(curconf->servers[i]);
				break;

			case SIGINT:
			case SIGTERM:
				syslog(LOG_INFO, "exit requested by signal");
				return 0;
			}
			break;

		default:
			abort();
		}
	}

	syslog(LOG_ERR, "port_get: %m\n");
	return 0;
}
