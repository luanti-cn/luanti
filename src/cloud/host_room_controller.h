// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <atomic>
#include <functional>
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

/// Registers a local Luanti server as a joinable cloud room and keeps it
/// alive (REST heartbeat + WS room.host + relay registration).
class HostRoomController
{
public:
	void begin(CloudHttpClient *http, RealtimeClient *rt)
	{
		m_http = http;
		m_rt = rt;
	}

	/// called once after init: candidates updates from the tunnel thread are
	/// pushed to the backend through this hook
	void setCandidatesSink(
			std::function<void(std::vector<Candidate>)> sink)
	{
		m_candidates_sink = std::move(sink);
	}

	void startHosting(u16 server_port);
	void stopHosting();
	bool active() const { return m_active; }

	/// {active, roomId, roomCode, port, tunnel:{state, viaRelay, error}}
	Json::Value statusJson() const;

	/// periodic work; call from the service step()
	void step(u64 now_ms);

	/// candidates produced by the tunnel thread
	void onCandidates(std::vector<Candidate> candidates);

	/// WS room.signal from a joining guest (endpoint reports for punching)
	void onRoomSignal(const Json::Value &msg);

private:
	void registerRoom();
	void sendHeartbeat(bool force_ws);
	void startTunnel();

	CloudHttpClient *m_http = nullptr;
	RealtimeClient *m_rt = nullptr;
	std::function<void(std::vector<Candidate>)> m_candidates_sink;

	std::atomic<bool> m_active{false};
	std::atomic<bool> m_registering{false};

	u16 m_port = 0;
	std::string m_room_id;
	std::string m_room_code;
	std::string m_relay_host;
	u16 m_relay_port = 0;
	std::string m_relay_ticket;
	std::vector<Candidate> m_candidates;

	u64 m_last_heartbeat = 0;
	u64 m_last_register_attempt = 0;

	mutable std::mutex m_mutex;
	TunnelManager m_tunnel;
};

} // namespace cloud
