/************************************************************************
* config.c
* voxelands - 3d voxel world sandbox game
* Copyright (C) Lisa 'darkrose' Milne 2016 <lisa@ltmnet.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
************************************************************************/

#include "common.h"
#include "auth.h"
#include "nvp.h"
#include "crypto.h"
#include "file.h"
#include "array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
	nvp_t *items;
	nvp_t *files;
	int isinit;
} config = {
	NULL,
	NULL,
	0
};

typedef struct config_s {
	char* default_value;
	int (*setter)(char* v);
} config_t;

typedef struct sort_s {
	struct sort_s *prev;
	struct sort_s *next;
	char *name;
	char *value;
} sort_t;

/* get the value of a config setting */
char* config_get(char* name)
{
	nvp_t *n = nvp_get(&config.items,name);
	if (!n)
		return NULL;

	if (!n->value)
		return ((config_t*)n->data)->default_value;

	return n->value;
}

/* get a config setting as an int value */
int config_get_int(char* name)
{
	char* v = config_get(name);
	if (v)
		return strtol(v,NULL,10);

	return 0;
}

/* get a config setting as an int value */
int64_t config_get_int64(char* name)
{
	char* v = config_get(name);
	if (v)
		return strtoll(v,NULL,10);

	return 0;
}

/* get a config setting as a float value */
float config_get_float(char* name)
{
	char* v = config_get(name);
	if (v)
		return strtof(v,NULL);

	return 0.0;
}

/* get a config setting as a boolean value */
int config_get_bool(char* name)
{
	char* v = config_get(name);
	return parse_bool(v);
}

/* get a config setting as a v3_t value */
int config_get_v3t(char* name, v3_t *value)
{
	char* v = config_get(name);
	if (!v)
		return 1;

	return str_tov3t(v,value);
}

/* set the value of a config setting */
void config_set(char* name, char* value)
{
	config_t *c;
	nvp_t *n;

	if (!name)
		return;

	n = nvp_get(&config.items,name);

	if (!n) {
		if (!value)
			return;

		c = malloc(sizeof(config_t));
		c->default_value = NULL;
		c->setter = NULL;

		nvp_set(&config.items,name,value,c);

		return;
	}

	if (n->value)
		free(n->value);
	n->value = NULL;

	if (value)
		n->value = strdup(value);

	c = n->data;
	if (c->setter) {
		if (!n->value && c->default_value) {
			c->setter(c->default_value);
		}else{
			c->setter(n->value);
		}
	}
}

/* set a config setting from a command */
int config_set_command(command_context_t *ctx, array_t *args)
{
	char* n;
	char* v;
	if (!args)
		return 1;

	if (ctx && (ctx->privs&PRIV_SERVER) == 0) {
		command_print(ctx,SEND_TO_SENDER,CN_WARN,"You don't have permission to do that");
		return 1;
	}

	n = array_get_string(args,0);
	v = array_join(args," ",1);
	config_set(n,v);

	return 0;
}

/* set a config setting to an int value */
void config_set_int(char* name, int value)
{
	char str[20];
	sprintf(str,"%d",value);
	config_set(name,str);
}

/* set a config setting to a 64bit int value */
void config_set_int64(char* name, int64_t value)
{
	char str[50];
	sprintf(str,"%ld",value);
	config_set(name,str);
}

/* set a config setting to a float value */
void config_set_float(char* name, float value)
{
	char str[50];
	sprintf(str,"%f",value);
	config_set(name,str);
}

/* set the default value of a config setting */
void config_set_default(char* name, char* value, int (*setter)(char* v))
{
	config_t *c;
	nvp_t *n = nvp_get(&config.items,name);

	if (!n) {
		if (!value && !setter)
			return;

		c = malloc(sizeof(config_t));
		if (value) {
			c->default_value = strdup(value);
		}else{
			c->default_value = NULL;
		}
		c->setter = setter;

		nvp_set(&config.items,name,NULL,c);

		if (setter)
			setter(value);

		return;
	}

	c = n->data;

	if (c->default_value)
		free(c->default_value);
	c->default_value = NULL;

	if (value)
		c->default_value = strdup(value);
}

/* set the default of a config setting to an int value */
void config_set_default_int(char* name, int value, int (*setter)(char* v))
{
	char str[20];
	sprintf(str,"%d",value);
	config_set_default(name,str,setter);
}

/* set the default of a config setting to a float value */
void config_set_default_float(char* name, float value, int (*setter)(char* v))
{
	char str[50];
	sprintf(str,"%f",value);
	config_set_default(name,str,setter);
}

/* load a config file */
void config_load(char* type, char* file)
{
	char buff[2048];
	int s;
	char* l;
	file_t *f;
	command_context_t ctx;

	if (!type)
		type = "config";

	f = file_load(type,file);
	if (!f)
		return;

	ctx.player[0] = 0;
	ctx.privs = PRIV_ALL;
	ctx.flags = 0;
	ctx.bridge_server = NULL;
	ctx.bridge_env = NULL;
	ctx.bridge_player = NULL;

	while ((s = file_readline(f,buff,2048)) > -1) {
		if (!s || buff[0] == '#')
			continue;
		l = trim(buff);
		if (l && l[0])
			command_exec(&ctx,l);
	}

	file_free(f);
}

/* load a config file from a command */
int config_load_command(command_context_t *ctx, array_t *args)
{
	char* f;
	if (!args)
		return 1;

	if (ctx && (ctx->privs&PRIV_SERVER) == 0) {
		command_print(ctx,SEND_TO_SENDER,CN_WARN,"You don't have permission to do that");
		return 1;
	}

	f = array_get_string(args,0);
	nvp_set(&config.files,f,"true",NULL);
	config_load("config",f);

	return 0;
}

