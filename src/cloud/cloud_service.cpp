// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "cloud_service.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <random>

#include "cloud_config.h"
#include "convert_json.h"
#include "log.h"
#include "porting.h"

namespace cloud
{

static constexpr u64 AUTO_REFRESH_INTERVAL_MS = 60'000;

static std::string makeClientId()
{
	static thread_local std::mt19937 rng((u32)(time(nullptr) & 0xFFFFFFFFu));
	u32 v = rng();
	char buf[16];
	snprintf(buf, sizeof(buf), "%08x", v);
	return std::string("lt") + buf;
}

CloudService &CloudService::get()
{
	static CloudService singleton;
	return singleton;
}

void CloudService::init()
{
	if (m_initialized)
		return;
	CloudConfig::get().refreshSettings();
	CloudConfig::get().reloadAuth();

	m_http.begin(CloudConfig::get().baseUrl());
	m_realtime.begin(CloudConfig::get().wsUrl());
	m_host.begin(&m_http, &m_realtime);
	m_host.setCandidatesSink([this](std::vector<Candidate> candidates) {
		// called on the tunnel thread; hand over to the main thread
		std::lock_guard<std::mutex> lock(m_internal_mutex);
		m_internal_candidates = std::move(candidates);
	});
	m_join.begin(&m_http, &m_realtime);

	m_initialized = true;

	if (CloudConfig::get().isPaired()) {
		refreshFriends();
		refreshUnread();
	}
}

void CloudService::shutdown()
{
	if (!m_initialized)
		return;
	m_host.stopHosting();
	m_join.cancel();
	m_realtime.stop();
	m_realtime.wait();
	m_initialized = false;
}

void CloudService::step()
{
	if (!m_initialized)
		return;
	u64 now = porting::getTimeMs();

	m_http.step();
	m_host.step(now);

	// WS events
	Json::Value msg;
	while (m_realtime.popEvent(msg))
		handleWsEvent(msg);

	// candidate updates from the host tunnel thread
	{
		std::vector<Candidate> candidates;
		{
			std::lock_guard<std::mutex> lock(m_internal_mutex);
			candidates = std::move(m_internal_candidates);
			m_internal_candidates.clear();
		}
		if (!candidates.empty())
			m_host.onCandidates(std::move(candidates));
	}

	autoRefreshTick(now);
}

// ---------------------------------------------------------------------------
// WS event dispatch
// ---------------------------------------------------------------------------

void CloudService::handleWsEvent(const Json::Value &msg)
{
	if (!msg.isObject())
		return;
	std::string type = msg["type"].asString();

	if (type == "friends.snapshot") {
		m_friends.clear();
		const Json::Value &arr = msg["friends"];
		if (arr.isArray())
			for (const auto &f : arr) {
				if (!f.isObject() || !f["username"].isString() ||
						f["username"].asString().empty()) {
					warningstream << "Cloud: skipping friend entry with "
							"empty username: " << fastWriteJson(f)
							<< std::endl;
					continue;
				}
				m_friends.push_back(f);
			}
		return;
	}
	if (type == "presence") {
		setFriendOnline(msg["username"].asString(), msg["online"].asBool(),
				msg["address"].asString());
		pushEvent("presence", msg);
		return;
	}
	if (type == "dm.new") {
		appendMessage(msg);
		std::string from = msg["from"].asString();
		if (m_open_chat == from) {
			// open dialog: immediately mark read
			Json::Value r(Json::objectValue);
			r["type"] = "dm.read";
			r["from"] = from;
			m_realtime.send(r);
		} else {
			m_unread[from]++;
			m_unread_total++;
		}
		pushEvent("dm.new", msg);
		return;
	}
	if (type == "dm.ack") {
		pushEvent("dm.ack", msg);
		return;
	}
	if (type == "friend.request" || type == "friend.accepted") {
		pushEvent(type, msg);
		refreshFriendsFromRest();
		return;
	}
	if (type == "party.update") {
		m_party = msg["party"];
		pushEvent("party.update", m_party);
		return;
	}
	if (type == "room.candidates") {
		warningstream << "Cloud: room.candidates push ("
				<< msg["candidates"].size() << " entries)" << std::endl;
		m_join.onRoomCandidates(msg);
		return;
	}
	if (type == "room.signal") {
		// deliver to whichever side we are: the joining guest *and* the
		// hosting player (each ignores signals for rooms it isn't in)
		m_host.onRoomSignal(msg);
		m_join.onRoomSignal(msg);
		return;
	}
	if (type == "error") {
		pushEvent("error", msg);
		return;
	}
	if (type == "hello") {
		// realtime channel just authenticated: pull fresh state
		refreshFriendsFromRest();
		refreshUnread();
		return;
	}
	// pong / dm.read: nothing to do
}

// ---------------------------------------------------------------------------
// friends
// ---------------------------------------------------------------------------

void CloudService::refreshFriends()
{
	refreshFriendsFromRest();
	refreshUnread();
}

void CloudService::refreshFriendsFromRest()
{
	m_last_friends_refresh = porting::getTimeMs();
	m_http.get("/api/cloud/client/friends/", true,
			[this](const CloudHttpResult &result) {
		if (!result.success || !result.data.isObject()) {
			warningstream << "Cloud: friends list request failed (HTTP "
					<< result.code << "): " << result.error << std::endl;
			if (result.code == 401) {
				// token revoked/expired: force re-pairing
				CloudConfig::get().clearAuth();
				pushEvent("notify", "登录状态已失效,请重新配对设备");
			} else if (!result.error.empty()) {
				pushEvent("notify", "好友列表获取失败: " + result.error);
			}
			return;
		}
		m_friends.clear();
		const Json::Value &arr = result.data["friends"];
		if (arr.isArray())
			for (const auto &f : arr) {
				if (!f.isObject() || !f["username"].isString() ||
						f["username"].asString().empty()) {
					warningstream << "Cloud: skipping friend entry with "
							"empty username: " << fastWriteJson(f)
							<< std::endl;
					continue;
				}
				m_friends.push_back(f);
			}
	});
}

void CloudService::setFriendOnline(const std::string &username, bool online,
		const std::string &address)
{
	for (auto &f : m_friends) {
		if (f.isObject() && f["username"].asString() == username) {
			f["online"] = online;
			f["currentServerAddress"] = online ? address : Json::Value();
			return;
		}
	}
}

Json::Value CloudService::friendsJson() const
{
	Json::Value arr(Json::arrayValue);
	for (const auto &f : m_friends)
		arr.append(f);
	return arr;
}

// ---------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------

void CloudService::refreshUnread()
{
	m_last_unread_refresh = porting::getTimeMs();
	m_http.get("/api/cloud/client/messages/unread/", true,
			[this](const CloudHttpResult &result) {
		if (!result.success || !result.data.isObject()) {
			warningstream << "Cloud: unread count request failed (HTTP "
					<< result.code << "): " << result.error << std::endl;
			return;
		}
		m_unread.clear();
		m_unread_total = 0;
		const Json::Value &arr = result.data["unread"];
		if (arr.isArray()) {
			for (const auto &e : arr) {
				std::string user = e["username"].asString();
				int count = e["count"].asInt();
				m_unread[user] = count;
				m_unread_total += count;
			}
		}
	});
}

Json::Value CloudService::unreadJson() const
{
	Json::Value v(Json::objectValue);
	v["total"] = m_unread_total;
	Json::Value users(Json::objectValue);
	for (const auto &[user, count] : m_unread)
		users[user] = count;
	v["users"] = users;
	return v;
}

void CloudService::requestHistory(const std::string &username, long before_id)
{
	std::string path = "/api/cloud/client/messages/" + username + "/";
	path += before_id > 0 ?
			"?before=" + std::to_string(before_id) + "&limit=30" : "?limit=30";

	m_http.get(path, true,
			[this, username, before_id](const CloudHttpResult &result) {
		m_history_ready[username] = true;
		if (!result.success || !result.data.isObject())
			return;
		std::vector<Json::Value> page;
		const Json::Value &arr = result.data["messages"];
		if (arr.isArray())
			for (const auto &m : arr)
				page.push_back(m);
		// server returns newest-first; normalize to oldest-first
		std::reverse(page.begin(), page.end());
		auto &deque = m_messages[username];
		if (before_id > 0) {
			// older page: prepend while keeping order
			for (size_t i = 0; i < page.size(); i++)
				deque.insert(deque.begin() + i, page[i]);
		} else {
			for (const auto &m : page)
				deque.push_back(m);
		}
		m_has_more[username] = result.data["hasMore"].asBool();
	});
}

Json::Value CloudService::messagesJson(const std::string &username) const
{
	Json::Value v(Json::objectValue);
	Json::Value arr(Json::arrayValue);
	auto it = m_messages.find(username);
	if (it != m_messages.end())
		for (const auto &m : it->second)
			arr.append(m);
	v["messages"] = arr;
	auto hm = m_has_more.find(username);
	v["hasMore"] = hm != m_has_more.end() && hm->second;
	auto rdy = m_history_ready.find(username);
	v["ready"] = rdy != m_history_ready.end() && rdy->second;
	return v;
}

void CloudService::appendMessage(const Json::Value &msg)
{
	std::string user = msg["from"].asString();
	auto &deque = m_messages[user];
	deque.push_back(msg);
	while (deque.size() > 200)
		deque.pop_front();
	m_history_ready[user] = true;
}

void CloudService::sendDM(const std::string &username, const std::string &body,
		const std::string &client_id)
{
	if (body.empty() || body.size() > 2000 || username.empty())
		return;

	std::string cid = client_id.empty() ? makeClientId() : client_id;

	// prefer the realtime channel
	if (m_realtime.wsConnected()) {
		Json::Value msg(Json::objectValue);
		msg["type"] = "dm.send";
		msg["to"] = username;
		msg["body"] = body;
		msg["clientId"] = cid;
		if (m_realtime.send(msg))
			return;
	}

	// REST fallback (or WS down): message object comes back in the response
	Json::Value body_json(Json::objectValue);
	body_json["body"] = body;
	m_http.post("/api/cloud/client/messages/" + username + "/", body_json,
			true, [this, username, cid](const CloudHttpResult &result) {
		if (result.success && result.data.isObject()) {
			Json::Value ack(Json::objectValue);
			ack["type"] = "dm.ack";
			ack["clientId"] = cid;
			ack["to"] = username;
			ack["ts"] = result.data["createdAt"];
			pushEvent("dm.ack", ack);
		} else {
			Json::Value err(Json::objectValue);
			err["code"] = static_cast<Json::Int64>(result.code);
			err["message"] = result.error;
			err["refClientId"] = cid;
			pushEvent("error", err);
		}
	});
}

void CloudService::markRead(const std::string &username)
{
	m_unread[username] = 0;
	recomputeUnreadTotal();
	if (m_realtime.wsConnected()) {
		Json::Value msg(Json::objectValue);
		msg["type"] = "dm.read";
		msg["from"] = username;
		m_realtime.send(msg);
	}
	m_http.post("/api/cloud/client/messages/" + username + "/read/",
			Json::Value(), true, nullptr);
}

void CloudService::setOpenChat(const std::string &username)
{
	m_open_chat = username;
}

void CloudService::recomputeUnreadTotal()
{
	int total = 0;
	for (const auto &[user, count] : m_unread)
		total += count;
	m_unread_total = total;
}

// ---------------------------------------------------------------------------
// party
// ---------------------------------------------------------------------------

void CloudService::partyCreate(const std::string &server_address)
{
	Json::Value body(Json::objectValue);
	body["serverAddress"] = server_address.empty() ?
			Json::Value() : Json::Value(server_address);
	m_http.post("/api/cloud/client/party/", body, true,
			[this](const CloudHttpResult &result) {
		if (result.success && result.data.isObject())
			m_party = result.data;
		else
			pushEvent("party.error", result.error);
	});
}

void CloudService::partyJoin(const std::string &code)
{
	Json::Value body(Json::objectValue);
	body["code"] = code;
	m_http.post("/api/cloud/client/party/join/", body, true,
			[this](const CloudHttpResult &result) {
		if (result.success && result.data.isObject())
			m_party = result.data;
		else
			pushEvent("party.error", result.error);
	});
}

void CloudService::partyLeave()
{
	m_http.post("/api/cloud/client/party/leave/", Json::Value(), true,
			[this](const CloudHttpResult &result) {
		if (result.success)
			m_party = Json::Value(Json::nullValue);
		else
			pushEvent("party.error", result.error);
	});
}

void CloudService::partyEnd()
{
	m_http.post("/api/cloud/client/party/end/", Json::Value(), true,
			[this](const CloudHttpResult &result) {
		if (result.success)
			m_party = Json::Value(Json::nullValue);
		else
			pushEvent("party.error", result.error);
	});
}

void CloudService::partySetServer(const std::string &address)
{
	Json::Value body(Json::objectValue);
	body["address"] = address;
	m_http.post("/api/cloud/client/party/server/", body, true,
			[this](const CloudHttpResult &result) {
		if (result.success && result.data.isObject())
			m_party = result.data;
		else
			pushEvent("party.error", result.error);
	});
}

void CloudService::partyKick(const std::string &username)
{
	Json::Value body(Json::objectValue);
	body["username"] = username;
	m_http.post("/api/cloud/client/party/kick/", body, true,
			[this](const CloudHttpResult &result) {
		if (!result.success)
			pushEvent("party.error", result.error);
	});
}

// ---------------------------------------------------------------------------
// hosting / joining / events
// ---------------------------------------------------------------------------

void CloudService::startHosting(u16 server_port)
{
	m_host.startHosting(server_port);
}

void CloudService::pushEvent(const std::string &type, const Json::Value &data)
{
	Event ev;
	ev.type = type;
	ev.data = data;
	m_events.push_back(std::move(ev));
}

bool CloudService::popEvent(Event &out)
{
	try {
		out = m_events.pop_front(0);
		return true;
	} catch (ItemNotFoundException &) {
		return false;
	}
}

void CloudService::setPresenceAddress(const std::string &address)
{
	m_presence_address = address;
	m_realtime.setPresenceAddress(address);
}

void CloudService::reloadAuth()
{
	CloudConfig::get().reloadAuth();
	if (CloudConfig::get().isPaired()) {
		refreshFriends();
		refreshUnread();
	}
}

Json::Value CloudService::statusJson() const
{
	AuthInfo info = CloudConfig::get().auth();
	Json::Value v(Json::objectValue);
	v["paired"] = info.valid();
	v["username"] = info.siteUsername;
	v["displayName"] = info.displayName.empty() ? info.siteUsername
			: info.displayName;
	v["deviceName"] = info.deviceName;
	v["defaultServerUsername"] = info.defaultServerUsername;
	v["ws"] = m_realtime.wsConnected();
	v["channel"] = m_realtime.channelUp();
	return v;
}

void CloudService::autoRefreshTick(u64 now_ms)
{
	if (!CloudConfig::get().isPaired())
		return;
	if (now_ms - m_last_friends_refresh >= AUTO_REFRESH_INTERVAL_MS)
		refreshFriendsFromRest();
	if (now_ms - m_last_unread_refresh >= AUTO_REFRESH_INTERVAL_MS)
		refreshUnread();
}

} // namespace cloud
