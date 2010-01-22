/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
/* $Id: server.c 263 2010-01-09 22:04:59Z river $ */

#include	<sys/socket.h>
#include	<stdio.h>
#include	<errno.h>
#include	<port.h>
#include	<netdb.h>
#include	<fcntl.h>
#include	<signal.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<unistd.h>
#include	<syslog.h>

#include	"wita.h"

static void	server_up(server_t *);
static void	server_down(server_t *, int);
static void	server_cancel_check(server_t *);
static void	server_start_read_check(server_t *);

/*
 * Find an existing server.
 */
server_t *
find_server(conf, name)
	config_t	*conf;
	char const	*name;
{
int	i;
	for (i = 0; i < conf->nservers; i++)
		if (strcmp(name, conf->servers[i]->sr_name) == 0)
			return conf->servers[i];
	return NULL;
}

/*
 * Create a new server.
 */
server_t *
new_server(conf, name)
	config_t	*conf;
	char const	*name;
{
server_t	 *sr;
server_t	**newsr;
struct addrinfo	  hints;
struct addrinfo	 *res = NULL;
int		  i;
char		  addr[16];
struct sigevent	  ev;
port_notify_t	  notf;
char		 *sport;

	assert(conf);
	assert(name);

	if ((sr = find_server(conf, name)) != NULL)
		return sr;

	if ((sr = calloc(1, sizeof(server_t))) == NULL)
		return NULL;

	ev.sigev_notify = SIGEV_PORT;
	ev.sigev_signo = 0;
	ev.sigev_value.sival_ptr = &notf;
	notf.portnfy_port = port;
	notf.portnfy_user = sr;

	if (timer_create(CLOCK_REALTIME, &ev, &sr->sr_timer) == -1) {
		syslog(LOG_ERR, "cannot create timer: %m");
		free(sr);
		return NULL;
	}

	if ((sr->sr_name = strdup(name)) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		free_server(sr);
		return NULL;
	}

	if ((sport = strchr(sr->sr_name, ':')) != NULL) {
		*sport++ = 0;
		sr->sr_port = sport;
	} else
		sr->sr_port = "3306";

	(void) memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if ((i = getaddrinfo(sr->sr_name, sr->sr_port, &hints, &res)) != 0) {
		syslog(LOG_ERR, "cannot resolve %s: %s", name, gai_strerror(i));
		free_server(sr);
		errno = EINVAL;
		return NULL;
	}

	if ((i = getnameinfo(res->ai_addr, res->ai_addrlen, addr, sizeof addr,
					NULL, 0, NI_NUMERICHOST)) != 0) {
		syslog(LOG_ERR, "cannot translate %s to IP: %s", name, gai_strerror(i));
		freeaddrinfo(res);
		free_server(sr);
		errno = EINVAL;
		return NULL;
	}

	/*
	 * Store the address in the server so we can connect to it later.
	 */
	assert(res->ai_addrlen == sizeof (sr->sr_sockaddr));
	(void) memcpy(&sr->sr_sockaddr, res->ai_addr, sizeof (sr->sr_sockaddr));
	freeaddrinfo(res);

	if ((sr->sr_address = strdup(addr)) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		free_server(sr);
		return NULL;
	}

	sr->sr_online = 0;

	if ((newsr = realloc(conf->servers, sizeof(server_t *) * (conf->nservers + 1))) == NULL) {
		free_server(sr);
		return NULL;
	}

	conf->servers = newsr;
	conf->servers[conf->nservers] = sr;
	conf->nservers++;

	return sr;
}

/*
 * Initiate a connect() to the given server, and start the timer
 * for connect timeout.
 */
