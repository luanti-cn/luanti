-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

local function friend_line(f)
	local name = f.username or ""
	local display = f.displayName
	if not display or display == "" then
		display = name
	end
	if name == "" and display == "" then
		return fgettext("(异常数据:空好友)")
	end
	local status
	if f.online then
		if f.currentServerAddress and f.currentServerAddress ~= "" then
			status = fgettext("在线 - $1", f.currentServerAddress)
		else
			status = fgettext("在线")
		end
	else
		status = fgettext("离线")
	end
	return core.colorize(f.online and "#7bd07b" or "#999999",
			display) .. " - " .. status
end

local function make_formspec(tabdata)
	local info = cloud_store.info
	local fs = {}

	if not info then
		table.insert_all(fs, {
			"label[0.5,1;", fgettext("好友 / 私聊 / 组队"), "]",
			"label[0.5,1.7;",
			fgettext("与 luanti.cn 账号配对后,即可查看好友、互发私聊、\\n一键加入好友开的本地游戏,并使用组队跟随。"), "]",
			"button[0.5,3.6;3,0.8;cloudfr_pair;", fgettext("输入配对码"), "]",
			"button[4,3.6;3,0.8;cloudfr_browser;", fgettext("打开网站"), "]",
		})
		if cloud_social.notice then
			table.insert_all(fs, {
				"label[0.5,5.1;", core.formspec_escape(cloud_social.notice), "]",
			})
		end
		return table.concat(fs)
	end

	table.insert_all(fs, {
		"label[0.5,0.55;", fgettext("好友($1)", #tabdata.friends or 0), "]",
	})

	-- friend list
	local lines = {}
	tabdata.friend_names = {}
	for i, f in ipairs(tabdata.friends or {}) do
		lines[#lines + 1] = core.formspec_escape(friend_line(f))
		tabdata.friend_names[i] = f.username
	end
	if #lines == 0 then
		lines[1] = fgettext("(暂无好友,可在网站添加)")
	end
	table.insert_all(fs, {
		"textlist[0.5,1;14.5,3.2;cloudfr_list;",
		table.concat(lines, ","), ";0]",
	})

	-- actions
	table.insert_all(fs, {
		"button[0.5,4.4;2.2,0.8;cloudfr_join;", fgettext("加入游戏"), "]",
		"button[2.9,4.4;2.2,0.8;cloudfr_dm;", fgettext("发私聊"), "]",
		"button[5.3,4.4;2.2,0.8;cloudfr_refresh;", fgettext("刷新"), "]",
	})

	-- room code entry
	table.insert_all(fs, {
		"field[7.8,4.5;3.2,0.8;cloudfr_code;;]",
		"button[11.2,4.4;2.2,0.8;cloudfr_code_join;", fgettext("房间码加入"), "]",
		"field_close_on_enter[cloudfr_code;false]",
	})

	-- pending friend requests (handled on the website)
	local reqs = {}
	for uname, display in pairs(cloud_social.pending_requests) do
		reqs[#reqs + 1] = display .. " (" .. uname .. ")"
	end
	if #reqs > 0 then
		table.insert_all(fs, {
			"label[0.5,5.6;", core.formspec_escape(
					fgettext("收到好友申请:$1(请在网站处理)",
					table.concat(reqs, ", "))), "]",
		})
	end

	if cloud_social.notice then
		local color = cloud_social.notice_error and "#F44" or "#FFFFFF"
		table.insert_all(fs, {
			"label[0.5,6.3;", core.colorize(color,
					core.formspec_escape(cloud_social.notice)), "]",
		})
	end

	return table.concat(fs)
end

local function selected_friend(tabdata)
	local idx = core.get_textlist_index("cloudfr_list")
	if idx == nil then
		return nil
	end
	return (tabdata.friend_names or {})[idx]
end

return {
	name = "cloud_friends",
	caption = fgettext("好友"),

	cbf_formspec = function(tabview, name, tabdata)
		tabdata.friends = {}
		local data = core.cloud_get_friends()
		if type(data) == "table" and type(data.friends) == "table" then
			tabdata.friends = data.friends
		end
		-- empty list: retry at most every 10s in case friends were added
		-- on the website after the client started
		if #tabdata.friends == 0 and cloud_store.info then
			local now = os.clock()
			if not tabdata.last_empty_refresh
					or now - tabdata.last_empty_refresh > 10 then
				tabdata.last_empty_refresh = now
				core.cloud_refresh()
			end
		end
		return make_formspec(tabdata)
	end,

	cbf_button_handler = function(this, fields, name, tabdata)
		if fields.cloudfr_pair then
			local dlg = create_cloud_pair_dialog()
			dlg:set_parent(this)
			this:hide()
			dlg:show()
			return true
		end

		if fields.cloudfr_browser then
			core.open_url(cloud_api.site_url())
			return true
		end

		if fields.cloudfr_refresh then
			core.cloud_refresh()
			return true
		end

		if fields.cloudfr_dm then
			local username = selected_friend(tabdata)
			if username then
				cloud_dm.open(this, username)
			end
			return true
		end

		if fields.cloudfr_join then
			local username = selected_friend(tabdata)
			if username then
				cloud_social.join_friend(this, username, nil)
			end
			return true
		end

		if fields.cloudfr_code_join or
				(fields.key_enter_field == "cloudfr_code" and fields.key_enter) then
			local code = (fields.cloudfr_code or ""):trim()
			if code ~= "" then
				cloud_social.join_friend(this, nil, code)
			end
			return true
		end

		return false
	end,

	on_change = function(type)
		if type == "ENTER" then
			if cloud_store.info then
				core.cloud_refresh()
			end
		end
	end,
}
