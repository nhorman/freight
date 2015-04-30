/*********************************************************
 * *Copyright (C) 2015 Neil Horman
 * *This program is free software; you can redistribute it and\or modify
 * *it under the terms of the GNU General Public License as published 
 * *by the Free Software Foundation; either version 2 of the License,
 * *or  any later version.
 * *
 * *This program is distributed in the hope that it will be useful,
 * *but WITHOUT ANY WARRANTY; without even the implied warranty of
 * *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * *GNU General Public License for more details.
 * *
 * *File: mode.c
 * *
 * *Author:Neil Horman
 * *
 * *Date: April 21, 2015
 * *
 * *Description: 
 * *********************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libconfig.h>
#include <mode.h>
#include <freight-log.h>
#include <freight-common.h>


static char** execarray = NULL;


struct container_options {
	char *user;
};

static int parse_container_options(char *config_path,
			           struct container_options *copts)
{
	config_t config;
	config_setting_t *tmp, *tmp2;
	int rc = -EINVAL;

	memset(copts, 0, sizeof(struct container_options));

	config_init(&config);

	if(config_read_file(&config, config_path) == CONFIG_FALSE) {
		LOG(ERROR, "Cannot parse container config %s: %s\n",
		    config_path, strerror(errno));
		goto out;
	}

	tmp = config_lookup(&config, "container_opts");

	if (!tmp)
		goto out;

	rc = 0;
	tmp2 = config_setting_get_member(tmp, "user");
	if (!tmp2)
		goto out;

	copts->user = strdup(config_setting_get_string(tmp2));

out:
	config_destroy(&config);
	return rc;
}

static int build_dir(const char *base, const char *path)
{
	char *tmp = alloca(strlen(base) + strlen(path) + 4);
	strcpy(tmp, base);
	strcat(tmp, "/");
	if (path)
		strcat(tmp, path);
	return mkdir(tmp, 0700);
}

static char *build_path(const char *base, const char *path)
{
	char *out;
	size_t tsz;

	tsz = strlen(base) + strlen(path) + 2;

	out = calloc(1, tsz);
	if (out) {
		out = strcpy(out, base);
		out = strcat(out, path);
	}
	return out;
}

void clean_container_root(const char *croot)
{
	recursive_dir_cleanup(croot);
	return;
}

void list_containers(char *scope, struct agent_config *acfg)
{
	char yumcmd[1024];

	sprintf(yumcmd, "yum --installroot=%s list %s",
		acfg->node.container_root,
		!strcmp(scope, "local") ? "installed" : "all");

	run_command(yumcmd, 1); 

	return;
}

int install_container(const char *rpm, struct agent_config *acfg)
{
	struct stat buf;
	int rc = -ENOENT;
	char yumcmd[1024];

	if (stat(acfg->node.container_root, &buf) == -ENOENT) {
		LOG(ERROR, "Container root isn't initalized\n");
		goto out;
	}

	sprintf(yumcmd, "yum --installroot=%s -y --nogpgcheck install %s",
		acfg->node.container_root, rpm);

	rc = run_command(yumcmd, acfg->cmdline.verbose);

out:
	return rc;
}

int uninstall_container(const char *rpm, struct agent_config *acfg)
{
	char yumcmd[1024];
	struct stat buf;
	int rc = -ENOENT;

	sprintf(yumcmd, "%s/containers/%s", acfg->node.container_root, rpm);

	if (stat(yumcmd, &buf) == -ENOENT)
		goto out;

	sprintf(yumcmd, "yum --installroot=%s -y erase %s",
		acfg->node.container_root, rpm);
	rc = run_command(yumcmd, acfg->cmdline.verbose);

out:
	return rc;
}

int init_container_root(const struct db_api *api,
			const struct agent_config *acfg)
{
	char *dirs[]= {
		"",
		"containers",
		"var",
		"var/lib",
		"var/lib/rpm",
		"var/lib/yum",
		"var/cache",
		"var/cache/yum",
		"etc",
		"etc/yum.repos.d",
		NULL,
	};
	const char *croot = acfg->node.container_root;
	char *tmp;
	int i, rc;
	FILE *fptr;
	struct yum_cfg_list *yum_cfg;

	/*
 	 * Sanity check the container root, it can't be the 
 	 * system root
 	 */
	rc = -EINVAL;
	if (!strcmp(croot, "/")) {
		LOG(ERROR, "container root cannot be system root!\n");
		goto out_cleanup;
	}
		
	/*
 	 * Start by emptying the container root
 	 */
	LOG(INFO, "Cleaning container root\n");
	clean_container_root(croot);

	LOG(INFO, "Building freight-agent environment\n");
	for (i=0; dirs[i] != NULL; i++) {

		rc = build_dir(croot, dirs[i]);
		if (rc) {
			LOG(ERROR, "Could not create %s: %s\n",
				dirs[i], strerror(rc));
			goto out_cleanup;
		}
	}

	/*
 	 * Create the yum.conf file
 	 * It should be able to rely on defaults as the environment is setup for
 	 * it
 	 */
	tmp = build_path(croot, "/etc/yum.conf");
	fptr = fopen(tmp, "w");
	if (!fptr) {
		rc = errno;
		LOG(ERROR, "Unable to write /etc/yum.conf: %s\n",
			strerror(errno));
		goto out_cleanup;
	}
	free(tmp);

	fprintf(fptr, "[main]\n");
	fprintf(fptr, "cachedir=/var/cache/yum/$basearch/$releasever\n");
	fprintf(fptr, "logfile=/var/log/yum.log\n");
	fprintf(fptr, "gpgcheck=0\n"); /* ONLY FOR NOW! */
	fclose(fptr);

	/*
 	 * Now we need to check the database for our repository configuration
 	 */
	yum_cfg = db_get_yum_cfg(api, acfg);
	if (!yum_cfg)
		LOG(WARNING, "No yum config in database, we won't be able "
			     "to fetch containers!\n");
	else {
		/* Iterate over the yum entries here and build repo files */
	}
