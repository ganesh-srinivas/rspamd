/*
 * Copyright (c) 2009, Rambler media
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Rambler media ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Rambler BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lua_common.h"

/* Lua module init function */
#define MODULE_INIT_FUNC "module_init"

lua_State *L = NULL;
const luaL_reg null_reg[] = {
    {NULL, NULL}
};

/* Logger methods */
LUA_FUNCTION_DEF(logger, _err);
LUA_FUNCTION_DEF(logger, _warn);
LUA_FUNCTION_DEF(logger, _info);
LUA_FUNCTION_DEF(logger, _debug);

static const struct luaL_reg loggerlib_m[] = {
    LUA_INTERFACE_DEF(logger, _err),
    LUA_INTERFACE_DEF(logger, _warn),
    LUA_INTERFACE_DEF(logger, _info),
    LUA_INTERFACE_DEF(logger, _debug),
    {NULL, NULL}
};

/* Util functions */
void 
lua_newclass (lua_State *L, const char *classname, const struct luaL_reg *func) 
{
	luaL_newmetatable (L, classname); /* mt */
	/* create __index table to place methods */
	lua_pushstring (L, "__index");    /* mt,"__index" */
	lua_newtable (L);                 /* mt,"__index",it */
	/* put class name into class metatable */
	lua_pushstring (L, "class");      /* mt,"__index",it,"class" */
	lua_pushstring (L, classname);    /* mt,"__index",it,"class",classname */
	lua_rawset (L, -3);               /* mt,"__index",it */
	/* pass all methods that start with _ to the metatable, and all others
	 * to the index table */
	for (; func->name; func++) {     /* mt,"__index",it */
		lua_pushstring (L, func->name);
		lua_pushcfunction (L, func->func);
		lua_rawset (L, func->name[0] == '_' ? -5: -3);
	}
	lua_rawset (L, -3);               /* mt */
	lua_pop (L, 1);
}

void 
lua_setclass (lua_State *L, const char *classname, int objidx) 
{
	luaL_getmetatable (L, classname);
	if (objidx < 0) {
		objidx--;
	}
	lua_setmetatable (L, objidx);
}

/* assume that table is at the top */
void 
lua_set_table_index (lua_State *L, const char *index, const char *value) 
{

	lua_pushstring (L, index);
	lua_pushstring (L, value);
	lua_settable(L, -3);
}


/*** Logger interface ***/
static int
lua_logger__err (lua_State *L)
{
    const char *msg;
    msg = luaL_checkstring (L, 2);
    msg_err (msg);
    return 1;
}

static int
lua_logger__warn (lua_State *L)
{
    const char *msg;
    msg = luaL_checkstring (L, 2);
    msg_warn (msg);
    return 1;
}

static int
lua_logger__info (lua_State *L)
{
    const char *msg;
    msg = luaL_checkstring (L, 2);
    msg_info (msg);
    return 1;
}

static int
lua_logger__debug (lua_State *L)
{
    const char *msg;
    msg = luaL_checkstring (L, 2);
    msg_debug (msg);
    return 1;
}


/*** Init functions ***/

int
luaopen_rspamd (lua_State *L)
{
	luaL_openlib(L, "rspamd", null_reg, 0);
    /* make version string available to scripts */
	lua_pushstring(L, "_VERSION");
	lua_pushstring(L, RVERSION);
    lua_rawset(L, -3);
    
	return 1;
}

int
luaopen_logger (lua_State *L)
{

    lua_newclass (L, "rspamd{logger}", loggerlib_m);
	luaL_openlib (L, NULL, null_reg, 0);

    return 1;
}

static void
init_lua ()
{
	if (L == NULL) {
		L = lua_open ();
		luaL_openlibs (L);
        
        (void)luaopen_rspamd (L);
        (void)luaopen_logger (L);
        (void)luaopen_config (L);
        (void)luaopen_metric (L);
		(void)luaopen_task (L);
        (void)luaopen_textpart (L);
		(void)luaopen_message (L);
	}
}

