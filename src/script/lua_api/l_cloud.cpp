// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "l_cloud.h"

#include <json/json.h>

#include "common/c_content.h"
#include "cloud/cloud_config.h"
#include "cloud/cloud_service.h"
#include "l_internal.h"

namespace
{

void pushJson(lua_State *L, const Json::Value &value)
{
	lua_pushnil(L); // nullindex
	push_json_value(L, value, lua_gettop(L));
	lua_remove(L, -2); // remove the nil again
}

} // anonymous namespace

int ModApiCloud::l_cloud_status(lua_State *L)
{
	pushJson(L, cloud::CloudService::get().statusJson());
	return 1;
}

int ModApiCloud::l_cloud_reload_auth(lua_State *L)
{
	cloud::CloudService::get().reloadAuth();
	lua_pushboolean(L, cloud::CloudConfig::get().isPaired());
	return 1;
}

int ModApiCloud::l_cloud_site_url(lua_State *L)
{
	std::string url = cloud::CloudConfig::get().siteUrl();
	lua_pushstring(L, url.c_str());
	return 1;
}

int ModApiCloud::l_cloud_refresh(lua_State *L)
{
	cloud::CloudService::get().refreshFriends();
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_get_friends(lua_State *L)
{
	Json::Value v(Json::objectValue);
	v["friends"] = cloud::CloudService::get().friendsJson();
	pushJson(L, v);
	return 1;
}

int ModApiCloud::l_cloud_get_unread(lua_State *L)
{
	pushJson(L, cloud::CloudService::get().unreadJson());
	return 1;
}

int ModApiCloud::l_cloud_get_messages(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	pushJson(L, cloud::CloudService::get().messagesJson(username));
	return 1;
}

int ModApiCloud::l_cloud_load_older(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	long before = (long)luaL_checknumber(L, 2);
	cloud::CloudService::get().requestHistory(username, before);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_send_dm(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	std::string body = luaL_checkstring(L, 2);
	cloud::CloudService::get().sendDM(username, body);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_mark_read(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	cloud::CloudService::get().markRead(username);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_set_open_chat(lua_State *L)
{
	std::string username = lua_isnoneornil(L, 1) ? "" : luaL_checkstring(L, 1);
	cloud::CloudService::get().setOpenChat(username);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_get_party(lua_State *L)
{
	const Json::Value &party = cloud::CloudService::get().partyJson();
	if (party.isNull()) {
		lua_pushnil(L);
	} else {
		pushJson(L, party);
	}
	return 1;
}

int ModApiCloud::l_cloud_party_create(lua_State *L)
{
	std::string address = lua_isnoneornil(L, 1) ? "" : luaL_checkstring(L, 1);
	cloud::CloudService::get().partyCreate(address);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_party_join(lua_State *L)
{
	std::string code = luaL_checkstring(L, 1);
	cloud::CloudService::get().partyJoin(code);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_party_leave(lua_State *L)
{
	cloud::CloudService::get().partyLeave();
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_party_end(lua_State *L)
{
	cloud::CloudService::get().partyEnd();
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_party_server(lua_State *L)
{
	std::string address = luaL_checkstring(L, 1);
	cloud::CloudService::get().partySetServer(address);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_party_kick(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	cloud::CloudService::get().partyKick(username);
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_join_friend(lua_State *L)
{
	std::string username = luaL_checkstring(L, 1);
	bool ok = cloud::CloudService::get().joinFriend(username);
	lua_pushboolean(L, ok);
	return 1;
}

int ModApiCloud::l_cloud_join_code(lua_State *L)
{
	std::string code = luaL_checkstring(L, 1);
	bool ok = cloud::CloudService::get().joinRoomCode(code);
	lua_pushboolean(L, ok);
	return 1;
}

int ModApiCloud::l_cloud_join_status(lua_State *L)
{
	pushJson(L, cloud::CloudService::get().joinStatusJson());
	return 1;
}

int ModApiCloud::l_cloud_join_cancel(lua_State *L)
{
	cloud::CloudService::get().joinCancel();
	lua_pushboolean(L, true);
	return 1;
}

int ModApiCloud::l_cloud_host_status(lua_State *L)
{
	pushJson(L, cloud::CloudService::get().hostStatusJson());
	return 1;
}

int ModApiCloud::l_cloud_poll_events(lua_State *L)
{
	lua_newtable(L);
	int index = 1;
	cloud::CloudService::Event ev;
	while (cloud::CloudService::get().popEvent(ev)) {
		Json::Value v(Json::objectValue);
		v["type"] = ev.type;
		v["data"] = ev.data;
		lua_pushnumber(L, index++);
		pushJson(L, v);
		lua_settable(L, -3);
	}
	return 1;
}

void ModApiCloud::Initialize(lua_State *L, int top)
{
	API_FCT(cloud_status);
	API_FCT(cloud_reload_auth);
	API_FCT(cloud_site_url);
	API_FCT(cloud_refresh);
	API_FCT(cloud_get_friends);
	API_FCT(cloud_get_unread);
	API_FCT(cloud_get_messages);
	API_FCT(cloud_load_older);
	API_FCT(cloud_send_dm);
	API_FCT(cloud_mark_read);
	API_FCT(cloud_set_open_chat);
	API_FCT(cloud_get_party);
	API_FCT(cloud_party_create);
	API_FCT(cloud_party_join);
	API_FCT(cloud_party_leave);
	API_FCT(cloud_party_end);
	API_FCT(cloud_party_server);
	API_FCT(cloud_party_kick);
	API_FCT(cloud_join_friend);
	API_FCT(cloud_join_code);
	API_FCT(cloud_join_status);
	API_FCT(cloud_join_cancel);
	API_FCT(cloud_host_status);
	API_FCT(cloud_poll_events);
}