out:
	return rc;
out_cleanup:
	clean_container_root(croot);
	goto out;
}


static void daemonize(const struct agent_config *acfg)
{
	int i, fd;

	/* Become our own process group */
	setsid();

	/* pick a root dir until systemd fixes it up */
	chdir("/tmp");

	/*
 	 * Close all file descriptors
 	 */
	for ( i=getdtablesize(); i>=0; --i)   
		close(i);

	fd = open("/dev/null", O_RDWR, 0);
	if (fd != -1) {
		dup2 (fd, STDIN_FILENO);  
		dup2 (fd, STDOUT_FILENO);  
		dup2 (fd, STDERR_FILENO);  
    
		if (fd > 2)  
			close (fd);  
	}
}

int exec_container(const char *rpm, const char *name,
                   const struct agent_config *acfg)
{
	pid_t pid;
	int eoc;
	struct container_options copts;
	char config_path[1024];

	sprintf(config_path, "%s/containers/%s/container_config",
		acfg->node.container_root, rpm);

	/*
 	 * Lets parse the container configuration
 	 */
	if (parse_container_options(config_path, &copts))
		return -ENOENT;

	/*
 	 * Now we need to do here is fork
 	 */
	pid = fork();

	/*
 	 * Pid error
 	 */
	if (pid < 0)
		return errno;	

	/*
 	 * Parent should return immediately
 	 */
	if (pid > 0)
		return 0;

	/*
 	 * child from here out
 	 * we should daemonize
 	 * NOTE: AFTER DAEMONIZING, LOG() doesn't work, 
 	 * we will need to use machinectl to check on status
 	 */
	daemonize(acfg);

	/*
 	 * Now lets start building our execv line
 	 */
	eoc = 6; /*systemd-nspawn -D <dir> -b -M <name> */

	if (copts.user)
		eoc += 2; /* -u <user> */

	eoc++; /* NULL teriminator */

	/*
 	 * Allocate the argv array
 	 */
	execarray = malloc(sizeof(const char *) * eoc);
	if (!execarray)
		exit(1);

	sprintf(config_path, "%s/containers/%s/containerfs",
		acfg->node.container_root, rpm);
	eoc = 0;
	execarray[eoc++] = "systemd-nspawn"; /* argv[0] */
	execarray[eoc++] = "-D"; /* -D */
	execarray[eoc++] = config_path; /* <dir> */
	execarray[eoc++] = "-M"; /*-M*/
	execarray[eoc++] = name;
	execarray[eoc++] = "-b"; /* -b */
	if (copts.user) {
		execarray[eoc++] = "-u"; /* -u */
		execarray[eoc++] = copts.user; /* <user> */
	}
	execarray[eoc++] = NULL;

	exit(execvp("systemd-nspawn", execarray));
}


/*
 * This is our mode entry function, we setup freight-agent to act as a container
 * node here and listen for db events from this point
 */
int enter_mode_loop(struct db_api *api, struct agent_config *config)
{
	int rc = -EINVAL;
	struct stat buf;
	
	/*
 	 * Start by setting up a clean container root
 	 * If its not already there
 	 */
	if (stat(config->node.container_root, &buf) != 0) {
		rc = errno; 
		if (rc != ENOENT) {
			LOG(ERROR, "Container root isn't available: %s\n",
				strerror(rc));
			LOG(ERROR, "Run freight-agent -m clean\n");
			goto out;
		}
	}

	if (rc == ENOENT) {
		LOG(INFO, "Creating a container root dir\n");
		rc = init_container_root(api, config);
		if (rc) {
			LOG(ERROR, "container root could not be initalized\n");
			goto out;
		}
	}
	rc = 0;
out:
	return rc;
}

