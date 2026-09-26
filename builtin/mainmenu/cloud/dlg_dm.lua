-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

cloud_dm = {}

local function display_name(username)
	for _, f in ipairs((core.cloud_get_friends() or {}).friends or {}) do
		if f.username == username then
			local d = f.displayName
			if d and d ~= "" then
				return d
			end
			return username
		end
	end
	return username
end

function cloud_dm.open(parent, username)
	local retval = dialog_create("dlg_cloud_dm",
			cloud_dm.formspec,
			cloud_dm.buttonhandler,
			nil)
	retval.data.username = username
	retval.data.display = display_name(username)
	retval.data.input = ""
	retval.data.session = {}   -- own messages sent this session {mine, body}
	retval.data.last_cache_len = -1
	retval.data.last_session_len = -1
	retval.data.loaded = false

	retval:set_parent(parent)
	parent:hide()
	retval:show()

	core.cloud_set_open_chat(username)
	core.cloud_mark_read(username)
	core.cloud_load_older(username, 0)

	return retval
end

function cloud_dm.formspec(dialogdata)
	local username = dialogdata.username
	local display = dialogdata.display or username

	-- cached messages (oldest..newest) + own session messages
	local cache = {}
	local data = core.cloud_get_messages(username)
	if type(data) == "table" and type(data.messages) == "table" then
		cache = data.messages
	end

	local lines = {}
	for _, m in ipairs(cache) do
		lines[#lines + 1] = core.formspec_escape(core.colorize("#BFBFBF",
				(m.fromDisplay or m.from or "?")) .. ": " .. (m.body or ""))
	end
	for _, m in ipairs(dialogdata.session) do
		if m.mine then
			lines[#lines + 1] = core.formspec_escape(
					core.colorize("#7bd07b", "我") .. ": " .. m.body)
		else
			lines[#lines + 1] = core.formspec_escape(
					core.colorize("#BFBFBF", display) .. ": " .. m.body)
		end
	end
	dialogdata.last_render = #lines

	local fs = {
		"formspec_version[4]",
		"size[12,8]",
		"label[0.375,0.8;", core.formspec_escape(
				fgettext("私聊 - $1", display)), "]",
		"textlist[0.375,1.3;11.25,4.9;;",
		table.concat(lines, ","), ";0]",
		"field[0.375,6.5;9.2,0.8;te_msg;;]",
		"field_close_on_enter[te_msg;false]",
		"button[9.8,6.5;1.8,0.8;dlg_dm_send;", fgettext("发送"), "]",
		"button[0.375,7.35;2.5,0.6;dlg_dm_close;", fgettext("关闭"), "]",
	}

	if not dialogdata.loaded then
		table.insert_all(fs, {
			"label[3.5,7.5;", fgettext("正在加载聊天记录……"), "]",
		})
	end

	return table.concat(fs)
end

function cloud_dm.buttonhandler(this, fields)
	if fields.dlg_dm_close then
		core.cloud_set_open_chat("")
		this:delete()
		return true
	end

	local send_pressed = fields.dlg_dm_send or
			(fields.key_enter_field == "te_msg" and fields.key_enter)
	if send_pressed then
		local body = (fields.te_msg or this.data.input or ""):trim()
		if body ~= "" then
			this.data.session[#this.data.session + 1] = {mine = true, body = body}
			core.cloud_send_dm(this.data.username, body)
			this.data.input = ""
			ui.update()
		end
		return true
	end

	if fields.te_msg then
		this.data.input = fields.te_msg
		return true
	end

	return false
end
