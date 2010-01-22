/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
/* $Id: group.c 261 2010-01-09 21:31:31Z river $ */

#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<syslog.h>

#include	"wita.h"

/*
 * Create a new group with no servers in it.
 */
group_t *
new_group(conf, name)
	config_t	*conf;
	char const	*name;
{
group_t	 *r;
group_t	**newgrs = conf->groups;

	assert(conf);
	assert(name);

	if ((r = calloc(1, sizeof(group_t))) == NULL)
		return NULL;

	if ((r->gr_name = strdup(name)) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		free_group(r);
		return NULL;
	}

	if ((newgrs = realloc(newgrs, sizeof(group_t *) * (conf->ngroups + 1))) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		free_group(r);
		return NULL;
	}

	conf->groups = newgrs;
	conf->groups[conf->ngroups] = r;
	conf->ngroups++;

	return r;
}

/*
 * Find an existing group.
 */
group_t *
find_group(conf, name)
	config_t	*conf;
	char const	*name;
{
int	i;

	assert(conf);
	assert(name);

	for (i = 0; i < conf->ngroups; i++)
		if (strcmp(name, conf->groups[i]->gr_name) == 0)
			return conf->groups[i];
	return NULL;
}

/*
 * Add a server to an existing group.
 */
int
add_server_to_group(group, server, backup)
	group_t		*group;
	server_t	*server;
	int		 backup;
{
server_group_t	**news = group->gr_servers;
server_group_t	 *sg;
	
	assert(group);
	assert(server);
	assert(backup == 0 || backup == 1);

	if ((sg = calloc(1, sizeof(server_group_t))) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway");
		return -1;
	}

	if ((news = realloc(news, sizeof(server_t *) * (group->gr_nservers + 1))) == NULL) {
		syslog(LOG_ERR, "out of memory (trying to continue anyway");
		free(sg);
		return -1;
	}

	sg->sg_server = server;
	sg->sg_backup = backup;
	group->gr_servers = news;
	group->gr_servers[group->gr_nservers] = sg;
	group->gr_nservers++;

	return 0;
}

void
free_group(gr)
	group_t	*gr;
{
int	i;
	free(gr->gr_name);
	
	for (i = 0; i < gr->gr_nservers; ++i) {
		free_server(gr->gr_servers[i]->sg_server);
		free(gr->gr_servers[i]);
	}
	free(gr->gr_servers);
	free(gr);
}
