-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Conversation list (unread counts over the friends list)

local function make_formspec(tabdata)
	local info = cloud_store.info
	if not info then
		return table.concat({
			"label[0.5,1;", fgettext("私聊"), "]",
			"label[0.5,1.7;", fgettext("请先在「云同步」页签配对账号。"), "]",
		})
	end

	local friends = {}
	local data = core.cloud_get_friends()
	if type(data) == "table" and type(data.friends) == "table" then
		friends = data.friends
	end

	local unread_users = {}
	local unread_data = core.cloud_get_unread()
	if type(unread_data) == "table" and type(unread_data.users) == "table" then
		unread_users = unread_data.users
	end

	local lines = {}
	tabdata.dm_names = {}
	for i, f in ipairs(friends) do
		local display = f.displayName
		if not display or display == "" then
			display = f.username or "?"
		end
		local count = tonumber(unread_users[f.username] or 0) or 0
		local badge = count > 0 and
				core.colorize("#FFD700", " [" .. count .. " 条未读]") or ""
		local line = core.colorize(f.online and "#7bd07b" or "#999999",
				display) .. badge
		lines[#lines + 1] = core.formspec_escape(line)
		tabdata.dm_names[i] = f.username
	end
	if #lines == 0 then
		lines[1] = fgettext("(暂无好友)")
	end

	local fs = {
		"label[0.5,0.55;", fgettext("私聊"), "]",
		"textlist[0.5,1;14.5,4.4;clouddm_list;",
		table.concat(lines, ","), ";0]",
		"button[0.5,5.8;2.6,0.8;clouddm_open;", fgettext("打开会话"), "]",
	}

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
	name = "cloud_dm",
	caption = fgettext("私聊"),

	cbf_formspec = function(tabview, name, tabdata)
		return make_formspec(tabdata)
	end,

	cbf_button_handler = function(this, fields, name, tabdata)
		if fields.clouddm_open then
			local idx = core.get_textlist_index("clouddm_list")
			local username = idx and (tabdata.dm_names or {})[idx] or nil
			if username then
				cloud_dm.open(this, username)
			end
			return true
		end
		return false
	end,
}