void
server_start_connect_check(server)
	server_t *server;
{
int			 fl;
struct itimerspec	 ts;

	assert(server);
	assert(server->sr_state == SR_IDLE);

	if ((server->sr_socket = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_start_connect_check: "
				"socket() failed: %m",
				server->sr_name, server->sr_address,
				server->sr_port);
		server_cancel_check(server);
		return;
	}

	if ((fl = fcntl(server->sr_socket, F_GETFL, 0)) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_start_connect_check: "
				"fcntl(F_GETFL) failed: %m",
				server->sr_name, server->sr_address,
				server->sr_port);
		server_cancel_check(server);
		return;
	}

	if ((fl = fcntl(server->sr_socket, F_SETFL, fl | O_NONBLOCK)) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_start_connect_check: "
				"fcntl(F_SETFL) failed: %m",
				server->sr_name, server->sr_address,
				server->sr_port);
		server_cancel_check(server);
		return;
	}

	if (connect(server->sr_socket, &server->sr_sockaddr, sizeof (server->sr_sockaddr)) == 0) {
		server_start_read_check(server);
		return;
	}

	switch (errno) {
	case EINPROGRESS:
		server->sr_state = SR_CONNECT;

		/*
		 * Set the timer for 5 seconds.
		 */
		(void) memset(&ts, 0, sizeof ts);
		ts.it_value.tv_sec = 5;

		if (timer_settime(server->sr_timer, 0, &ts, NULL) == -1) {
			syslog(LOG_ERR, "%s[%s]:%s: server_start_connect_check: "
					"cannot set connect timer: timer_settime: %m",
					server->sr_name, server->sr_address,
					server->sr_port);
			server_cancel_check(server);
			return;
		}

		/*
		 * And associate the fd so we know when it connected.
		 */
		if (port_associate(port, PORT_SOURCE_FD, server->sr_socket, POLLOUT, server) == -1) {
			syslog(LOG_ERR, "%s[%s]:%s: server_start_connect_check: "
					"cannot associate fd: port_associate: %m",
					server->sr_name, server->sr_address,
					server->sr_port);
			server_cancel_check(server);
		}

		return;

	default:
		server_down(server, errno);
		return;
	}
}

void
server_schedule_check(server)
	server_t *server;
{
struct itimerspec	ts;

	/*
	 * Set the timer for 5 seconds.
	 */
	(void) memset(&ts, 0, sizeof ts);
	ts.it_value.tv_sec = 5;

	if (timer_settime(server->sr_timer, 0, &ts, NULL) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_schedule_check: "
				"cannot set timer for next check: timer_settime: %m",
				server->sr_name, server->sr_address,
				server->sr_port);
		syslog(LOG_ERR, "fatal state inconsistency (lost server), exiting");
		exit(1);
	}
}

void
server_up(sr)
	server_t	*sr;
{
struct itimerspec	ts;
	
	if (!sr->sr_online) {
		syslog(LOG_NOTICE, "%s[%s]:%s: state now UP",
				sr->sr_name,
				sr->sr_address,
				sr->sr_port);
		sr->sr_online = 1;
	}

	(void) close(sr->sr_socket);
	sr->sr_state = SR_IDLE;

	/* Check again in 5 seconds. */
	(void) memset(&ts, 0, sizeof ts);
	ts.it_value.tv_sec = 5;
	if (timer_settime(sr->sr_timer, 0, &ts, NULL) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_up: "
				"cannot set timer for next check: timer_settime: %m",
				sr->sr_name, sr->sr_address,
				sr->sr_port);
		syslog(LOG_ERR, "fatal state inconsistency (lost server), exiting");
		exit(1);
	}
}

void
server_down(sr, error)
	server_t	*sr;
{
struct itimerspec	ts;

	if (sr->sr_online) {
		syslog(LOG_WARNING, "%s[%s]:%s: state now DOWN: %s",
				sr->sr_name,
				sr->sr_address,
				sr->sr_port,
				strerror(error));
		sr->sr_online = 0;
	}

	(void) close(sr->sr_socket);
	sr->sr_state = SR_IDLE;

	/* Check again in 5 seconds. */
	(void) memset(&ts, 0, sizeof ts);
	ts.it_value.tv_sec = 5;
	if (timer_settime(sr->sr_timer, 0, &ts, NULL) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_down: "
				"cannot set timer for next check: timer_settime: %m",
				sr->sr_name, sr->sr_address,
				sr->sr_port);
		syslog(LOG_ERR, "fatal state inconsistency (lost server), exiting");
		exit(1);
	}
}

