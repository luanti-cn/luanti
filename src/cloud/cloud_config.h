// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "irrlichttypes.h"

namespace cloud
{

/// Paired device/account info, persisted to <user_path>/client/cloud/auth.json
/// (same file as the main menu Lua cloud module).
struct AuthInfo
{
	std::string deviceToken;
	std::string siteUsername;
	std::string displayName;
	std::string deviceName;
	std::string defaultServerUsername;

	bool valid() const { return deviceToken.size() >= 8; }
};

/// Static configuration + credentials access.
class CloudConfig
{
public:
	static CloudConfig &get();

	/// Base URL of the cloud backend, e.g. "https://api.luanti.cn" (no trailing slash)
	const std::string &baseUrl() const { return m_base_url; }
	/// Browser-facing site URL (pairing guidance, account management)
	const std::string &siteUrl() const { return m_site_url; }
	/// WebSocket endpoint, e.g. "wss://api.luanti.cn/api/cloud/client/ws/"
	std::string wsUrl() const;

	/// (Re)read base/site URLs from g_settings
	void refreshSettings();

	/// (Re)read auth.json from disk (call after Lua re-pairs)
	void reloadAuth();
	/// Currently loaded credentials (may be invalid when not paired)
	AuthInfo auth() const;
	/// Persist credentials (called from C++ only; Lua writes the same file too)
	void saveAuth(const AuthInfo &info);
	void clearAuth();
	bool isPaired() const { return auth().valid(); }

	/// "host:port" list, e.g. "stun.cloudflare.com:3478"
	std::vector<std::string> stunServers() const;

	static std::string authFilePath();

private:
	CloudConfig() = default;

	std::string m_base_url;
	std::string m_site_url;
	mutable std::mutex m_auth_mutex;
	AuthInfo m_auth;
};

} // namespace cloud
