/* Copyright (c) 2009 River Tarnell <river@loreley.flyingparchment.org.uk>. */
/*
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely. This software is provided 'as-is', without any express or implied
 * warranty.
 */
  
/* $Id: config.c 261 2010-01-09 21:31:31Z river $ */

#include	<stdio.h>
#include	<assert.h>
#include	<errno.h>
#include	<string.h>
#include	<stdlib.h>
#include	<syslog.h>

#include	"wita.h"

config_t	*curconf;

static void	free_configuration(config_t *);

int
load_configuration(char const *file)
{
FILE		*f;
char		 line[1024];
config_t	*newconf;

	assert(file);

	if ((f = fopen(file, "r")) == NULL) {
		syslog(LOG_ERR, "cannot open configuration file %s: %m", file);
		return -1;
	}

	if ((newconf = calloc(1, sizeof (config_t))) == NULL) {
		(void) fclose(f);
		syslog(LOG_ERR, "out of memory (trying to continue anyway)");
		return -1;
	}

	while (fgets(line, sizeof line, f) != NULL) {
	char	*grname;
	char	*sname;
	int	 backup;
	group_t	*group;

		if (line[strlen(line) - 1] != '\n') {
			syslog(LOG_ERR, "unterminated newline in configuration");
			(void) fclose(f);
			free_configuration(newconf);
			return -1;
		}

		line[strlen(line) - 1] = 0;

		if (*line == 0 || *line == '#')
			continue;

		if ((grname = strtok(line, " \t")) == NULL)
			continue;

		if ((group = new_group(newconf, grname)) == NULL) {
			syslog(LOG_ERR, "cannot allocate group: %m");
			(void) fclose(f);
			free_configuration(newconf);
			return -1;
		}

		while ((sname = strtok(NULL, " \t")) != NULL) {
		server_t	*sr;

			if (*sname == '!') {
				sname++;
				backup = 1;
			} else
				backup = 0;

			if ((sr = new_server(newconf, sname)) == NULL) {
				syslog(LOG_ERR, "cannot allocate server: %m");
				(void) fclose(f);
				free_configuration(newconf);
				return -1;
			}

			if (add_server_to_group(group, sr, backup) == -1) {
				syslog(LOG_ERR, "cannot add server to group: %m");
				(void) fclose(f);
				free_configuration(newconf);
				return -1;
			}
		}
	}

	(void) fclose(f);

	free_configuration(curconf);
	curconf = newconf;
	return 0;
}

void
free_configuration(conf)
	config_t	*conf;
{
int	i;

	if (conf == NULL)
		return;

	for (i = 0; i < conf->ngroups; ++i)
		free_group(conf->groups[i]);
	free(conf->groups);

	for (i = 0; i < conf->nservers; ++i)
		free_server(conf->servers[i]);
	free(conf->servers);

	free(conf);
}