void
server_cancel_check(sr)
	server_t	*sr;
{
struct itimerspec	ts;
	if (sr->sr_socket != -1)
		(void) close(sr->sr_socket);
	sr->sr_state = SR_IDLE;

	/* Check again in 5 seconds. */
	(void) memset(&ts, 0, sizeof ts);
	ts.it_value.tv_sec = 5;
	if (timer_settime(sr->sr_timer, 0, &ts, NULL) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_cancel_check: "
				"cannot set timer for next check: timer_settime: %m",
				sr->sr_name, sr->sr_address,
				sr->sr_port);
		syslog(LOG_ERR, "fatal state inconsistency (lost server), exiting");
		exit(1);
	}
}

void
server_start_read_check(sr)
	server_t *sr;
{
struct itimerspec	ts;

	/*
	 * Connect succeeded, now try reading some data.
	 */
	switch (read(sr->sr_socket, &sr->sr_rdbuf, 1)) {
	case 0:	/* EOF */
		server_down(sr, ENODATA);
		return;

	case -1: /* Error */
		if (errno != EAGAIN) {
			server_down(sr, errno);
			return;
		}

		sr->sr_state = SR_READ;

		/*
		 * Set the timer for 5 seconds.
		 */
		(void) memset(&ts, 0, sizeof ts);
		ts.it_value.tv_sec = 5;

		if (timer_settime(sr->sr_timer, 0, &ts, NULL) == -1) {
			syslog(LOG_ERR, "%s[%s]:%s: server_starte_read_check: "
					"cannot set timer for read timeout: timer_settime: %m",
					sr->sr_name, sr->sr_address, sr->sr_port);
			server_cancel_check(sr);
			return;
		}

		/*
		 * And associate the fd so we know when the read returned.
		 */
		if (port_associate(port, PORT_SOURCE_FD, sr->sr_socket, POLLIN, sr) == -1) {
			syslog(LOG_ERR, "%s[%s]:%s: server_start_read_check: "
					"cannot associate fd with port: port_associate: %m",
					sr->sr_name, sr->sr_address, sr->sr_port);
			server_cancel_check(sr);
			return;
		}
		return;

	default: /* Success */
		server_up(sr);
		return;
	}
}

/*
 * Handle a timer event on this server.
 */
void
server_handle_timer(sr)
	server_t	*sr;
{
	assert(sr);

	switch (sr->sr_state) {
		/*
		 * If the server is idle, it's time to start another
		 * check.
		 */
	case SR_IDLE:
		server_start_connect_check(sr);
		break;

		/*
		 * If the server is currently connecting or reading,
		 * the operation timed out.
		 */
	case SR_CONNECT:
	case SR_READ:
		server_down(sr, ETIMEDOUT);
		break;
	}
}

/*
 * Handle an fd event for a server.
 */
void
server_handle_fd(sr)
	server_t	*sr;
{
int		 error;
size_t		 errlen = sizeof error;

	assert(sr);
	assert(sr->sr_state == SR_CONNECT ||
		sr->sr_state == SR_READ);

	if (getsockopt(sr->sr_socket, SOL_SOCKET, SO_ERROR, &error, &errlen) == -1) {
		syslog(LOG_ERR, "%s[%s]:%s: server_handle_fd: cannot retrieve socket error: %m",
				sr->sr_name, sr->sr_address, sr->sr_port);
		server_cancel_check(sr);
		return;
	}

	if (error != 0) {
		server_down(sr, error);
		return;
	}

	/*
	 * No error.  If we're in SR_CONNECT, start a read check.
	 * If we're in SR_READ, the server must be up.
	 */
	if (sr->sr_state == SR_CONNECT) 
		server_start_read_check(sr);
	else
		server_up(sr);
}

void
free_server(sr)
	server_t	*sr;
{
	free(sr->sr_name);
	free(sr->sr_address);
	(void) timer_delete(sr->sr_timer);
	free(sr);
}
