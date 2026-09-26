// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <json/json.h>

#include "threading/thread.h"
#include "util/container.h"

namespace cloud
{

/// Long-lived connection to the cloud realtime gateway
/// (/api/cloud/client/ws/).  Uses libcurl's WebSocket API when available and
/// transparently degrades to 30s REST polling otherwise.
///
/// All outgoing JSON frames are queued with post() (thread-safe); incoming
/// frames are retrieved with popEvent() from the service/main thread.
class RealtimeClient : public Thread
{
public:
	RealtimeClient() : Thread("CloudRealtime") {}
	~RealtimeClient() override;

	void begin(const std::string &ws_url);

	/// Queue a client->server message. Returns false if not connected
	/// (caller should fall back to REST).
	bool send(const Json::Value &msg);

	/// Pop one server->client frame. Returns false if none pending.
	bool popEvent(Json::Value &out);

	bool wsConnected() const { return m_ws_connected; }
	/// True when we have *some* live channel (WS or polling)
	bool channelUp() const { return m_ws_connected || m_poll_active; }

	void setPresenceAddress(const std::string &address);

private:
	void *run() override;

	bool wsSession();
	bool pollSession();
	bool wsSendAll(const std::string &payload);
	/// drain outgoing queue into the socket; returns false on socket error
	bool flushOutgoing();
	bool wsSendPing();

	std::string m_ws_url;
	/// CURL* while a WS session is alive (guarded by CLOUD_USE_WS build flag)
	void *m_curl_handle = nullptr;

	MutexedQueue<Json::Value> m_outgoing;
	MutexedQueue<Json::Value> m_incoming;

	std::atomic<bool> m_ws_connected{false};
	std::atomic<bool> m_poll_active{false};
	std::atomic<bool> m_ws_permanently_failed{false};

	std::mutex m_presence_mutex;
	std::string m_presence_address;
	std::atomic<bool> m_presence_dirty{false};
};

} // namespace cloud
