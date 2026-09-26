// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include "lua_api/l_base.h"

class ModApiCloud : public ModApiBase
{
private:
	// session
	static int l_cloud_status(lua_State *L);
	static int l_cloud_reload_auth(lua_State *L);
	static int l_cloud_site_url(lua_State *L);

	// friends / messages
	static int l_cloud_refresh(lua_State *L);
	static int l_cloud_get_friends(lua_State *L);
	static int l_cloud_get_unread(lua_State *L);
	static int l_cloud_get_messages(lua_State *L);
	static int l_cloud_load_older(lua_State *L);
	static int l_cloud_send_dm(lua_State *L);
	static int l_cloud_mark_read(lua_State *L);
	static int l_cloud_set_open_chat(lua_State *L);

	// party
	static int l_cloud_get_party(lua_State *L);
	static int l_cloud_party_create(lua_State *L);
	static int l_cloud_party_join(lua_State *L);
	static int l_cloud_party_leave(lua_State *L);
	static int l_cloud_party_end(lua_State *L);
	static int l_cloud_party_server(lua_State *L);
	static int l_cloud_party_kick(lua_State *L);

	// multiplayer host/join
	static int l_cloud_join_friend(lua_State *L);
	static int l_cloud_join_code(lua_State *L);
	static int l_cloud_join_status(lua_State *L);
	static int l_cloud_join_cancel(lua_State *L);
	static int l_cloud_host_status(lua_State *L);

	// event pump for UI
	static int l_cloud_poll_events(lua_State *L);

public:
	static void Initialize(lua_State *L, int top);
};
