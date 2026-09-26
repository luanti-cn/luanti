// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "stun_client.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>

#include "log.h"
#include "network/networkexceptions.h"
#include "porting.h"
#include "util/string.h"

namespace cloud
{

namespace
{

constexpr u32 STUN_MAGIC_COOKIE = 0x2112A44D;

void putU16(u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)(v);
}

void putU32(u8 *p, u32 v)
{
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)(v);
}

u16 getU16(const u8 *p)
{
	return (u16)((p[0] << 8) | p[1]);
}

u32 getU32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

void randomBytes(u8 *out, size_t n)
{
	static thread_local std::mt19937 rng((u32)(time(nullptr) & 0xFFFFFFFF));
	for (size_t i = 0; i < n; i++)
		out[i] = (u8)(rng() & 0xFF);
}

bool parseMappedAddress(const u8 *attr, size_t len, bool xor_mapped,
		const u8 *txid, Address &out)
{
	// attr: reserved(1) family(1) port(2) address(4|16)
	if (len < 8)
		return false;
	u8 family = attr[1];
	u16 port = getU16(attr + 2);
	if (xor_mapped)
		port ^= (u16)(STUN_MAGIC_COOKIE >> 16);
	if (family == 0x01 && len >= 8) {
		u32 ip = getU32(attr + 4);
		if (xor_mapped)
			ip ^= STUN_MAGIC_COOKIE;
		out.setAddress(ip);
		out.setPort(port);
		return true;
	}
	(void)txid;
	return false; // IPv6 candidates are not collected for now
}

} // anonymous namespace

StunClient::MappingResult StunClient::query(UDPSocket &socket,
		const std::string &server, u16 port, int timeout_ms)
{
	MappingResult result;

	Address stun_addr;
	try {
		stun_addr.Resolve(server.c_str());
	} catch (ResolveError &e) {
		warningstream << "Cloud STUN: cannot resolve \"" << server
				<< "\": " << e.what() << std::endl;
		return result;
	}
	stun_addr.setPort(port);

	// Build Binding Request
	u8 req[20];
	putU16(req, 0x0001); // Binding Request
	putU16(req + 2, 0);  // message length
	putU32(req + 4, 0x2112A44D); // magic cookie
	randomBytes(req + 8, 12);

	try {
		socket.Send(stun_addr, req, sizeof(req));
	} catch (SendFailedException &) {
		return result;
	}

	u64 deadline = porting::getTimeMs() + (u64)timeout_ms;
	u8 buf[1024];
	while (porting::getTimeMs() < deadline) {
		int remaining = (int)(deadline - porting::getTimeMs());
		if (!socket.WaitData(remaining > 100 ? 100 : remaining))
			continue;
		Address sender;
		int n = socket.Receive(sender, buf, sizeof(buf));
		if (n < 20)
			continue;
		u16 type = getU16(buf);
		if (type != 0x0101) // Binding success response
			continue;
		if (memcmp(buf + 8, req + 8, 12) != 0)
			continue; // different transaction
		u16 msg_len = getU16(buf + 2);
		size_t avail = std::min((size_t)n - 20u, (size_t)msg_len);
		size_t off = 20;
		while (off + 4 <= avail) {
			u16 attr_type = getU16(buf + off);
			u16 attr_len = getU16(buf + off + 2);
			if (off + 4 + attr_len > avail)
				break;
			if (attr_type == 0x0020) { // XOR-MAPPED-ADDRESS
				if (parseMappedAddress(buf + off + 4, attr_len, true,
						req + 8, result.mapped)) {
					result.success = true;
					return result;
				}
			} else if (attr_type == 0x0001) { // MAPPED-ADDRESS
				Address tmp;
				if (parseMappedAddress(buf + off + 4, attr_len, false,
						req + 8, tmp)) {
					result.mapped = tmp;
					result.success = true;
				}
			}
			off += 4 + attr_len + ((4 - (attr_len & 3)) & 3);
		}
		if (result.success)
			return result;
	}
	return result;
}

StunClient::MappingResult StunClient::queryAny(UDPSocket &socket,
		const std::vector<std::string> &servers, int timeout_ms)
{
	MappingResult result;
	for (const auto &entry : servers) {
		auto parts = str_split(entry, ':');
		if (parts.empty())
			continue;
		std::string host = parts[0];
		u16 port = 3478;
		if (parts.size() >= 2) {
			int p = atoi(parts[1].c_str());
			if (p > 0 && p <= 65535)
				port = (u16)p;
		}
		result = query(socket, host, port, timeout_ms);
		if (result.success)
			return result;
	}
	return result;
}

} // namespace cloud
