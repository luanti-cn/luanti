// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "host_room_controller.h"

#include "cloud_http_client.h"
#include "log.h"
#include "porting.h"
#include "realtime_client.h"

namespace cloud
{

namespace
{
constexpr u64 HEARTBEAT_INTERVAL_MS = 30'000;
}

void HostRoomController::startHosting(u16 server_port)
{
	if (m_active || server_port == 0 || !m_http)
		return;
	m_port = server_port;
	m_candidates.clear();
	m_registering = false;
	m_active = true;
	m_last_heartbeat = 0;
	registerRoom();
}

void HostRoomController::stopHosting()
{
	if (!m_active)
		return;
	m_active = false;
	m_tunnel.shutdown();

	if (m_rt && m_rt->wsConnected() && !m_room_id.empty()) {
		Json::Value msg(Json::objectValue);
		msg["type"] = "room.host";
		msg["roomId"] = m_room_id;
		msg["status"] = "closed";
		m_rt->send(msg);
	}
	if (m_http && !m_room_id.empty())
		m_http->post("/api/cloud/client/host/close/", Json::Value(),
				true, nullptr);

	std::lock_guard<std::mutex> lock(m_mutex);
	m_room_id.clear();
	m_room_code.clear();
	m_relay_ticket.clear();
	m_candidates.clear();
}

void HostRoomController::registerRoom()
{
	if (m_registering.exchange(true))
		return;
	m_last_register_attempt = porting::getTimeMs();

	Json::Value body(Json::objectValue);
	body["status"] = "open";
	body["candidates"] = Json::Value(Json::arrayValue);

	m_http->post("/api/cloud/client/host/register/", body, true,
			[this](const CloudHttpResult &result) {
		m_registering = false;
		if (!m_active)
			return;
		if (!result.success || !result.data.isObject()) {
			warningstream << "Cloud host: register failed: "
					<< result.error << std::endl;
			// retry on next step()
			m_last_heartbeat = 0;
			return;
		}
		const Json::Value &data = result.data;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_room_id = data["roomId"].asString();
			m_room_code = data["roomCode"].asString();
			const Json::Value &relay = data["relay"];
			if (relay.isObject()) {
				m_relay_host = relay["host"].asString();
				m_relay_port = (u16)relay["port"].asUInt();
				m_relay_ticket = relay["ticket"].isNull() ?
						"" : relay["ticket"].asString();
			} else {
				m_relay_host.clear();
				m_relay_port = 0;
				m_relay_ticket.clear();
			}
		}
		infostream << "Cloud host: room registered (code " << m_room_code
				<< ")" << std::endl;
		warningstream << "Cloud host: room registered (code " << m_room_code
				<< ", relay "
				<< (m_relay_port ? m_relay_host + ":" +
						std::to_string(m_relay_port) : "none") << ")"
				<< std::endl;
		startTunnel();
		m_last_heartbeat = porting::getTimeMs();
	});
}

void HostRoomController::startTunnel()
{
	std::string room_id, relay_host, relay_ticket;
	u16 relay_port = 0, port = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		room_id = m_room_id;
		relay_host = m_relay_host;
		relay_port = m_relay_port;
		relay_ticket = m_relay_ticket;
		port = m_port;
	}

	TunnelManager::HostParams params;
	params.local_server_port = port;
	params.room_id = room_id;
	params.relay_host = relay_host;
	params.relay_port = relay_port;
	params.relay_ticket = relay_ticket;
	params.on_candidates = [this](std::vector<Candidate> candidates) {
		// tunnel thread: hand over to the service (main thread)
		if (m_candidates_sink)
			m_candidates_sink(std::move(candidates));
	};
	m_tunnel.startHost(std::move(params));
}

void HostRoomController::onCandidates(std::vector<Candidate> candidates)
{
	if (!m_active)
		return;
	warningstream << "Cloud host: publishing " << candidates.size()
			<< " candidate(s)" << std::endl;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_candidates = candidates;
	}

	std::string room_id;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		room_id = m_room_id;
	}
	if (room_id.empty())
		return;

	// publish over WS (backend pushes room.candidates to room members)
	Json::Value arr(Json::arrayValue);
	for (const auto &c : candidates)
		arr.append(c.toJson());
	if (m_rt && m_rt->wsConnected()) {
		Json::Value msg(Json::objectValue);
		msg["type"] = "room.host";
		msg["roomId"] = room_id;
		msg["status"] = "open";
		msg["candidates"] = arr;
		m_rt->send(msg);
	} else {
		// no realtime channel: publish immediately via REST heartbeat,
		// otherwise joiners would see zero candidates for up to 30s
		warningstream << "Cloud host: WS down, publishing candidates "
				"via REST heartbeat" << std::endl;
		m_last_heartbeat = 0; // force sendHeartbeat on next step()
	}
}

void HostRoomController::sendHeartbeat(bool force_ws)
{
	std::string room_id;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		room_id = m_room_id;
	}
	if (room_id.empty() || !m_active)
		return;

	if (force_ws && m_rt && m_rt->wsConnected()) {
		Json::Value msg(Json::objectValue);
		msg["type"] = "room.host";
		msg["roomId"] = room_id;
		msg["status"] = "open";
		Json::Value arr(Json::arrayValue);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (const auto &c : m_candidates)
				arr.append(c.toJson());
		}
		msg["candidates"] = arr;
		m_rt->send(msg);
		return;
	}

	Json::Value body(Json::objectValue);
	body["status"] = "open";
	Json::Value arr(Json::arrayValue);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto &c : m_candidates)
			arr.append(c.toJson());
	}
	body["candidates"] = arr;

	m_http->post("/api/cloud/client/host/heartbeat/", body, true,
			[this](const CloudHttpResult &result) {
		if (result.code == 404) {
			// room expired on the backend: re-register
			m_room_id.clear();
			m_room_code.clear();
			m_last_heartbeat = 0;
		}
	});
}

void HostRoomController::onRoomSignal(const Json::Value &msg)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_active || m_room_id.empty())
		return;
	if (msg["roomId"].asString() != m_room_id)
		return;
	m_tunnel.deliverSignal(msg["from"].asString(), msg["payload"]);
}

void HostRoomController::step(u64 now_ms)
{
	if (!m_active)
		return;
	if (m_room_id.empty()) {
		// (re-)register with backoff so a dead network doesn't busy-loop
		if (!m_registering && now_ms - m_last_register_attempt >= 5000)
			registerRoom();
		return;
	}
	if (!m_room_id.empty() &&
			now_ms - m_last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
		m_last_heartbeat = now_ms;
		sendHeartbeat(false);
	}
}

Json::Value HostRoomController::statusJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Json::Value v(Json::objectValue);
	v["active"] = m_active.load();
	v["roomId"] = m_room_id;
	v["roomCode"] = m_room_code;
	v["port"] = m_port;
	TunnelStatus ts = m_tunnel.status();
	Json::Value tunnel(Json::objectValue);
	tunnel["state"] = ts.state == TunnelStatus::CONNECTED ? "connected"
			: ts.state == TunnelStatus::PUNCHING ? "punching"
			: ts.state == TunnelStatus::FAILED ? "failed" : "idle";
	tunnel["viaRelay"] = ts.via_relay;
	tunnel["error"] = ts.error;
	v["tunnel"] = tunnel;
	return v;
}

} // namespace cloud
