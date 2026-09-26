-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Progress dialog shown while a friend-host tunnel is established
-- (hole punching / relay fallback). Once connected, hands over to the
-- credential dialog (cloud_join) which launches the actual game.

local function status_text(data)
	local st = data.status or {}
	if st.stage == "joining" then
		return fgettext("正在联系云端……")
	elseif st.stage == "tunneling" then
		if st.tunnel and st.tunnel.viaRelay then
			return fgettext("正在通过中继连接到 $1 ……", st.hostUsername or "?")
		end
		return fgettext("正在与 $1 建立直连(打洞)……", st.hostUsername or "?")
	elseif st.stage == "connected" then
		return fgettext("已连接!正在准备进入游戏……")
	elseif st.stage == "failed" then
		return fgettext_ne("连接失败:$1", st.error or "")
	end
	return fgettext("正在准备……")
end

local function join_progress_formspec(dialogdata)
	local fs = {
		"formspec_version[4]",
		"size[10,4.6]",
		"label[0.375,0.8;",
		core.formspec_escape(fgettext("加入好友房间")),"]",
		"label[0.375,1.7;",
		core.formspec_escape(status_text(dialogdata)), "]",
	}

	if dialogdata.status and dialogdata.status.stage == "failed" then
		table.insert_all(fs, {
			"container[0.375,3.4]",
			"button[0,0;2.775,0.8;dlg_joinfriend_close;", fgettext("返回"), "]",
			"container_end[]",
		})
	else
		table.insert_all(fs, {
			"container[0.375,3.4]",
			"button[7.225,0;2.025,0.8;dlg_joinfriend_cancel;", fgettext("取消"), "]",
			"container_end[]",
		})
	end

	return table.concat(fs)
end

local function join_progress_buttonhandler(this, fields)
	if fields.dlg_joinfriend_cancel then
		core.cloud_join_cancel()
		this:delete()
		return true
	end
	if fields.dlg_joinfriend_close then
		core.cloud_join_cancel()
		this:delete()
		return true
	end
	return false
end

--- Start joining `username`'s hosted room (or by `room_code`) and show
--- progress. On success switches to the credential dialog and starts the game.
function cloud_social.join_friend(tabview, username, room_code)
	local ok
	if room_code and room_code ~= "" then
		ok = core.cloud_join_code(room_code)
	else
		ok = core.cloud_join_friend(username or "")
	end
	if not ok then
		cloud_social.notify(fgettext("已在加入流程中,请先取消当前操作"), true)
		return
	end

	local retval = dialog_create("dlg_cloud_join_friend",
			join_progress_formspec,
			join_progress_buttonhandler,
			nil)
	retval.data.status = {stage = "joining"}
	retval.data.parent = tabview
	retval.data.launched = false

	retval:set_parent(tabview)
	tabview:hide()
	retval:show()
end
