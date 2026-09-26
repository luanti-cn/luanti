// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) LuantiCN contributors

#include "cloud_config.h"

#include <json/json.h>
#include <fstream>
#include <sstream>

#include "convert_json.h"
#include "filesys.h"
#include "log.h"
#include "porting.h"
#include "settings.h"
#include "util/string.h"

namespace cloud
{

static constexpr const char *DEFAULT_BASE_URL = "https://api.luanti.cn";
static constexpr const char *DEFAULT_SITE_URL = "https://luanti.cn";

CloudConfig &CloudConfig::get()
{
	static CloudConfig singleton;
	return singleton;
}

static std::string trimTrailingSlashes(std::string url)
{
	while (!url.empty() && url.back() == '/')
		url.pop_back();
	return url;
}

void CloudConfig::refreshSettings()
{
	std::string url = g_settings->get("cloud_sync_url");
	if (trim(url).empty())
		url = DEFAULT_BASE_URL;
	m_base_url = trimTrailingSlashes(url);

	url = g_settings->get("cloud_site_url");
	if (trim(url).empty())
		url = DEFAULT_SITE_URL;
	m_site_url = trimTrailingSlashes(url);
}

std::string CloudConfig::wsUrl() const
{
	std::string base = m_base_url;
	std::string rest;
	auto pos = base.find("://");
	if (pos != std::string::npos) {
		rest = base.substr(pos + 3);
		base = base.substr(0, pos);
	} else {
		rest = base;
		base = "https";
	}
	std::string scheme = (base == "http") ? "ws" : "wss";
	return scheme + "://" + rest + "/api/cloud/client/ws/";
}

std::string CloudConfig::authFilePath()
{
	return porting::path_user + DIR_DELIM + "client" + DIR_DELIM + "cloud" +
			DIR_DELIM + "auth.json";
}

void CloudConfig::reloadAuth()
{
	AuthInfo info;
	std::string path = authFilePath();
	std::ifstream stream(path.c_str(), std::ios::binary);
	if (stream) {
		std::stringstream sstr;
		sstr << stream.rdbuf();
		Json::Value root;
		Json::Reader reader;
		if (reader.parse(sstr.str(), root)) {
			if (root.isObject()) {
				if (root["deviceToken"].isString())
					info.deviceToken = root["deviceToken"].asString();
				if (root["siteUsername"].isString())
					info.siteUsername = root["siteUsername"].asString();
				if (root["displayName"].isString())
					info.displayName = root["displayName"].asString();
				if (root["deviceName"].isString())
					info.deviceName = root["deviceName"].asString();
				if (root["defaultServerUsername"].isString())
					info.defaultServerUsername = root["defaultServerUsername"].asString();
			}
		}
	}
	if (!info.valid())
		info = AuthInfo();

	std::lock_guard<std::mutex> lock(m_auth_mutex);
	m_auth = info;
}

AuthInfo CloudConfig::auth() const
{
	std::lock_guard<std::mutex> lock(m_auth_mutex);
	return m_auth;
}

void CloudConfig::saveAuth(const AuthInfo &info)
{
	{
		std::lock_guard<std::mutex> lock(m_auth_mutex);
		m_auth = info;
	}

	Json::Value root(Json::objectValue);
	root["deviceToken"] = info.deviceToken;
	if (!info.siteUsername.empty())
		root["siteUsername"] = info.siteUsername;
	if (!info.displayName.empty())
		root["displayName"] = info.displayName;
	if (!info.deviceName.empty())
		root["deviceName"] = info.deviceName;
	if (!info.defaultServerUsername.empty())
		root["defaultServerUsername"] = info.defaultServerUsername;

	std::string path = authFilePath();
	std::string dir = path.substr(0, path.find_last_of(DIR_DELIM));
	if (!fs::CreateAllDirs(dir)) {
		errorstream << "Cloud: cannot create " << dir << std::endl;
		return;
	}
	std::ofstream stream(path.c_str(), std::ios::binary | std::ios::trunc);
	if (stream)
		stream << fastWriteJson(root);
	if (!stream)
		errorstream << "Cloud: cannot write " << path << std::endl;
}

void CloudConfig::clearAuth()
{
	std::lock_guard<std::mutex> lock(m_auth_mutex);
	m_auth = AuthInfo();
	remove(authFilePath().c_str());
}

std::vector<std::string> CloudConfig::stunServers() const
{
	std::vector<std::string> result;
	std::string raw = g_settings->get("cloud_stun_servers");
	if (trim(raw).empty())
		raw = "stun.cloudflare.com:3478,stun.l.google.com:19302";
	for (const auto &part : str_split(raw, ',')) {
		std::string s(trim(part));
		if (!s.empty())
			result.push_back(s);
	}
	return result;
}

} // namespace cloud
