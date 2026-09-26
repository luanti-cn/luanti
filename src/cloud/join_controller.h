// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "irrlichttypes.h"
#include "tunnel_manager.h"

namespace cloud
{

class CloudHttpClient;
class RealtimeClient;

/// Joins a friend's hosted room: REST join -> WS room.join -> tunnel
/// (hole punching with relay fallback) -> local loopback endpoint the engine
/// connects to like a normal server.
class JoinController
{
public:
	enum Stage
	{
		IDLE,
		JOINING,       // REST join / WS room.join in flight
		TUNNELING,     // punching / connecting tunnel
		CONNECTED,     // loopback ready
		FAILED,
	};

	void begin(CloudHttpClient *http, RealtimeClient *rt)
	{
		m_http = http;
		m_rt = rt;
	}

	/// one of the two parameters must be non-empty
	bool prepareJoin(const std::string &username, const std::string &room_code);
	void cancel();
	bool busy() const { return m_stage != IDLE; }

	Stage stage() const { return m_stage; }
	/// 127.0.0.1:<port> to hand to the engine; empty until connected
	std::string endpoint() const;

	/// {stage, error, endpoint, hostUsername, roomCode, tunnel:{...}}
	Json::Value statusJson() const;

	/// WS event routing (from CloudService::step, main thread)
	void onRoomCandidates(const Json::Value &msg);
	void onRoomSignal(const Json::Value &msg);
	void onRoomGone();

private:
	void startTunnel();

	CloudHttpClient *m_http = nullptr;
	RealtimeClient *m_rt = nullptr;

	std::atomic<Stage> m_stage{IDLE};
	std::string m_room_id;
	std::string m_room_code;
	std::string m_host_username;
	std::string m_error;
	std::vector<Candidate> m_candidates;
	std::string m_relay_host;
	u16 m_relay_port = 0;

	mutable std::mutex m_mutex;
	TunnelManager m_tunnel;
};

} // namespace cloud
