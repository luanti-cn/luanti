-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

cloud_api = {}

local DEFAULT_BASE_URL = "https://api.luanti.cn"
local REQUEST_TIMEOUT = 15

local status_messages = {
	[400] = "Invalid request parameters",
	[401] = "Session expired, please pair your device again",
	[403] = "Account banned or blocked",
	[404] = "Resource not found",
	[409] = "Conflict: limit reached or name already taken",
	[410] = "Pairing code already used or expired",
	[413] = "File too large",
	[500] = "Internal server error, please try again later",
}

function cloud_api.base_url()
	local url = core.settings:get("cloud_sync_url")
	if not url or url:trim() == "" then
		url = DEFAULT_BASE_URL
	end
	return (url:gsub("/+$", ""))
end

-- 浏览器打开的站点首页(配对引导、账号管理),与 API 域名分离。
function cloud_api.site_url()
	local url = core.settings:get("cloud_site_url")
	if not url or url:trim() == "" then
		url = cloud_api.base_url()
	end
	return (url:gsub("/+$", ""))
end

local function request_worker(param)
	local http = core.get_http_api()
	local response = http.fetch_sync({
		url = param.url,
		method = param.method,
		data = param.data,
		extra_headers = param.extra_headers,
		timeout = param.timeout,
		quiet = true,
	})
	return {
		succeeded = response.succeeded,
		timeout = response.timeout,
		code = response.code,
		data = response.data,
	}
end

local function do_request(method, path, body, authenticated, callback)
	local headers = {"Content-Type: application/json"}
	if authenticated and cloud_store.info then
		headers[#headers + 1] =
				"Authorization: Bearer " .. cloud_store.info.deviceToken
	end

	local param = {
		url = cloud_api.base_url() .. path,
		method = method,
		data = body and core.write_json(body) or nil,
		extra_headers = headers,
		timeout = REQUEST_TIMEOUT,
	}

	core.handle_async(request_worker, param, function(response)
		local result = {
			success = false,
			code = response.code,
			data = nil,
			error = nil,
		}

		if not response.succeeded then
			if response.timeout then
				result.error = fgettext_ne("Request timed out, please check your network connection")
			else
				result.error = fgettext_ne("Network error, could not connect to $1", cloud_api.base_url())
			end
			callback(result)
			return
		end

		local parsed = core.parse_json(response.data)
		if type(parsed) == "table" then
			result.data = parsed
			if type(parsed.error) == "string" then
				result.error = parsed.error
			end
		end

		if response.code >= 200 and response.code < 300 then
			result.success = true
		else
			if response.code == 401 then
				cloud_store.clear()
			end
			if not result.error then
				local fallback = status_messages[response.code]
				result.error = fallback and fgettext_ne(fallback)
						or fgettext_ne("Unknown error (HTTP $1)", tostring(response.code))
			end
		end

		callback(result)
	end)
end

function cloud_api.pair(code, device_name, callback)
	local body = {code = code}
	if device_name and device_name ~= "" then
		body.device_name = device_name
	end
	do_request("POST", "/api/cloud/client/pair/", body, false, callback)
end

function cloud_api.me(callback)
	do_request("GET", "/api/cloud/client/me/", nil, true, callback)
end

function cloud_api.characters(callback)
	do_request("GET", "/api/cloud/client/characters/", nil, true, callback)
end

function cloud_api.provision(address, character_id, callback)
	local body = {address = address}
	if character_id then
		body.characterId = character_id
	end
	do_request("POST", "/api/cloud/client/vault/provision/", body, true, callback)
end

function cloud_api.vault_put(address, username, password, callback)
	do_request("PUT", "/api/cloud/client/vault/", {
		address = address,
		username = username,
		password = password,
	}, true, callback)
end

function cloud_api.presence(address, callback)
	do_request("POST", "/api/cloud/client/presence/", {address = address},
			true, callback)
end
