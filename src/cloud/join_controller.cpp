// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "join_controller.h"

#include <sstream>

#include "cloud_http_client.h"
#include "log.h"
#include "porting.h"
#include "realtime_client.h"
#include "util/string.h"

namespace cloud
{

static std::vector<Candidate> parseCandidates(const Json::Value &value)
{
	// REST responses carry the candidate array *double encoded* as a JSON
	// string (candidatesJson); WS pushes carry a real array.
	std::vector<Candidate> out;
	Json::Value arr = value;
	if (arr.isString()) {
		Json::Reader reader;
		if (!reader.parse(arr.asString(), arr))
			return out;
	}
	if (!arr.isArray())
		return out;
	for (const auto &c : arr) {
		if (!c.isObject())
			continue;
		Candidate cand;
		cand.type = c["type"].asString();
		if (cand.type != "stun" && cand.type != "local")
			continue;
		cand.host = c["host"].asString();
		cand.port = (u16)c["port"].asUInt();
		if (!cand.host.empty() && cand.port)
			out.push_back(cand);
	}
	return out;
}

bool JoinController::prepareJoin(const std::string &username,
		const std::string &room_code)
{
	if (busy() || !m_http)
		return false;
	if (username.empty() && room_code.empty())
		return false;

	m_error.clear();
	m_candidates.clear();
	m_relay_host.clear();
	m_relay_port = 0;
	m_room_code = room_code;
	m_stage = JOINING;

	Json::Value body(Json::objectValue);
	if (!room_code.empty())
		body["roomCode"] = room_code;
	else
		body["username"] = username;

	m_http->post("/api/cloud/client/host/join/", body, true,
			[this](const CloudHttpResult &result) {
		if (m_stage != JOINING)
			return;
		if (!result.success || !result.data.isObject()) {
			m_error = result.error.empty() ? "加入房间失败" : result.error;
			m_stage = FAILED;
			return;
		}
		const Json::Value &data = result.data;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_room_id = data["roomId"].asString();
			m_host_username = data["hostUsername"].asString();
			m_candidates = parseCandidates(data["candidatesJson"]);
			const Json::Value &relay = data["relay"];
			if (relay.isObject()) {
				m_relay_host = relay["host"].asString();
				m_relay_port = (u16)relay["port"].asUInt();
			}
		}
		warningstream << "Cloud join: got room " << m_room_id << " (host "
				<< m_host_username << ", " << m_candidates.size()
				<< " candidates, relay " << (m_relay_port ? m_relay_host : "none")
				<< ")" << std::endl;

		// enter the room's signaling channel
		if (m_rt && m_rt->wsConnected()) {
			Json::Value msg(Json::objectValue);
			msg["type"] = "room.join";
			msg["roomId"] = m_room_id;
			m_rt->send(msg);
		}

		startTunnel();
	});
	return true;
}

void JoinController::startTunnel()
{
	TunnelManager::GuestParams params;
	params.candidates = m_candidates;
	params.relay_host = m_relay_host;
	params.relay_port = m_relay_port;
	params.peer_username = m_host_username;

	// report our observed endpoints to the host over WS signaling
	std::string room_id = m_room_id;
	std::string to = m_host_username;
	params.signal_sender = [this, room_id, to](const Json::Value &payload) {
		if (!m_rt || !m_rt->wsConnected())
			return;
		Json::Value msg(Json::objectValue);
		msg["type"] = "room.signal";
		msg["roomId"] = room_id;
		msg["to"] = to;
		msg["payload"] = payload;
		m_rt->send(msg);
	};
	m_tunnel.startGuest(std::move(params));
	m_stage = TUNNELING;
}

void JoinController::cancel()
{
	m_tunnel.shutdown();
	m_stage = IDLE;
	m_room_id.clear();
	m_error.clear();
}

std::string JoinController::endpoint() const
{
	TunnelStatus ts = m_tunnel.status();
	if (ts.state == TunnelStatus::CONNECTED && ts.loopback_port)
		return "127.0.0.1:" + std::to_string(ts.loopback_port);
	return "";
}

void JoinController::onRoomCandidates(const Json::Value &msg)
{
	if (m_stage != TUNNELING && m_stage != CONNECTED)
		return;
	if (msg["roomId"].asString() != m_room_id)
		return;
	std::vector<Candidate> candidates = parseCandidates(msg["candidates"]);
	if (!candidates.empty())
		m_tunnel.addRemoteCandidates(candidates);
}

void JoinController::onRoomSignal(const Json::Value &msg)
{
	if (msg["roomId"].asString() != m_room_id)
		return;
	m_tunnel.deliverSignal(msg["from"].asString(), msg["payload"]);
}

void JoinController::onRoomGone()
{
	// host closed the room while we were joining/tunneling
	if (m_stage == JOINING || m_stage == TUNNELING) {
		m_error = "房主已关闭房间";
		m_tunnel.shutdown();
		m_stage = FAILED;
	}
}

Json::Value JoinController::statusJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	Json::Value v(Json::objectValue);
	const char *stage_names[] = {"idle", "joining", "tunneling", "connected",
			"failed"};
	Stage eff = m_stage.load();
	// the tunnel runs on its own thread; surface its state here
	if (eff == TUNNELING &&
			m_tunnel.status().state == TunnelStatus::CONNECTED)
		eff = CONNECTED;
	v["stage"] = stage_names[eff];
	v["error"] = m_error;
	v["hostUsername"] = m_host_username;
	v["roomCode"] = m_room_code;
	v["endpoint"] = endpoint();
	TunnelStatus ts = m_tunnel.status();
	Json::Value tunnel(Json::objectValue);
	tunnel["state"] = ts.state == TunnelStatus::CONNECTED ? "connected"
			: ts.state == TunnelStatus::PUNCHING ? "punching"
			: ts.state == TunnelStatus::FAILED ? "failed" : "idle";
	tunnel["viaRelay"] = ts.via_relay;
	v["tunnel"] = tunnel;
	return v;
}

} // namespace cloud