void
init_lua_filters (struct config_file *cfg)
{
	struct config_file **pcfg;
	GList *cur;
	struct script_module *module;
	
	init_lua ();
	cur = g_list_first (cfg->script_modules);
	while (cur) {
		module = cur->data;
		if (module->path) {
			if (luaL_loadfile (L, module->path) != 0) {
				msg_info ("lua_init_filters: load of %s failed: %s", module->path, lua_tostring (L, -1));
		        cur = g_list_next (cur);
                continue;
            }

			/* Call module init function */
			pcfg = lua_newuserdata (L, sizeof (struct config_file *));
			lua_setclass (L, "rspamd{config}", -1);
			*pcfg = cfg;
            lua_setglobal (L, "rspamd_config");
			/* do the call (1 arguments, 1 result) */
            if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
				msg_info ("lua_init_filters: init of %s failed: %s", module->path, lua_tostring (L, -1));
			}
		}
		cur = g_list_next (cur);
	}
}

/* Callback functions */

int
lua_call_filter (const char *function, struct worker_task *task)
{
	int result;
	struct worker_task **ptask;

	lua_getglobal (L, function);
	ptask = lua_newuserdata (L, sizeof (struct worker_task *));
	lua_setclass (L, "rspamd{task}", -1);
	*ptask = task;
	
	if (lua_pcall (L, 1, 1, 0) != 0) {
		msg_info ("lua_call_filter: call to %s failed", function);
	}

	/* retrieve result */
	if (!lua_isnumber (L, -1)) {
		msg_info ("lua_call_filter: function %s must return a number", function);
	}
	result = lua_tonumber (L, -1);
	lua_pop (L, 1);  /* pop returned value */
	return result;
}

int
lua_call_chain_filter (const char *function, struct worker_task *task, int *marks, unsigned int number)
{
	int result, i;

	lua_getglobal (L, function);

	for (i = 0; i < number; i ++) {
		lua_pushnumber (L, marks[i]);
	}
	if (lua_pcall (L, number, 1, 0) != 0) {
		msg_info ("lua_init_filters: call to %s failed", function);
	}

	/* retrieve result */
	if (!lua_isnumber (L, -1)) {
		msg_info ("lua_call_header_filter: function %s must return a number", function);
	}
	result = lua_tonumber (L, -1);
	lua_pop (L, 1);  /* pop returned value */
	return result;
}

/*
 * LUA custom consolidation function
 */
struct consolidation_callback_data {
	struct worker_task *task;
	double score;
	const char *func;
};

static void
lua_consolidation_callback (gpointer key, gpointer value, gpointer arg)
{
	double res;
	struct symbol *s = (struct symbol *)value;
	struct consolidation_callback_data *data = (struct consolidation_callback_data *)arg;

	lua_getglobal (L, data->func);

	lua_pushstring (L, (const char *)key);
	lua_pushnumber (L, s->score);
	if (lua_pcall (L, 2, 1, 0) != 0) {
		msg_info ("lua_consolidation_callback: call to %s failed", data->func);
	}

	/* retrieve result */
	if (!lua_isnumber (L, -1)) {
		msg_info ("lua_consolidation_callback: function %s must return a number", data->func);
	}
	res = lua_tonumber (L, -1);
	lua_pop (L, 1);  /* pop returned value */
	data->score += res;
}

double
lua_consolidation_func (struct worker_task *task, const char *metric_name, const char *function_name)
{
	struct metric_result *metric_res;
	double res = 0.;
	struct consolidation_callback_data data = { task, 0, function_name };

	if (function_name == NULL) {
		return 0;
	}

	metric_res = g_hash_table_lookup (task->results, metric_name);
	if (metric_res == NULL) {
		return res;
	}

	g_hash_table_foreach (metric_res->symbols, lua_consolidation_callback, &data);

	return data.score;
}

void
add_luabuf (const char *line)
{
	int error;
	init_lua ();

	error = luaL_loadbuffer(L, line, strlen(line), "config")  ||
			lua_pcall(L, 0, 0, 0);
	if (error) {
		yyerror ("lua error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);  /* pop error message from the stack */
	}
}
