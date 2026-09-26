-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Shared state and per-frame plumbing for the cloud social UI
-- (friends / DM / party tabs and friend-host joins).

cloud_social = {
	pending_requests = {},  -- username -> display name (until website visit)
	notice = nil,           -- transient status line shown on tabs
	notice_error = false,
	-- friend-host join progress (mirrors core.cloud_join_status())
	join = {stage = "idle"},
}

local notice_until = 0

function cloud_social.notify(text, is_error)
	cloud_social.notice = text
	cloud_social.notice_error = is_error and true or false
	notice_until = os.clock() + 6
end

local function clear_notice()
	if cloud_social.notice and os.clock() > notice_until then
		cloud_social.notice = nil
		cloud_social.notice_error = false
	end
end

local handled_dms = {}

local function handle_event(ev)
	local data = ev.data
	if ev.type == "dm.new" then
		cloud_social.notify(fgettext_ne("私聊 $1: $2",
				data.fromDisplay or data.from, data.body or ""))
		handled_dms[data.from] = true
	elseif ev.type == "friend.request" then
		cloud_social.pending_requests[data.from] = data.fromDisplay or data.from
		cloud_social.notify(fgettext_ne("$1 请求加你为好友,请到网站处理",
				data.fromDisplay or data.from))
	elseif ev.type == "friend.accepted" then
		cloud_social.pending_requests[data.username] = nil
		cloud_social.notify(fgettext_ne("已与 $1 成为好友",
				data.displayName or data.username))
		core.cloud_refresh()
	elseif ev.type == "presence" then
		-- friends list refresh is cheap enough on visibility change only
	elseif ev.type == "party.update" then
		cloud_social.notify(fgettext("队伍信息已更新"))
	elseif ev.type == "party.error" then
		cloud_social.notify(fgettext_ne("组队失败: $1",
				type(data) == "table" and (data.message or "") or data), true)
	elseif ev.type == "error" then
		cloud_social.notify(fgettext_ne("云端错误: $1",
				data.message or ""), true)
	elseif ev.type == "notify" then
		cloud_social.notify(type(data) == "table"
				and (data.message or "") or data, true)
	end
end

-- friend-host join progress: refresh dialog and hand over on connect
local function poll_join_progress()
	local dlg = ui.childlist["dlg_cloud_join_friend"]
	if not dlg then
		return
	end
	local st = core.cloud_join_status()
	if st.stage ~= dlg.data.status.stage
			or st.error ~= (dlg.data.status.error or "") then
		dlg.data.status = st
		ui.update()
	end
	if st.stage == "connected" and not dlg.data.launched then
		dlg.data.launched = true
		local endpoint = st.endpoint or ""
		local addr, port = endpoint:match("^(.+):(%d+)$")
		local parent = dlg.data.parent
		dlg:delete()
		core.cloud_set_open_chat("")
		if addr and port then
			-- vault key: per-host namespace so P2P credentials stay stable
			cloud_join.handle(parent, addr, tonumber(port), nil, nil,
					"p2p:" .. (st.hostUsername or "?"))
		end
	end
end

local dm_mark_read_timer = 0

local function poll_dm_dialog()
	local dlg = ui.childlist["dlg_cloud_dm"]
	if not dlg then
		return
	end
	local data = core.cloud_get_messages(dlg.data.username)
	local cache_len = (data and data.messages) and #data.messages or 0
	local session_len = #dlg.data.session

	if cache_len ~= dlg.data.last_cache_len
			or session_len ~= dlg.data.last_session_len then
		dlg.data.last_cache_len = cache_len
		dlg.data.last_session_len = session_len
		dlg.data.loaded = true
		ui.update()
	end

	-- keep unread cleared while the conversation is open (rate limited)
	dm_mark_read_timer = dm_mark_read_timer + 1
	if dm_mark_read_timer >= 180 then -- ~3s at 60fps
		dm_mark_read_timer = 0
		core.cloud_mark_read(dlg.data.username)
	end
end

-- called every frame from the C++ main menu loop
function cloud_on_step()
	clear_notice()
	poll_join_progress()
	poll_dm_dialog()

	local evs = core.cloud_poll_events()
	if not evs then
		return
	end
	local dirty = false
	for _, ev in pairs(evs) do
		handle_event(ev)
		dirty = true
	end
	if dirty and ui.childlist["maintab"] then
		ui.update()
	end
end
