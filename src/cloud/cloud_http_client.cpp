// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "cloud_http_client.h"

#include <json/json.h>
#include <sstream>

#include "cloud_config.h"
#include "convert_json.h"
#include "log.h"

namespace cloud
{

CloudHttpClient::~CloudHttpClient()
{
	if (m_caller)
		httpfetch_caller_free(m_caller);
}

void CloudHttpClient::begin(const std::string &base_url)
{
	m_base_url = base_url;
	if (!m_caller)
		m_caller = httpfetch_caller_alloc_secure();
}

void CloudHttpClient::request(const std::string &method, const std::string &path,
		const Json::Value &body_json, bool authenticated, CloudHttpCallback callback,
		long timeout_ms)
{
	if (m_base_url.empty()) {
		CloudHttpResult result;
		result.error = "Cloud client not initialized";
		if (callback)
			callback(result);
		return;
	}

	HTTPFetchRequest fetch_request;
	fetch_request.caller = m_caller;
	fetch_request.request_id = m_next_request_id++;
	fetch_request.url = m_base_url + path;
	fetch_request.timeout = timeout_ms;
	fetch_request.connect_timeout = timeout_ms;
	fetch_request.quiet = true;

	if (method == "POST")
		fetch_request.method = HTTP_POST;
	else if (method == "PUT")
		fetch_request.method = HTTP_PUT;
	else if (method == "DELETE")
		fetch_request.method = HTTP_DELETE;
	else if (method == "PATCH")
		fetch_request.method = HTTP_PATCH;
	else
		fetch_request.method = HTTP_GET;

	if (!body_json.isNull()) {
		fetch_request.raw_data = fastWriteJson(body_json);
		fetch_request.extra_headers.emplace_back("Content-Type: application/json");
	}
	if (authenticated) {
		AuthInfo info = CloudConfig::get().auth();
		if (info.valid())
			fetch_request.extra_headers.push_back(
					"Authorization: Bearer " + info.deviceToken);
	}

	{
		std::lock_guard<std::mutex> lock(m_pending_mutex);
		m_pending.emplace(fetch_request.request_id, Pending{std::move(callback)});
	}

	httpfetch_async(fetch_request);
}

void CloudHttpClient::step()
{
	if (!m_caller)
		return;
	HTTPFetchResult fetch_result;
	while (httpfetch_async_get(m_caller, fetch_result)) {
		Pending pending;
		{
			std::lock_guard<std::mutex> lock(m_pending_mutex);
			auto it = m_pending.find(fetch_result.request_id);
			if (it == m_pending.end())
				continue;
			pending = std::move(it->second);
			m_pending.erase(it);
		}

		CloudHttpResult result;
		result.code = fetch_result.response_code;

		if (!fetch_result.succeeded) {
			result.error = fetch_result.timeout ?
					"请求超时,请检查网络连接" : "网络错误,无法连接云端";
		} else if (!fetch_result.data.empty()) {
			Json::Reader reader;
			Json::Value body;
			if (reader.parse(fetch_result.data, body) && body.isObject()) {
				result.data = body;
				if (body["error"].isString())
					result.error = body["error"].asString();
			}
		}

		result.success = fetch_result.succeeded &&
				fetch_result.response_code >= 200 && fetch_result.response_code < 300;
		if (!result.success && result.error.empty())
			result.error = "HTTP " + std::to_string(fetch_result.response_code);

		if (pending.callback)
			pending.callback(result);
	}
}

} // namespace cloud
