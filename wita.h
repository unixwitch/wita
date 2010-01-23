/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
#ifndef	WITA_H
#define	WITA_H

#include	<sys/socket.h>
#include	<time.h>

#define WITA_VERSION "1.0"

/*
 * A single server.
 */
typedef enum {
	SR_IDLE,	/* Server is not being checked */
	SR_CONNECT,	/* connect() in progress */
	SR_READ		/* read() in progress */
} server_state_t;

typedef struct server {
	char		*sr_name;	/* Name as specified by the user */
	char		*sr_address;	/* IP address in dotted quad notation */
	char const	*sr_port;	/* Port to test connection to */
	int		 sr_online;	/* If the server was working at last check */
	timer_t		 sr_timer;	/* Timer for this server */
	server_state_t	 sr_state;	/* Server state */
	int		 sr_socket;	/* Connection socket */
	struct sockaddr	 sr_sockaddr;	/* Address for connect() */
	char		 sr_rdbuf;	/* One-byte buffer for read check */
} server_t;

/*
 * An entry for a server in a group.
 */
typedef struct server_group {
	server_t	*sg_server;
	int		 sg_backup;	/* Is this a backup server */
} server_group_t;

/*
 * A group of servers.
 */
typedef struct group {
	char	 	 *gr_name;	/* Group name in config file */
	int		  gr_nservers;	/* How many servers in the group */
	server_group_t	**gr_servers;	/* The servers in this group */
} group_t;

/*
 * Global configuration.
 */

typedef struct {
	int	  	  nservers;
	server_t	**servers;

	int		  ngroups;
	group_t		**groups;
} config_t;

server_t	*new_server(config_t *, char const *name);
server_t	*find_server(config_t *, char const *name);
void		 server_start_connect_check(server_t *);
void		 server_handle_fd(server_t *);
void		 server_handle_timer(server_t *);
void		 free_server(server_t *);

group_t		*new_group(config_t *, char const *name);
group_t		*find_group(config_t *, char const *name);
int		 add_server_to_group(group_t *group, server_t *server, int backup);
void		 free_group(group_t *group);

config_t *curconf;

int load_configuration(char const *file);

extern int	  port;

/*
 * PowerDNS interface.
 */
void	handle_pdns();

#endif	/* !WITA_H */
