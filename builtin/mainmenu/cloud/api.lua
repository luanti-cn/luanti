-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

cloud_api = {}

local DEFAULT_BASE_URL = "https://api.luanti.cn"
local REQUEST_TIMEOUT = 15

local status_messages = {
	[400] = "请求参数不合法",
	[401] = "登录状态已失效,请重新配对设备",
	[403] = "账号被封禁或已被拉黑",
	[404] = "资源不存在",
	[409] = "操作冲突:可能已达数量上限,或名字已被占用",
	[410] = "配对码已使用或已过期",
	[413] = "文件过大",
	[500] = "服务器内部错误,请稍后再试",
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
				result.error = fgettext_ne("请求超时,请检查网络连接")
			else
				result.error = fgettext_ne("网络错误,无法连接到 $1", cloud_api.base_url())
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
						or fgettext_ne("未知错误(HTTP $1)", tostring(response.code))
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
