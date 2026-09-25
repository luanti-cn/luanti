-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

cloud_store = {
	info = nil,
}

do
	local dir = core.get_user_path() .. DIR_DELIM .. "client" ..
			DIR_DELIM .. "cloud"
	local path = dir .. DIR_DELIM .. "auth.json"

	local function read()
		local file = io.open(path, "r")
		if not file then
			return nil
		end
		local content = file:read("*all")
		file:close()
		return core.parse_json(content)
	end

	function cloud_store.load()
		local info = read()
		if type(info) ~= "table" or type(info.deviceToken) ~= "string"
				or #info.deviceToken < 8 then
			cloud_store.info = nil
			return nil
		end
		cloud_store.info = info
		return info
	end

	function cloud_store.save(info)
		assert(type(info) == "table" and type(info.deviceToken) == "string"
				and #info.deviceToken > 0)
		cloud_store.info = info
		assert(core.create_dir(dir))
		core.safe_file_write(path, core.write_json(info))
	end

	function cloud_store.clear()
		cloud_store.info = nil
		os.remove(path)
	end

	cloud_store.path = path
	cloud_store.load()
end