/* set the ignore flag for a config file from a command */
int config_ignore_command(command_context_t *ctx, array_t *args)
{
	char* f;
	if (!args)
		return 1;

	if (ctx && (ctx->privs&PRIV_SERVER) == 0) {
		command_print(ctx,SEND_TO_SENDER,CN_WARN,"You don't have permission to do that");
		return 1;
	}

	f = array_get_string(args,0);

	nvp_set(&config.files,f,"false",NULL);

	return 0;
}

/* initialise configuration, load config files and defaults, etc */
void config_init(int argc, char** argv)
{
	int i;
	nvp_t *n;

	config_default_init();

	/* add the default config file to the to-exec list */
	nvp_set(&config.files,"default.cfg","true",NULL);

	for (i=1; i<(argc-1); i++) {
		if (!strcmp(argv[i],"exec")) {
			i += 1;
			nvp_set(&config.files,argv[i],"true",NULL);
		}else if (!strcmp(argv[i],"ignore")) {
			i += 1;
			nvp_set(&config.files,argv[i],"false",NULL);
		}
	}

	n = config.files;
	while (n) {
		if (n->value && !strcmp(n->value,"true"))
			config_load("config",n->name);
		n = n->next;
	}

	for (i=0; i<argc; i++) {
		if (!strcmp(argv[i],"set") && i+2 < argc) {
			config_set(argv[i+1],argv[i+2]);
			i+=2;
		}else if (!strcmp(argv[i],"unset") && i+1 < argc) {
			i+=1;
			config_set(argv[i],NULL);
		}else if (!strcmp(argv[i],"exec")) {
			i += 1;
		}else if (!strcmp(argv[i],"ignore")) {
			i += 1;
		}
	}

	config.isinit = 1;
}

/* save the current config */
void config_save(char* section, char* type, char* file)
{
	file_t *f;
	nvp_t *n;

	if (!type && !file) {
		n = config.files;
		while (n) {
			if (n->value && !strcmp(n->value,"true"))
				break;
			n = n->next;
		}

		/* TODO: should probably force saving to somewhere, custom.cfg? */
		if (!n)
			return;

		f = file_create("config",n->name);
	}else{
		f = file_create(type,file);
	}

	if (!f)
		return;

	if (section) {
		int l;
		l = strlen(section);
		n = config.items;
		while (n) {
			if (n->value && !strncmp(n->name,section,l))
				file_writef(f,"set %s %s\n",n->name,n->value);
			n = n->next;
		}
	}else{
		sort_t *copyhead = 0;
		sort_t *copytail = 0;
		sort_t *copying = 0;
		sort_t *copied = 0;
		int copy_failed=0;
		
		// get config name/value pairs in new list
		n = config.items;
		while(n) {
			if (!n->name) continue;
			copying=malloc(sizeof(sort_t));
			if (!copying) {
				copy_failed=1;
				break;
			}
			if (!copyhead)
				// remember the first entry in the list for sorting
				copyhead=copying;
			if (copied) {
				copied->next = copying;
				copying->prev = copied;
			}
			else {
				copying->prev = 0;
			}
		
			copying->name = strdup(n->name);
			if (n->value)
				copying->value = strdup(n->value);
			else
				copying->value = 0;
			
			copying->next = 0;
			copied = copying;
			n = n->next;
		}
		// remember the last entry in the list for cleaning up
		copytail=copied;

		// alpha sort name/value pairs by name
		sort_t *sprev;
		sort_t *scurr;
		char *store;

		if (!copy_failed && copyhead) {
			scurr=copyhead;
			while(scurr->next){
				if (strcmp(scurr->name,scurr->next->name)>0){
					store=scurr->name;
					scurr->name = scurr->next->name;
					scurr->next->name = store;
					store=scurr->value;
					scurr->value=scurr->next->value;
					scurr->next->value=store;
					sprev=scurr;
					while(sprev->prev){
						if (strcmp(sprev->prev->name,sprev->name) > 0) {
							store=sprev->name;
							sprev->name = sprev->prev->name;
							sprev->prev->name = store;
							store=sprev->value;
							sprev->value = sprev->prev->value;
							sprev->prev->value=store;
							sprev=sprev->prev;
						}
						else break;
					}
				}
				scurr=scurr->next;
			}
		}

		// save one of the lists to file
		if (copy_failed) {
			while (n) {
				if (n->value)
					file_writef(f,"set %s %s\n",n->name,n->value);
				n = n->next;
			}
		} else {
			scurr=copyhead;
			while (scurr) {
				if (scurr->value)
					file_writef(f,"set %s %s\n",scurr->name,scurr->value);
				scurr=scurr->next;
			}
		}

		// free list memory and clean up
		while(copytail) {
			if (!(copytail->prev)) break;
			copytail=copytail->prev;
			free(copytail->next);
			copytail->next=0;
		}
		free(copytail);

		copyhead=0;
		copytail=0;
		copying=0;
		copied=0;
		sprev=0;
		scurr=0;
		store=0;

/*
		events_save(f);
*/
		command_save(f);
	}

	file_flush(f);
	file_free(f);
}

/* clears all config values for section (i.e. "world.*") to defaults */
void config_clear(char* section)
{
	nvp_t *n;
	int l;

	/* don't clear everything, only sections */
	if (!section)
		return;

	l = strlen(section);
	n = config.items;
	while (n) {
		if (n->value && !strncmp(n->name,section,l)) {
			free(n->value);
			n->value = NULL;
		}
		n = n->next;
	}
}
