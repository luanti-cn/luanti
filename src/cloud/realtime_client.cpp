// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "realtime_client.h"

#include <cstring>
#include <map>

#include <json/json.h>

#include "cloud_config.h"
#include "convert_json.h"
#include "httpfetch.h"
#include "log.h"
#include "porting.h"
#include "settings.h"

#if USE_CURL
#include <curl/curl.h>
#if LIBCURL_VERSION_NUM >= 0x075600 // 7.86.0: WebSocket API
#include <curl/websockets.h>
#define CLOUD_USE_WS 1
#endif
#endif

namespace cloud
{

static constexpr u64 PING_INTERVAL_MS = 30'000;
static constexpr u64 PRESENCE_INTERVAL_MS = 60'000;
static constexpr u64 POLL_INTERVAL_MS = 30'000;
static constexpr u64 MAX_SILENCE_MS = 120'000;

static Json::Value makePing()
{
	Json::Value v(Json::objectValue);
	v["type"] = "ping";
	return v;
}

RealtimeClient::~RealtimeClient()
{
	if (isRunning()) {
		stop();
		wait();
	}
}

void RealtimeClient::begin(const std::string &ws_url)
{
	m_ws_url = ws_url;
	if (!isRunning())
		start();
}

bool RealtimeClient::send(const Json::Value &msg)
{
	if (!m_ws_connected)
		return false;
	m_outgoing.push_back(msg);
	return true;
}

bool RealtimeClient::popEvent(Json::Value &out)
{
	try {
		out = m_incoming.pop_front(0);
		return true;
	} catch (ItemNotFoundException &) {
		return false;
	}
}

void RealtimeClient::setPresenceAddress(const std::string &address)
{
	{
		std::lock_guard<std::mutex> lock(m_presence_mutex);
		if (m_presence_address == address)
			return;
		m_presence_address = address;
	}
	m_presence_dirty = true;
}

static bool waitForStopOrDelay(u32 delay_ms)
{
	u32 left = delay_ms;
	while (left >= 100) {
		sleep_ms(100);
		left -= 100;
	}
	if (left > 0)
		sleep_ms(left);
	return false;
}

void *RealtimeClient::run()
{
	int ws_failures = 0;

	while (!stopRequested()) {
		bool ws_attempt = false;
#if defined(CLOUD_USE_WS)
		ws_attempt = !m_ws_permanently_failed;
#endif
		if (ws_attempt) {
			bool ok = wsSession();
			m_ws_connected = false;
			if (stopRequested())
				break;
			if (ok) {
				ws_failures = 0;
				waitForStopOrDelay(1000);
			} else if (++ws_failures >= 5) {
				warningstream << "Cloud: WebSocket unavailable, "
						"falling back to REST polling" << std::endl;
				m_ws_permanently_failed = true;
			} else {
				waitForStopOrDelay(3000);
			}
		} else {
			pollSession();
			m_poll_active = false;
		}
	}
	return nullptr;
}

#if defined(CLOUD_USE_WS)

bool RealtimeClient::wsSendAll(const std::string &payload)
{
	auto *eh = static_cast<CURL *>(m_curl_handle);
	size_t total = payload.size();
	size_t off = 0;
	while (off < total) {
		size_t sent = 0;
		unsigned int flags = (off == 0) ? CURLWS_TEXT : 0;
		CURLcode rc = curl_ws_send(eh, payload.data() + off, total - off,
				&sent, 0, flags);
		if (rc != CURLE_OK || sent == 0)
			return false;
		off += sent;
	}
	return true;
}

bool RealtimeClient::flushOutgoing()
{
	while (true) {
		Json::Value msg;
		try {
			msg = m_outgoing.pop_front(0);
		} catch (ItemNotFoundException &) {
			return true;
		}
		if (!wsSendAll(fastWriteJson(msg))) {
			infostream << "Cloud: WS send failed" << std::endl;
			return false;
		}
	}
}

bool RealtimeClient::wsSendPing()
{
	m_outgoing.push_back(makePing());
	return flushOutgoing();
}

bool RealtimeClient::wsSession()
{
	CURL *eh = curl_easy_init();
	if (!eh)
		return false;
	m_curl_handle = eh;

	struct curl_slist *headers = nullptr;
	AuthInfo info = CloudConfig::get().auth();
	if (info.valid()) {
		std::string h = "Authorization: Bearer " + info.deviceToken;
		headers = curl_slist_append(headers, h.c_str());
	}

	curl_easy_setopt(eh, CURLOPT_URL, m_ws_url.c_str());
	curl_easy_setopt(eh, CURLOPT_CONNECT_ONLY, 2L); // websocket
	curl_easy_setopt(eh, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(eh, CURLOPT_HTTPHEADER, headers);
	if (!g_settings->getBool("curl_verify_cert"))
		curl_easy_setopt(eh, CURLOPT_SSL_VERIFYPEER, 0L);
	long curl_timeout = g_settings->getS32("curl_timeout");
	curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT_MS,
			curl_timeout > 0 ? curl_timeout * 1000 : 10000L);

	CURLcode rc = curl_easy_perform(eh);
	if (rc != CURLE_OK) {
		warningstream << "Cloud: WS connect failed: "
				<< curl_easy_strerror(rc) << std::endl;
		curl_slist_free_all(headers);
		curl_easy_cleanup(eh);
		m_curl_handle = nullptr;
		return false;
	}

	warningstream << "Cloud: WebSocket connected" << std::endl;
	m_ws_connected = true;

	bool ok = true;
	std::string accumulate;
	u64 last_incoming_ms = porting::getTimeMs();
	u64 last_ping_ms = 0;
	u64 last_presence_ms = 0;

	char buffer[16 * 1024];
	while (!stopRequested()) {
		// ---- read all available data ----
		while (true) {
			size_t received = 0;
			const struct curl_ws_frame *frame = nullptr;
			rc = curl_ws_recv(eh, buffer, sizeof(buffer), &received, &frame);
			if (rc == CURLE_AGAIN)
				break;
			if (rc != CURLE_OK) {
				infostream << "Cloud: WS recv failed: "
						<< curl_easy_strerror(rc) << std::endl;
				ok = false;
				break;
			}
			last_incoming_ms = porting::getTimeMs();
			if (received > 0) {
				accumulate.append(buffer, received);
				bool frame_done = !frame || frame->bytesleft == 0;
				if (frame_done && !accumulate.empty()) {
					Json::Value msg;
					Json::Reader reader;
					if (reader.parse(accumulate, msg) && msg.isObject())
						m_incoming.push_back(std::move(msg));
					accumulate.clear();
				}
			} else {
				break;
			}
		}
		if (!ok)
			break;

		u64 now = porting::getTimeMs();

		// ---- outgoing / heartbeat / presence ----
		if (!m_outgoing.empty() && !flushOutgoing()) {
			ok = false;
			break;
		}
		if (now - last_ping_ms >= PING_INTERVAL_MS) {
			if (!wsSendPing()) {
				ok = false;
				break;
			}
			last_ping_ms = now;
		}
		bool presence_due = m_presence_dirty ||
				(now - last_presence_ms >= PRESENCE_INTERVAL_MS);
		if (presence_due) {
			Json::Value presence(Json::objectValue);
			presence["type"] = "presence";
			{
				std::lock_guard<std::mutex> lock(m_presence_mutex);
				if (!m_presence_address.empty())
					presence["address"] = m_presence_address;
			}
			m_outgoing.push_back(std::move(presence));
			m_presence_dirty = false;
			last_presence_ms = now;
			if (!flushOutgoing()) {
				ok = false;
				break;
			}
		}

		if (now - last_incoming_ms >= MAX_SILENCE_MS) {
			infostream << "Cloud: WS silent for too long, reconnecting" << std::endl;
			break;
		}

		// ---- wait ----
		sleep_ms(20);
	}

	m_ws_connected = false;
	curl_slist_free_all(headers);
	curl_easy_cleanup(eh);
	m_curl_handle = nullptr;
	return ok;
}

#else // !CLOUD_USE_WS

bool RealtimeClient::wsSendAll(const std::string &) { return false; }
bool RealtimeClient::flushOutgoing() { return true; }
bool RealtimeClient::wsSendPing() { return false; }
bool RealtimeClient::wsSession() { return false; }

#endif // CLOUD_USE_WS

// ---------------------------------------------------------------------------
// REST polling fallback: synthesizes the same events the WS gateway pushes.
// ---------------------------------------------------------------------------

static bool syncJsonFetch(const std::string &url, const std::string &token,
		const std::string &body, const char *method, long timeout_ms,
		long &response_code, std::string &response)
{
	HTTPFetchRequest req;
	req.url = url;
	req.caller = HTTPFETCH_SYNC;
	req.timeout = timeout_ms;
	req.connect_timeout = timeout_ms;
	req.quiet = true;
	if (!strcmp(method, "POST"))
		req.method = HTTP_POST;
	else if (!strcmp(method, "PUT"))
		req.method = HTTP_PUT;
	else if (!strcmp(method, "DELETE"))
		req.method = HTTP_DELETE;
	if (!body.empty()) {
		req.raw_data = body;
		req.extra_headers.emplace_back("Content-Type: application/json");
	}
	if (!token.empty())
		req.extra_headers.push_back("Authorization: Bearer " + token);

	HTTPFetchResult res;
	if (!httpfetch_sync_interruptible(req, res, 100))
		return false;
	response_code = res.response_code;
	response = std::move(res.data);
	return res.succeeded;
}

bool RealtimeClient::pollSession()
{
	m_poll_active = true;
	warningstream << "Cloud: realtime channel degraded to REST polling" << std::endl;

	AuthInfo info = CloudConfig::get().auth();
	if (!info.valid()) {
		// nothing to poll; idle until auth appears
		while (!stopRequested() && !CloudConfig::get().isPaired())
			sleep_ms(500);
		if (stopRequested())
			return true;
		info = CloudConfig::get().auth();
	}

	std::string base = CloudConfig::get().baseUrl();
	std::map<std::string, long> last_seen_id;

	while (!stopRequested()) {
		u64 cycle_start = porting::getTimeMs();

		// friends snapshot
		{
			long code = 0;
			std::string body;
			if (syncJsonFetch(base + "/api/cloud/client/friends/",
					info.deviceToken, "", "GET", 10000, code, body) &&
					code == 200) {
				Json::Value msg;
				Json::Reader reader;
				if (reader.parse(body, msg) && msg.isObject()) {
					Json::Value evt(Json::objectValue);
					evt["type"] = "friends.snapshot";
					evt["friends"] = msg["friends"];
					m_incoming.push_back(std::move(evt));
				}
			}
		}

		// unread snapshot (+ dm.new synthesis for new messages)
		{
			long code = 0;
			std::string body;
			if (syncJsonFetch(base + "/api/cloud/client/messages/unread/",
					info.deviceToken, "", "GET", 10000, code, body) &&
					code == 200) {
				Json::Value msg;
				Json::Reader reader;
				if (reader.parse(body, msg) && msg.isObject()) {
					const Json::Value &arr = msg["unread"];
					if (arr.isArray()) {
						for (const auto &entry : arr) {
							std::string user = entry["username"].asString();
							if (user.empty())
								continue;
							long &seen = last_seen_id[user];
							// fetch recent history to discover new messages
							long hcode = 0;
							std::string hbody;
							std::string url = base + "/api/cloud/client/messages/" +
									user + "/?limit=20";
							if (!syncJsonFetch(url, info.deviceToken, "", "GET",
									10000, hcode, hbody) || hcode != 200)
								continue;
							Json::Value hist;
							Json::Reader hreader;
							if (!hreader.parse(hbody, hist) || !hist.isObject())
								continue;
							const Json::Value &messages = hist["messages"];
							if (!messages.isArray())
								continue;
							long max_id = seen;
							for (const auto &m : messages) {
								long id = m["id"].asLargestInt();
								if (id > max_id)
									max_id = id;
								if (m["from"].asString() != user)
									continue; // own outgoing messages
								if (seen == 0 || id <= seen)
									continue;
								Json::Value evt(Json::objectValue);
								evt["type"] = "dm.new";
								evt["from"] = m["from"];
								evt["fromDisplay"] = m["fromDisplay"];
								evt["body"] = m["body"];
								evt["ts"] = m["createdAt"];
								m_incoming.push_back(std::move(evt));
							}
							// first poll for a user: don't replay old history
							seen = max_id;
						}
					}
				}
			}
		}

		// presence
		{
			Json::Value presence(Json::objectValue);
			std::string address;
			{
				std::lock_guard<std::mutex> lock(m_presence_mutex);
				address = m_presence_address;
			}
			presence["address"] = address;
			long pcode = 0;
			std::string out;
			syncJsonFetch(base + "/api/cloud/client/presence/",
					info.deviceToken, fastWriteJson(presence), "POST",
					10000, pcode, out);
		}

		// wait out the remainder of the polling interval
		while (!stopRequested() &&
				porting::getTimeMs() - cycle_start < POLL_INTERVAL_MS)
			sleep_ms(200);

		// pick up re-pairing done by the Lua UI
		AuthInfo fresh = CloudConfig::get().auth();
		if (fresh.valid() && fresh.deviceToken != info.deviceToken)
			info = fresh;
	}
	return true;
}

} // namespace cloud
