-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Party tab: create / join by code / follow the leader's server

local function member_line(m)
	local marks = {}
	if m.isLeader then
		marks[#marks + 1] = core.colorize("#FFD700", fgettext("[队长]"))
	end
	if m.online then
		marks[#marks + 1] = core.colorize("#7bd07b", fgettext("在线"))
	end
	return (m.displayName or m.username or "?") ..
			(#marks > 0 and (" " .. table.concat(marks, " ")) or "")
end

local function make_formspec(tabdata)
	if not cloud_store.info then
		return table.concat({
			"label[0.5,1;", fgettext("组队"), "]",
			"label[0.5,1.7;", fgettext("请先在「云同步」页签配对账号。"), "]",
		})
	end

	local party = core.cloud_get_party()
	tabdata.party = party

	local fs = {"label[0.5,0.55;", fgettext("组队"), "]"}

	if type(party) ~= "table" or not party.code then
		table.insert_all(fs, {
			"label[0.5,1.4;", fgettext("和好友组成队伍,由队长选择服务器,一键共同进服。"), "]",
			"button[0.5,2.6;2.6,0.8;cloudparty_create;", fgettext("创建队伍"), "]",
			"field[3.5,2.7;3,0.8;cloudparty_code;;]",
			"button[6.7,2.6;2.6,0.8;cloudparty_join;", fgettext("输入队伍码加入"), "]",
			"field_close_on_enter[cloudparty_code;false]",
		})
	else
		local i_am_leader = party.leader ==
				(cloud_store.info.siteUsername or "")
		tabdata.i_am_leader = i_am_leader

		table.insert_all(fs, {
			"label[0.5,1.4;", fgettext_ne("队伍码:$1", party.code), "]",
			"label[0.5,2.0;", fgettext_ne("队长:$1", party.leader or "?"), "]",
		})
		if party.serverAddress and party.serverAddress ~= "" then
			table.insert_all(fs, {
				"label[0.5,2.6;", fgettext_ne("目标服务器:$1", party.serverAddress), "]",
			})
		else
			table.insert_all(fs, {
				"label[0.5,2.6;", fgettext("目标服务器:未设定"), "]",
			})
		end

		local lines = {}
		tabdata.member_names = {}
		for i, m in ipairs(party.members or {}) do
			lines[#lines + 1] = core.formspec_escape(member_line(m))
			tabdata.member_names[i] = m.username
		end
		table.insert_all(fs, {
			"textlist[0.5,3.3;8.5,2.6;cloudparty_members;",
			table.concat(lines, ","), ";0]",
		})

		if party.serverAddress and party.serverAddress ~= "" then
			table.insert_all(fs, {
				"button[9.3,3.3;4.4,0.8;cloudparty_play;",
				fgettext("加入队伍服务器"), "]",
			})
		end
		if i_am_leader then
			table.insert_all(fs, {
				"field[0.5,6.3;4,0.8;cloudparty_server;",
				fgettext("服务器地址(host:port)"), ";]",
				"field_close_on_enter[cloudparty_server;false]",
				"button[4.7,6.3;2.4,0.8;cloudparty_setserver;",
				fgettext("设定服务器"), "]",
				"button[9.3,6.3;2.2,0.8;cloudparty_kick;",
				fgettext("踢出成员"), "]",
				"button[11.7,6.3;2,0.8;cloudparty_end;",
				fgettext("解散队伍"), "]",
			})
		end
		table.insert_all(fs, {
			"button[0.5,7.3;2.4,0.8;cloudparty_leave;", fgettext("退出队伍"), "]",
			"button[3.1,7.3;2.4,0.8;cloudparty_refresh;", fgettext("刷新"), "]",
		})
	end

	if cloud_social.notice then
		local color = cloud_social.notice_error and "#F44" or "#FFFFFF"
		table.insert_all(fs, {
			"label[0.5,6.8;", core.colorize(color,
					core.formspec_escape(cloud_social.notice)), "]",
		})
	end

	return table.concat(fs)
end

return {
	name = "cloud_party",
	caption = fgettext("组队"),

	cbf_formspec = function(tabview, name, tabdata)
		return make_formspec(tabdata)
	end,

	cbf_button_handler = function(this, fields, name, tabdata)
		if fields.cloudparty_create then
			core.cloud_party_create("")
			return true
		end

		if fields.cloudparty_join or
				(fields.key_enter_field == "cloudparty_code" and fields.key_enter) then
			local code = (fields.cloudparty_code or ""):trim()
			if code ~= "" then
				core.cloud_party_join(code)
			end
			return true
		end

		if fields.cloudparty_refresh then
			core.cloud_refresh()
			return true
		end

		if fields.cloudparty_leave then
			core.cloud_party_leave()
			return true
		end

		if fields.cloudparty_end and tabdata.i_am_leader then
			core.cloud_party_end()
			return true
		end

		if fields.cloudparty_setserver and tabdata.i_am_leader then
			local addr = (fields.cloudparty_server or ""):trim()
			if addr ~= "" then
				core.cloud_party_server(addr)
			end
			return true
		end

		if fields.cloudparty_kick and tabdata.i_am_leader then
			local idx = core.get_textlist_index("cloudparty_members")
			local username = idx and (tabdata.member_names or {})[idx] or nil
			if username then
				core.cloud_party_kick(username)
			end
			return true
		end

		if fields.cloudparty_play then
			local party = tabdata.party
			if type(party) == "table" and party.serverAddress
					and party.serverAddress ~= "" then
				local address, port = party.serverAddress:match("^(.+):(%d+)$")
				if address and port then
					port = tonumber(port)
				else
					address = party.serverAddress
					port = core.settings:get("default_port") or 30000
				end
				cloud_join.handle(this, address, port, nil, nil, nil)
			end
			return true
		end

		return false
	end,
}
