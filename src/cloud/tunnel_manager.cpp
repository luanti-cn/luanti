// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "tunnel_manager.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "cloud_config.h"
#include "log.h"
#include "network/networkexceptions.h"
#include "porting.h"
#include "stun_client.h"

namespace cloud
{

namespace
{

constexpr char TUNNEL_MAGIC[4] = {'L', 'C', 'T', 'N'};
constexpr int PUNCH_INTERVAL_MS = 500;
constexpr int PUNCH_TIMEOUT_MS = 12'000;   // per round
constexpr int MAX_PUNCH_ROUNDS = 2;
constexpr int KEEPALIVE_INTERVAL_MS = 10'000;
constexpr int PEER_DEAD_MS = 15'000;
constexpr int RELAY_REGISTER_INTERVAL_MS = 25'000;
constexpr int REPUNCH_GRACE_MS = 8'000;
constexpr size_t MAX_PACKET = 64 * 1024;

std::string addrKey(const Address &a)
{
	std::ostringstream os;
	a.print(os);
	return os.str();
}

Address parseCandidate(const Candidate &c)
{
	Address addr;
	if (c.host.empty())
		return addr;
	try {
		addr.Resolve(c.host.c_str());
		addr.setPort(c.port);
	} catch (ResolveError &) {
		return Address();
	}
	return addr;
}

/// Local LAN IPv4 via the UDP-connect trick (no packets are sent)
u32 getBestLocalIPv4()
{
	int s = (int)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
		return (u32)0x7F000001;
	sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(53);
	dest.sin_addr.s_addr = htonl(0x08080808);
	u32 out = (u32)0x7F000001;
	if (::connect(s, (sockaddr *)&dest, sizeof(dest)) == 0) {
		sockaddr_in addr;
		socklen_t len = sizeof(addr);
		memset(&addr, 0, sizeof(addr));
		if (::getsockname(s, (sockaddr *)&addr, &len) == 0 &&
				addr.sin_family == AF_INET)
			out = ntohl(addr.sin_addr.s_addr);
	}
#ifdef _WIN32
	closesocket(s);
#else
	::close(s);
#endif
	return out;
}

std::string ipToString(u32 host_order_ip)
{
	return std::to_string((host_order_ip >> 24) & 0xFF) + "." +
			std::to_string((host_order_ip >> 16) & 0xFF) + "." +
			std::to_string((host_order_ip >> 8) & 0xFF) + "." +
			std::to_string(host_order_ip & 0xFF);
}

} // anonymous namespace

TunnelManager::~TunnelManager()
{
	shutdown();
}

bool TunnelManager::isTunnelPacket(const u8 *data, int size)
{
	return size >= 5 && memcmp(data, TUNNEL_MAGIC, 4) == 0;
}

std::string TunnelManager::addrKey(const Address &a)
{
	return ::cloud::addrKey(a);
}

void TunnelManager::setState(TunnelStatus::State state, const char *error)
{
	std::lock_guard<std::mutex> lock(m_status_mutex);
	m_status.state = state;
	if (error)
		m_status.error = error;
	if (state == TunnelStatus::CONNECTED)
		m_status.error.clear();
	m_status.peer = m_peer.isValid() ? addrKey(m_peer) : std::string();
}

bool TunnelManager::statusIsPunching()
{
	std::lock_guard<std::mutex> lock(m_status_mutex);
	return m_status.state == TunnelStatus::PUNCHING;
}

TunnelStatus TunnelManager::status() const
{
	std::lock_guard<std::mutex> lock(m_status_mutex);
	TunnelStatus s = m_status;
	return s;
}

void TunnelManager::deliverSignal(const std::string &from,
		const Json::Value &payload)
{
	std::lock_guard<std::mutex> lock(m_signal_mutex);
	m_signals.push_back({from, payload});
}

void TunnelManager::addRemoteCandidates(const std::vector<Candidate> &candidates)
{
	std::lock_guard<std::mutex> lock(m_signal_mutex);
	for (const auto &c : candidates)
		m_candidates.push_back(c);
}

bool TunnelManager::startHost(const HostParams &params)
{
	if (isRunning())
		return false;
	m_host = params;
	m_is_host = true;
	m_peer = Address();
	m_relay = Address();
	m_punch_targets.clear();
	m_candidates.clear();
	m_self_endpoints.clear();
	m_server_sockets.clear();
	m_peer_by_key.clear();
	m_peer_last_seen.clear();

	if (!params.relay_host.empty() && params.relay_port) {
		try {
			m_relay.Resolve(params.relay_host.c_str());
			m_relay.setPort(params.relay_port);
		} catch (ResolveError &) {
			m_relay = Address();
		}
	}

	m_tunnel.Close();
	m_tunnel = UDPSocket::CreateEphemeral(false);
	m_tunnel.setTimeoutMs(50);

	{
		std::lock_guard<std::mutex> lock(m_status_mutex);
		m_status = TunnelStatus();
		m_status.state = TunnelStatus::PUNCHING;
		m_status.via_relay = false;
	}

	m_last_keepalive = 0;
	m_last_punch = 0;
	m_last_relay_reg = 0;
	m_punch_rounds = 0;
	m_have_stun = false;

	start();
	return true;
}

bool TunnelManager::startGuest(const GuestParams &params)
{
	if (isRunning())
		return false;
	m_guest = params;
	m_is_host = false;
	m_peer = Address();
	m_relay = Address();
	m_engine_endpoint = Address();
	m_punch_targets.clear();
	m_self_endpoints.clear();
	m_candidates.clear();
	m_early_queue.clear();

	if (!params.relay_host.empty() && params.relay_port) {
		try {
			m_relay.Resolve(params.relay_host.c_str());
			m_relay.setPort(params.relay_port);
		} catch (ResolveError &) {
			m_relay = Address();
		}
	}
	for (const auto &c : params.candidates)
		m_candidates.push_back(c);

	m_tunnel.Close();
	m_tunnel = UDPSocket::CreateEphemeral(false);
	m_tunnel.setTimeoutMs(50);
	m_loopback.Close();
	m_loopback = UDPSocket::Create(Address(0x7F000001u, 0)); // 127.0.0.1:0
	m_loopback.setTimeoutMs(50);

	warningstream << "[Tunnel] guest start: candidates="
			<< m_candidates.size() << " relay="
			<< (m_relay.isValid() ? params.relay_host + ":" +
					std::to_string(params.relay_port) : "none")
			<< std::endl;

	{
		std::lock_guard<std::mutex> lock(m_status_mutex);
		m_status = TunnelStatus();
		m_status.state = TunnelStatus::PUNCHING;
		Address local = m_loopback.GetLocalAddress();
		m_status.loopback_port = local.isValid() ? local.getPort() : 0;
	}
	warningstream << "[Tunnel] guest loopback port: "
			<< m_status.loopback_port << std::endl;

	m_last_keepalive = 0;
	m_last_punch = 0;
	m_last_relay_reg = 0;
	m_punch_rounds = 0;
	m_have_stun = false;

	start();
	return true;
}

void TunnelManager::shutdown()
{
	if (isRunning()) {
		stop();
		wait();
	}
	m_tunnel.Close();
	m_loopback.Close();
	m_server_sockets.clear();
	m_peer_by_key.clear();
	{
		std::lock_guard<std::mutex> lock(m_status_mutex);
		m_status.state = TunnelStatus::IDLE;
		m_status.via_relay = false;
		m_status.loopback_port = 0;
		m_status.peer.clear();
	}
}

void *TunnelManager::run()
{
	if (m_is_host)
		hostLoop();
	else
		guestLoop();
	return nullptr;
}

// ---------------------------------------------------------------------------
// HOST
// ---------------------------------------------------------------------------

void TunnelManager::hostRegisterRelay()
{
	if (!m_relay.isValid() || m_host.relay_ticket.empty())
		return;
	std::string reg = "LRCN1|" + m_host.room_id + "|" + m_host.relay_ticket;
	try {
		m_tunnel.Send(m_relay, reg.data(), (int)reg.size());
		warningstream << "[Tunnel] host: relay registration sent to "
				<< addrKey(m_relay) << std::endl;
	} catch (SendFailedException &) {
	}
}

void TunnelManager::hostPunchTick(u64 now)
{
	u8 probe[13];
	memcpy(probe, TUNNEL_MAGIC, 4);
	probe[4] = 'P';
	static thread_local u8 seq = 0;
	probe[5] = seq++;
	memset(probe + 6, 0, 7);

	for (const auto &target : m_punch_targets) {
		if (!target.isValid())
			continue;
		try {
			m_tunnel.Send(target, probe, sizeof(probe));
		} catch (SendFailedException &) {
		}
	}

	// keep established paths (incl. relay) alive so the guest's
	// dead-peer detection doesn't fire when there is no game traffic yet
	if (now - m_last_keepalive >= KEEPALIVE_INTERVAL_MS) {
		u8 ka[5];
		memcpy(ka, TUNNEL_MAGIC, 4);
		ka[4] = 'K';
		for (const auto &[key, peer] : m_peer_by_key) {
			(void)key;
			if (!peer.isValid())
				continue;
			try {
				m_tunnel.Send(peer, ka, sizeof(ka));
			} catch (SendFailedException &) {
			}
		}
		m_last_keepalive = now;
	}
}

void TunnelManager::hostHandleTunnelPacket(const Address &sender,
		const u8 *data, int size)
{
	if (size < 5)
		return;
	char type = (char)data[4];
	switch (type) {
	case 'P': // punch probe from guest: ack + remember endpoint
	{
		bool known = false;
		for (const auto &t : m_punch_targets)
			if (t == sender)
				known = true;
		if (!known) {
			warningstream << "[Tunnel] host: punch probe from "
					<< addrKey(sender) << std::endl;
			m_punch_targets.push_back(sender);
		}
		u8 ack[5];
		memcpy(ack, TUNNEL_MAGIC, 4);
		ack[4] = 'A';
		try {
			m_tunnel.Send(sender, ack, sizeof(ack));
		} catch (SendFailedException &) {
		}
		return;
	}
	case 'K':
		m_peer_last_seen[addrKey(sender)] = porting::getTimeMs();
		return;
	case 'A':
	{
		bool known = false;
		for (const auto &t : m_punch_targets)
			if (t == sender)
				known = true;
		if (!known)
			m_punch_targets.push_back(sender);
		return;
	}
	case 'D':
		break;
	default:
		return;
	}

	// ---- data from a guest: route through a per-peer server socket ----
	std::string key = addrKey(sender);
	auto it = m_server_sockets.find(key);
	if (it == m_server_sockets.end()) {
		try {
			UDPSocket sock = UDPSocket::CreateEphemeral(false);
			sock.setTimeoutMs(50);
			m_server_sockets.emplace(key, std::move(sock));
			m_peer_by_key[key] = sender;
		} catch (SocketException &e) {
			errorstream << "Cloud tunnel: cannot create forward socket: "
					<< e.what() << std::endl;
			return;
		}
		it = m_server_sockets.find(key);
	}
	m_peer_last_seen[key] = porting::getTimeMs();
	m_peer = sender;

	Address server_addr(0x7F000001u, m_host.local_server_port);
	try {
		it->second.Send(server_addr, data + 5, size - 5);
	} catch (SendFailedException &) {
	}
}

void TunnelManager::hostLoop()
{
	u64 now = porting::getTimeMs();
	u64 last_stun = 0;
	m_punch_deadline = now + PUNCH_TIMEOUT_MS;

	auto collectCandidates = [this, &last_stun]() {
		if (!m_have_stun) {
			StunClient::MappingResult stun = StunClient::queryAny(m_tunnel,
					CloudConfig::get().stunServers(), 2500);
			m_self_endpoints.clear();
			if (stun.success) {
				m_self_endpoints.push_back(stun.mapped);
				m_have_stun = true;
			}
			u32 local_ip = getBestLocalIPv4();
			m_self_endpoints.emplace_back(local_ip,
					m_tunnel.GetLocalAddress().getPort());
		}
		// build the candidate list published to guests
		std::vector<Candidate> list;
		for (size_t i = 0; i < m_self_endpoints.size(); i++) {
			const Address &ep = m_self_endpoints[i];
			Candidate c;
			c.type = (i == 0 && m_have_stun) ? "stun" : "local";
			c.host = ipToString(ntohl(ep.getAddress().s_addr));
			c.port = ep.getPort();
			list.push_back(c);
		}
		if (m_host.on_candidates)
			m_host.on_candidates(std::move(list));
		last_stun = porting::getTimeMs();
	};

	collectCandidates();
	hostRegisterRelay();
	m_last_relay_reg = porting::getTimeMs();

	while (!stopRequested()) {
		now = porting::getTimeMs();

		// consume incoming signals (guest endpoint reports)
		while (true) {
			Signal sig;
			{
				std::lock_guard<std::mutex> lock(m_signal_mutex);
				if (m_signals.empty())
					break;
				sig = std::move(m_signals.front());
				m_signals.pop_front();
			}
			handleSignal(sig.from, sig.payload);
		}

		// refresh STUN periodically (NAT mappings may rotate)
		if (m_have_stun && now - last_stun >= 60'000) {
			m_have_stun = false;
			collectCandidates();
		}

		// relay registration / keepalive
		if (m_relay.isValid() &&
				now - m_last_relay_reg >= RELAY_REGISTER_INTERVAL_MS) {
			hostRegisterRelay();
			m_last_relay_reg = now;
		}

		// punching doubles as keepalive toward known guests
		if (now - m_last_punch >= PUNCH_INTERVAL_MS) {
			hostPunchTick(now);
			m_last_punch = now;
		}

		// ---- tunnel socket: guest traffic ----
		if (m_tunnel.WaitData(20)) {
			u8 buf[MAX_PACKET];
			Address sender;
			int n = m_tunnel.Receive(sender, buf, sizeof(buf));
			if (n > 0)
				hostHandleTunnelPacket(sender, buf, n);
		}

		// ---- per-peer server sockets: game server replies ----
		for (auto &[key, sock] : m_server_sockets) {
			while (sock.WaitData(0)) {
				u8 buf[MAX_PACKET];
				Address from;
				int n = sock.Receive(from, buf, sizeof(buf));
				if (n <= 0)
					break;
				auto itp = m_peer_by_key.find(key);
				if (itp == m_peer_by_key.end())
					break;
				u8 out[MAX_PACKET + 5];
				memcpy(out, TUNNEL_MAGIC, 4);
				out[4] = 'D';
				memcpy(out + 5, buf, n);
				try {
					m_tunnel.Send(itp->second, out, n + 5);
				} catch (SendFailedException &) {
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// GUEST
// ---------------------------------------------------------------------------

void TunnelManager::guestSendTo(const Address &to, char type,
		const u8 *payload, int size)
{
	if (!to.isValid())
		return;
	u8 out[MAX_PACKET + 5];
	memcpy(out, TUNNEL_MAGIC, 4);
	out[4] = (u8)type;
	if (size > 0 && payload)
		memcpy(out + 5, payload, size);
	try {
		m_tunnel.Send(to, out, size + 5);
	} catch (SendFailedException &) {
	}
}

void TunnelManager::guestSendData(const u8 *payload, int size)
{
	if (size > (int)MAX_PACKET)
		return;
	if (m_peer.isValid()) {
		guestSendTo(m_peer, 'D', payload, size);
		m_last_keepalive = porting::getTimeMs();
	} else if (m_early_queue.size() < 128) {
		// not connected yet: buffer early engine traffic
		m_early_queue.emplace_back(payload, payload + size);
	}
}

void TunnelManager::guestFlushEarlyQueue()
{
	if (m_early_queue.empty() || !m_peer.isValid())
		return;
	for (const auto &pkt : m_early_queue)
		guestSendTo(m_peer, 'D', pkt.data(), (int)pkt.size());
	m_early_queue.clear();
}

void TunnelManager::guestHandleTunnelPacket(const Address &sender,
		const u8 *data, int size)
{
	if (size < 5)
		return;
	char type = (char)data[4];
	switch (type) {
	case 'P': // host punching at us: accept as peer
	case 'A':
		if (statusIsPunching()) {
			m_peer = sender;
			m_last_peer_traffic = porting::getTimeMs();
			m_last_keepalive = m_last_peer_traffic;
			setState(TunnelStatus::CONNECTED);
			warningstream << "[Tunnel] guest: CONNECTED via P2P, peer="
					<< addrKey(sender) << std::endl;
			guestFlushEarlyQueue();
			// reply so the host also locks in the path
			guestSendTo(sender, 'A', data + 5, size - 5);
		} else if (sender == m_peer) {
			m_last_peer_traffic = porting::getTimeMs();
		}
		return;
	case 'K':
		m_last_peer_traffic = porting::getTimeMs();
		return;
	case 'D':
		break;
	default:
		return;
	}

	m_last_peer_traffic = porting::getTimeMs();
	if (m_engine_endpoint.isValid()) {
		try {
			m_loopback.Send(m_engine_endpoint, data + 5, size - 5);
		} catch (SendFailedException &) {
		}
	}
}

void TunnelManager::guestPunchTick()
{
	u8 probe[13];
	memcpy(probe, TUNNEL_MAGIC, 4);
	probe[4] = 'P';
	static thread_local u8 seq = 0;
	probe[5] = seq++;
	memset(probe + 6, 0, 7);

	std::vector<Candidate> candidates;
	{
		std::lock_guard<std::mutex> lock(m_signal_mutex);
		candidates = m_candidates;
	}
	for (const auto &c : candidates) {
		Address target = parseCandidate(c);
		if (target.isValid())
			guestSendTo(target, 'P', probe + 5, 8);
	}
}

void TunnelManager::guestLoop()
{
	u64 now = porting::getTimeMs();
	m_punch_deadline = now + PUNCH_TIMEOUT_MS;

	// observe our public endpoint(s) and tell the host via WS signaling
	StunClient::MappingResult stun = StunClient::queryAny(m_tunnel,
			CloudConfig::get().stunServers(), 2500);
	m_self_endpoints.clear();
	if (stun.success)
		m_self_endpoints.push_back(stun.mapped);
	u32 local_ip = getBestLocalIPv4();
	m_self_endpoints.emplace_back(local_ip,
			m_tunnel.GetLocalAddress().getPort());

	if (m_guest.signal_sender) {
		Json::Value payload(Json::objectValue);
		payload["type"] = "guest_endpoints";
		Json::Value eps(Json::arrayValue);
		for (const auto &ep : m_self_endpoints) {
			Json::Value e(Json::objectValue);
			e["host"] = ipToString(ntohl(ep.getAddress().s_addr));
			e["port"] = ep.getPort();
			eps.append(e);
		}
		payload["endpoints"] = eps;
		m_guest.signal_sender(payload);
	}

	while (!stopRequested()) {
		now = porting::getTimeMs();

		bool connected;
		{
			std::lock_guard<std::mutex> lock(m_status_mutex);
			connected = m_status.state == TunnelStatus::CONNECTED;
		}

		if (!connected) {
			// punch phase
			if (now - m_last_punch >= PUNCH_INTERVAL_MS) {
				guestPunchTick();
				m_last_punch = now;
			}
			if (now >= m_punch_deadline) {
				if (m_relay.isValid()) {
					warningstream << "[Tunnel] guest: punch failed, "
							"switching to relay" << std::endl;
					m_peer = m_relay;
					m_last_peer_traffic = now;
					m_last_keepalive = now;
					{
						std::lock_guard<std::mutex> lock(m_status_mutex);
						m_status.state = TunnelStatus::CONNECTED;
						m_status.via_relay = true;
						m_status.peer = addrKey(m_peer);
					}
					guestFlushEarlyQueue();
				} else if (++m_punch_rounds <= MAX_PUNCH_ROUNDS) {
					m_punch_deadline = now + PUNCH_TIMEOUT_MS;
				} else {
					setState(TunnelStatus::FAILED,
							"Unable to establish tunnel (symmetric NAT and no relay available)");
					return;
				}
			}
		} else {
			// keepalive + dead detection
			if (now - m_last_keepalive >= KEEPALIVE_INTERVAL_MS) {
				if (m_peer.isValid())
					guestSendTo(m_peer, 'K', nullptr, 0);
				m_last_keepalive = now;
			}
			if (now - m_last_peer_traffic > PEER_DEAD_MS) {
				infostream << "Cloud tunnel: peer went silent, re-punching"
						<< std::endl;
				m_peer = Address();
				m_last_peer_traffic = now;
				m_punch_deadline = now + REPUNCH_GRACE_MS;
				std::lock_guard<std::mutex> lock(m_status_mutex);
				m_status.state = TunnelStatus::PUNCHING;
				m_status.via_relay = false;
			}
		}

		// ---- tunnel socket: host traffic ----
		if (m_tunnel.WaitData(10)) {
			u8 buf[MAX_PACKET];
			Address sender;
			int n = m_tunnel.Receive(sender, buf, sizeof(buf));
			if (n > 0 && isTunnelPacket(buf, n))
				guestHandleTunnelPacket(sender, buf, n);
		}

		// ---- loopback socket: engine <-> tunnel ----
		if (m_loopback.WaitData(0)) {
			u8 buf[MAX_PACKET];
			Address from;
			int n = m_loopback.Receive(from, buf, sizeof(buf));
			if (n > 0) {
				m_engine_endpoint = from;
				guestSendData(buf, n);
			}
		}
	}
}

// ---- signaling ----

void TunnelManager::handleSignal(const std::string &from,
		const Json::Value &payload)
{
	// host receives guest endpoint reports
	if (m_is_host && payload.isObject() &&
			payload["type"].asString() == "guest_endpoints") {
		const Json::Value &eps = payload["endpoints"];
		warningstream << "[Tunnel] host: got " << eps.size()
				<< " guest endpoint(s) via signaling" << std::endl;
		if (eps.isArray()) {
			for (const auto &e : eps) {
				Candidate c;
				c.type = "stun";
				c.host = e["host"].asString();
				c.port = (u16)e["port"].asUInt();
				Address a = parseCandidate(c);
				if (!a.isValid())
					continue;
				bool known = false;
				for (const auto &t : m_punch_targets)
					if (t == a)
						known = true;
				if (!known)
					m_punch_targets.push_back(a);
			}
		}
		(void)from;
	}
}

} // namespace cloud
