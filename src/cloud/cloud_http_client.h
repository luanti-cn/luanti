// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>

#include <json/json.h>

#include "httpfetch.h"
#include "irrlichttypes.h"

namespace cloud
{

struct CloudHttpResult
{
	bool success = false;
	long code = 0;
	Json::Value data;   // parsed body (may be Null)
	std::string error;  // human readable error (network / server / HTTP status)
};

using CloudHttpCallback = std::function<void(const CloudHttpResult &)>;

/// Thin async JSON/REST client over the engine httpfetch module.
/// All calls are non-blocking; callbacks fire from step() on the caller's
/// (main/menu/game) thread.
class CloudHttpClient
{
public:
	CloudHttpClient() = default;
	~CloudHttpClient();

	CloudHttpClient(const CloudHttpClient &) = delete;
	CloudHttpClient &operator=(const CloudHttpClient &) = delete;

	void begin(const std::string &base_url);

	/// Issue an asynchronous request. path begins with "/".
	/// body_json may be empty for GET/DELETE. authenticated adds the Bearer token.
	void request(const std::string &method, const std::string &path,
			const Json::Value &body_json, bool authenticated,
			CloudHttpCallback callback, long timeout_ms = 15000);

	/// Convenience: GET
	void get(const std::string &path, bool authenticated, CloudHttpCallback cb)
	{
		request("GET", path, Json::Value(), authenticated, std::move(cb));
	}
	/// Convenience: POST with JSON body
	void post(const std::string &path, const Json::Value &body, bool authenticated,
			CloudHttpCallback cb)
	{
		request("POST", path, body, authenticated, std::move(cb));
	}

	/// Pump finished requests; invoke callbacks. Call frequently from the
	/// main/menu/game thread.
	void step();

private:
	struct Pending
	{
		CloudHttpCallback callback;
	};

	u64 m_caller = 0;
	u64 m_next_request_id = 1;
	std::string m_base_url;
	std::mutex m_pending_mutex;
	std::map<u64, Pending> m_pending;
};

} // namespace cloud
