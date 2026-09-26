// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <json/json.h>

#include "irrlichttypes.h"
#include "network/address.h"
#include "network/socket.h"
#include "threading/thread.h"

namespace cloud
{

struct Candidate
{
	std::string type; // "stun" | "local" | "relay"
	std::string host;
	u16 port = 0;

	Json::Value toJson() const
	{
		Json::Value v(Json::objectValue);
		v["type"] = type;
		v["host"] = host;
		v["port"] = port;
		return v;
	}
};

struct TunnelStatus
{
	enum State
	{
		IDLE,
		PUNCHING,
		CONNECTED,
		FAILED,
	} state = IDLE;
	bool via_relay = false;
	u16 loopback_port = 0;   // guest: local UDP proxy port
	std::string peer;        // "ip:port" of the current/last peer
	std::string error;
};

/// UDP tunnel between two players:
/// - HOST mode: forwards tunnel traffic <-> local Luanti server (127.0.0.1:P)
/// - GUEST mode: exposes 127.0.0.1:<ephemeral> and forwards to the host over a
///   hole-punched P2P path, falling back to the UDP relay transparently.
///
/// Runs on its own thread; status()/deliverSignal() are thread-safe.
class TunnelManager : public Thread
{
public:
	struct HostParams
	{
		u16 local_server_port = 0;
		std::string room_id;
		std::string relay_ticket;  // empty => no relay available
		std::string relay_host;    // resolved once at start
		u16 relay_port = 0;
		std::function<void(std::vector<Candidate> candidates)> on_candidates;
	};

	struct GuestParams
	{
		std::vector<Candidate> candidates; // stun/local candidates of the host
		std::string relay_host;
		u16 relay_port = 0;
		std::string peer_username;         // host site username (signal target)
		/// called on the tunnel thread when observed endpoints must be
		/// reported to the host (payload: {"type":"guest_endpoints",...})
		std::function<void(const Json::Value &payload)> signal_sender;
	};

	TunnelManager() : Thread("CloudTunnel") {}
	~TunnelManager() override;

	bool startHost(const HostParams &params);
	bool startGuest(const GuestParams &params);
	/// request stop and join the thread
	void shutdown();

	TunnelStatus status() const;

	/// room.signal payload from the host (guest only)
	void deliverSignal(const std::string &from, const Json::Value &payload);
	/// add remote candidates discovered after the tunnel started (guest)
	void addRemoteCandidates(const std::vector<Candidate> &candidates);

private:
	void *run() override;
	void hostLoop();
	void guestLoop();

	// ---- shared helpers ----
	static bool isTunnelPacket(const u8 *data, int size);
	void handleSignal(const std::string &from, const Json::Value &payload);
	void setState(TunnelStatus::State state, const char *error = nullptr);
	bool statusIsPunching();
	static std::string addrKey(const Address &a);

	// ---- guest state ----
	GuestParams m_guest;
	void guestPunchTick();
	void guestHandleTunnelPacket(const Address &sender, const u8 *data, int size);
	void guestSendTo(const Address &to, char type, const u8 *payload, int size);
	void guestSendData(const u8 *payload, int size);
	void guestFlushEarlyQueue();

	// ---- host state ----
	HostParams m_host;
	std::map<std::string, UDPSocket> m_server_sockets;
	std::map<std::string, Address> m_peer_by_key;
	std::map<std::string, u64> m_peer_last_seen;
	void hostHandleTunnelPacket(const Address &sender, const u8 *data, int size);
	void hostRegisterRelay();
	void hostPunchTick(u64 now);

	// ---- common ----
	UDPSocket m_tunnel;          // punch + data
	UDPSocket m_loopback;        // guest only
	Address m_peer;              // current data path (host candidates/relay)
	Address m_engine_endpoint;   // guest: loopback peer of the Luanti engine
	Address m_relay;             // resolved relay endpoint
	std::vector<Address> m_punch_targets;  // host: guest endpoints (from signal)
	std::vector<Address> m_self_endpoints; // stun+local endpoints of this side
	std::vector<Candidate> m_candidates;   // remote candidates (guest)
	bool m_is_host = false;

	u64 m_last_peer_traffic = 0;
	u64 m_last_keepalive = 0;
	u64 m_last_punch = 0;
	u64 m_last_relay_reg = 0;
	u64 m_punch_deadline = 0;
	int m_punch_rounds = 0;
	bool m_have_stun = false;

	std::deque<std::vector<u8>> m_early_queue; // guest loopback packets pre-connect

	mutable std::mutex m_status_mutex;
	TunnelStatus m_status;

	mutable std::mutex m_signal_mutex;
	struct Signal
	{
		std::string from;
		Json::Value payload;
	};
	std::deque<Signal> m_signals;
};

} // namespace cloud
