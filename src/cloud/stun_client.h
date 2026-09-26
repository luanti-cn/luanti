// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <string>
#include <vector>

#include "network/address.h"
#include "network/socket.h"

namespace cloud
{

/// Minimal STUN client (RFC 5389, Binding requests only) used to observe the
/// NAT-mapped public endpoint of a UDP socket.
class StunClient
{
public:
	struct MappingResult
	{
		Address mapped;        // public ip:port as observed by the STUN server
		bool success = false;
	};

	/// Send a Binding request from `socket` (which must already be bound) to
	/// `server:port` and parse XOR-MAPPED-ADDRESS / MAPPED-ADDRESS.
	/// Blocks up to timeout_ms; only meant to be called from worker threads.
	static MappingResult query(UDPSocket &socket, const std::string &server,
			u16 port, int timeout_ms = 3000);

	/// Try every server from `servers` ("host:port" strings) until one answers.
	static MappingResult queryAny(UDPSocket &socket,
			const std::vector<std::string> &servers, int timeout_ms = 3000);
};

} // namespace cloud
