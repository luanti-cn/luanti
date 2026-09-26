// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "cloud_http_client.h"
#include "host_room_controller.h"
#include "irrlichttypes.h"
#include "join_controller.h"
#include "realtime_client.h"
#include "util/container.h"

namespace cloud
{

/// Central facade owning all cloud connections and caches.
/// init()/step()/shutdown() are called from the main thread (menu & game).
class CloudService
{
public:
	struct Event
	{
		std::string type;    // dm.new / friend.request / ... / notify
		Json::Value data;
	};

	static CloudService &get();

	void init();
	void shutdown();
	/// pump HTTP results + WS events + controller timers
	void step();

	// ---- session ----
	/// {paired, username, displayName, deviceName, ws}
	Json::Value statusJson() const;
	void reloadAuth();

	// ---- friends ----
	void refreshFriends();
	/// array of FriendInfo objects (cached)
	Json::Value friendsJson() const;
	void setFriendOnline(const std::string &username, bool online,
			const std::string &address);

	// ---- messages ----
	void refreshUnread();
	/// {total=N, users={name=count,...}}
	Json::Value unreadJson() const;
	/// request (async) history page; results appear in messagesJson()
	void requestHistory(const std::string &username, long before_id = 0);
	/// cached conversation: {messages=[...], hasMore=bool, ready=bool}
	Json::Value messagesJson(const std::string &username) const;
	/// WS dm.send with REST fallback; result arrives as dm.ack event
	void sendDM(const std::string &username, const std::string &body,
			const std::string &client_id = "");
	void markRead(const std::string &username);
	/// the chat dialog currently open (suppresses unread counting)
	void setOpenChat(const std::string &username);
	/// total unread across all friends
	int unreadTotal() const { return m_unread_total; }

	// ---- party ----
	Json::Value partyJson() const { return m_party; }
	void partyCreate(const std::string &server_address);
	void partyJoin(const std::string &code);
	void partyLeave();
	void partyEnd();
	void partySetServer(const std::string &address);
	void partyKick(const std::string &username);

	// ---- hosting / joining ----
	void startHosting(u16 server_port);
	void stopHosting() { m_host.stopHosting(); }
	Json::Value hostStatusJson() const { return m_host.statusJson(); }
	bool joinFriend(const std::string &username)
	{
		return m_join.prepareJoin(username, "");
	}
	bool joinRoomCode(const std::string &code)
	{
		return m_join.prepareJoin("", code);
	}
	void joinCancel() { m_join.cancel(); }
	Json::Value joinStatusJson() const { return m_join.statusJson(); }

	// ---- UI/game events ----
	bool popEvent(Event &out);
	void pushEvent(const std::string &type, const Json::Value &data);

	// ---- presence ----
	void setPresenceAddress(const std::string &address);

	bool wsConnected() const { return m_realtime.wsConnected(); }

private:
	CloudService() = default;

	void handleWsEvent(const Json::Value &msg);
	void appendMessage(const Json::Value &msg); // dm.new shaped object
	void refreshFriendsFromRest();
	void recomputeUnreadTotal();
	void autoRefreshTick(u64 now_ms);

	CloudHttpClient m_http;
	RealtimeClient m_realtime;
	HostRoomController m_host;
	JoinController m_join;

	// caches (main thread only)
	std::vector<Json::Value> m_friends;
	std::map<std::string, int> m_unread;
	int m_unread_total = 0;
	std::map<std::string, std::deque<Json::Value>> m_messages;
	std::map<std::string, bool> m_has_more;
	std::map<std::string, bool> m_history_ready;
	std::string m_open_chat;
	Json::Value m_party{Json::nullValue};

	MutexedQueue<Event> m_events;

	// host tunnel thread -> main thread handover
	std::mutex m_internal_mutex;
	std::vector<Candidate> m_internal_candidates;

	std::string m_presence_address;
	u64 m_last_friends_refresh = 0;
	u64 m_last_unread_refresh = 0;
	bool m_initialized = false;
};

} // namespace cloud
