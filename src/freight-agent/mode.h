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
 * *File: mode.h
 * *
 * *Author:Neil Horman
 * *
 * *Date: April 16, 2015
 * *
 * *Description: 
 * *********************************************************/


#ifndef _MODE_H_
#define _MODE_H_
#include <freight-config.h>
#include <freight-db.h>

int init_container_root(const struct db_api *api,
			const struct agent_config *acfg);

void clean_container_root(const char *croot);

int install_container(const char *rpm, struct agent_config *acfg);

int uninstall_container(const char *rpm, struct agent_config *acfg);

int enter_mode_loop(struct db_api *api, struct agent_config *config);

void list_containers(char *scope, struct agent_config *config);
#endif
