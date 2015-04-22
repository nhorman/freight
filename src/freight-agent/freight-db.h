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
 * *File: freight-db.h
 * *
 * *Author:Neil Horman
 * *
 * *Date: April 16, 2015
 * *
 * *Description API for DB connections
 * *********************************************************/


#ifndef _FREIGHT_DB_H_
#define _FREIGHT_DB_H_
#include <stdio.h>
#include <errno.h>
#include <freight-config.h>


struct yum_config {
	char *name;
	char *url;
};

struct yum_cfg_list {
	size_t cnt;
	struct yum_config list[0];
};

struct db_api {

	/* setup and teardown functions */
	int (*init)(struct agent_config *acfg);
	void (*cleanup)(struct agent_config *acfg);
	int (*connect)(struct agent_config *acfg);
	int (*disconnect)(struct agent_config *acfg);

	/* operational methods */
	struct yum_cfg_list *(*get_yum_cfg)(const struct agent_config *acfg);

};

extern struct db_api postgres_db_api;

static inline struct db_api* get_db_api(struct agent_config *acfg) {

	switch (acfg->db.dbtype) {
	case DB_TYPE_POSTGRES:
		return &postgres_db_api;
	default:
		return NULL;
	}
}

static inline int db_init(struct db_api *api, struct agent_config *acfg)
{
	if (!api->init)
		return -EOPNOTSUPP;
	return api->init(acfg);	
}

static inline void db_cleanup(struct db_api *api, struct agent_config *acfg)
{
	if (!api->cleanup)
		return;
	return api->cleanup(acfg);	
}

static inline int db_connect(struct db_api *api, struct agent_config *acfg)
{
	if (!api->connect)
		return -EOPNOTSUPP;
	return api->connect(acfg);	
}

static inline int db_disconnect(struct db_api *api, struct agent_config *acfg)
{
	if (!api->disconnect)
		return -EOPNOTSUPP;
	return api->disconnect(acfg);	
}

static inline struct yum_cfg_list* db_get_yum_cfg(const struct db_api *api,
						  const struct agent_config *acfg)
{
	if (!api->get_yum_cfg)
		return NULL;
	return api->get_yum_cfg(acfg);
}

#endif