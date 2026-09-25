-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

cloud_join = {}

local function char_label(char)
	if not char.passwordType or char.passwordType == "RANDOM" then
		return char.name
	end
	if char.passwordType == "FIXED" then
		return char.name .. "(" .. fgettext("固定密码") .. ")"
	end
	if char.passwordType == "LIST_ROTATE" then
		return char.name .. "(" .. fgettext("密码轮换") .. ")"
	end
	return char.name
end

local function default_username()
	local info = cloud_store.info
	if info and info.defaultServerUsername and info.defaultServerUsername ~= "" then
		return info.defaultServerUsername
	end
	return core.settings:get("name") or ""
end

local function join_formspec(dialogdata)
	local retval = {
		"formspec_version[4]",
		"size[10,7.4]",
		"label[0.375,0.8;",
		fgettext("加入 $1", dialogdata.server and dialogdata.server.name
				or dialogdata.address), "]",
	}

	if dialogdata.busy then
		table.insert_all(retval, {
			"label[7.4,0.8;", fgettext("请稍候……"), "]",
		})
	end

	if dialogdata.loading then
		table.insert_all(retval, {
			"label[0.375,1.5;", fgettext("正在加载云端角色……"), "]",
			"button[6.85,6.2;2.775,0.8;dlg_join_cancel;", fgettext("取消"), "]",
		})
		return table.concat(retval)
	end

	local items = {fgettext("默认(站点用户名 + 随机密码)")}
	for _, char in ipairs(dialogdata.characters or {}) do
		items[#items + 1] = char_label(char)
	end
	local selected = dialogdata.selected or 1

	table.insert_all(retval, {
		"label[0.375,1.5;", fgettext("云端角色"), "]",
		"dropdown[0.375,1.8;9.25,0.8;dd_char;",
		core.formspec_escape(table.concat(items, ",")), ";",
		tostring(selected), "]",
		"field[0.375,3.05;9.25,0.8;te_username;",
		fgettext("玩家名(仅限字母、数字、- 和 _)"), ";",
		core.formspec_escape(dialogdata.name or ""), "]",
	})

	if dialogdata.char_error then
		table.insert_all(retval, {
			"label[0.375,4.1;", core.formspec_escape(dialogdata.char_error), "]",
			"button[7.75,4.2;1.875,0.6;dlg_join_reload;",
			fgettext("重试"), "]",
		})
	end

	local buttons_y = 6.2
	if dialogdata.error then
		table.insert_all(retval, {
			"box[0.375,", tostring(buttons_y - 0.9), ";9.25,0.6;#600]",
			"label[0.625,", tostring(buttons_y - 0.6), ";",
			core.formspec_escape(dialogdata.error), "]",
		})
		buttons_y = buttons_y + 0.8
		retval[2] = "size[10," .. tostring(buttons_y + 1.175) .. "]"
	end

	table.insert_all(retval, {
		"container[0.375,", tostring(buttons_y), "]",
		"button[0,0;2.775,0.8;dlg_join_manual;", fgettext("手动登录"), "]",
		"button[3.6125,0;2.775,0.8;dlg_join_cancel;", fgettext("取消"), "]",
		"button[7.225,0;2.025,0.8;dlg_join_confirm;", fgettext("加入"), "]",
		"container_end[]",
	})

	return table.concat(retval)
end

local function do_join(this, username, password, created_username)
	local function start()
		local address = this.data.address
		cloud_api.presence(address .. ":" .. this.data.port, function() end)

		gamedata.mode       = "join"
		gamedata.playername = username
		gamedata.password   = password
		gamedata.address    = address
		gamedata.port       = this.data.port
		gamedata.allow_login_or_register = "any"
		gamedata.selected_world = 0

		this:delete()
		core.start()
	end

	if created_username and username ~= created_username then
		cloud_api.vault_put(this.data.address, username, password, function(result)
			if not result.success then
				this.data.busy = false
				this.data.error = result.error
				ui.update()
				return
			end
			start()
		end)
	else
		start()
	end
end

local function on_join_clicked(this, fields)
	local selected = this.data.selected or 1
	local characters = this.data.characters or {}
	local character = selected > 1 and characters[selected - 1] or nil

	this.data.busy = true
	this.data.error = nil
	ui.update()

	cloud_api.provision(this.data.address, character and character.id or nil,
			function(result)
		if not result.success then
			this.data.busy = false
			this.data.error = result.error
			ui.update()
			return
		end

		local username = (fields.te_username or this.data.name or ""):trim()
		if username == "" then
			username = result.data.username
		end
		if not core.is_valid_player_name(username) then
			this.data.busy = false
			this.data.error = fgettext("玩家名不合法:仅限字母、数字、- 和 _,最长 20 个字符")
			ui.update()
			return
		end

		do_join(this, username, result.data.password, result.data.username)
	end)
end

local function join_buttonhandler(this, fields)
	this.data.error = nil

	if fields.dlg_join_cancel then
		this:delete()
		return true
	end

	if fields.dlg_join_manual then
		this:delete()
		if this.data.manual then
			this.data.manual()
		end
		return true
	end

	if fields.dlg_join_reload then
		this.data.loading = true
		this.data.char_error = nil
		ui.update()
		cloud_api.characters(function(result)
			this.data.loading = false
			if result.success then
				this.data.characters = result.data.characters or {}
			else
				this.data.char_error = result.error
			end
			ui.update()
		end)
		return true
	end

	if (fields.dlg_join_confirm or fields.key_enter) and not this.data.busy then
		this.data.name = fields.te_username or this.data.name or ""
		on_join_clicked(this, fields)
		return true
	end

	if fields.dd_char and not this.data.busy then
		local idx = tonumber(fields.dd_char)
		if idx and idx >= 1 then
			this.data.selected = idx
			local characters = this.data.characters or {}
			if idx > 1 and characters[idx - 1] then
				this.data.name = characters[idx - 1].name
			else
				this.data.name = default_username()
			end
			ui.update()
		end
		return true
	end

	return false
end

function cloud_join.handle(tabview, address, port, server, manual)
	if not address or address == "" or not port then
		return false
	end

	if server and not is_server_protocol_compat_or_error(
				server.proto_min, server.proto_max) then
		return true
	end

	local retval = dialog_create("dlg_cloud_join",
			join_formspec,
			join_buttonhandler,
			nil)
	retval.data.address = address
	retval.data.port = port
	retval.data.server = server
	retval.data.name = default_username()
	retval.data.selected = 1
	retval.data.loading = true
	retval.data.manual = manual

	retval:set_parent(tabview)
	tabview:hide()
	retval:show()

	cloud_api.characters(function(result)
		retval.data.loading = false
		if result.success then
			retval.data.characters = result.data.characters or {}
		else
			retval.data.char_error = result.error
		end
		ui.update()
	end)

	return true
end
