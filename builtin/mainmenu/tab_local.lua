-- Luanti
-- Copyright (C) 2014 sapier
-- SPDX-License-Identifier: LGPL-2.1-or-later


local current_game, main_button_handler
local valid_disabled_settings = {
	["enable_damage"]=true,
	["creative_mode"]=true,
	["enable_server"]=true,
}

-- Name and port stored to persist when updating the formspec
local current_name = core.settings:get("name")
local current_port = core.settings:get("port")
local current_password = ""

local function selected_world_index()
	local index = core.get_textlist_index("sp_worlds")
	if index and index > 0 then return index end
	return filterlist.get_current_index(menudata.worldlist,
		tonumber(core.settings:get("mainmenu_last_selected_world")))
end

-- Currently chosen game in gamebar for theming and filtering
function current_game()
	local gameid = core.settings:get("menu_last_game")
	local game = gameid and pkgmgr.find_by_gameid(gameid)
	-- Fall back to first game installed if one exists.
	if not game and #pkgmgr.games > 0 then

		-- If devtest is the first game in the list and there is another
		-- game available, pick the other game instead.
		local picked_game
		if pkgmgr.games[1].id == "devtest" and #pkgmgr.games > 1 then
			picked_game = 2
		else
			picked_game = 1
		end

		game = pkgmgr.games[picked_game]
		gameid = game.id
		core.settings:set("menu_last_game", gameid)
	end

	return game
end

-- Apply menu changes from given game
function apply_game(game)
	core.settings:set("menu_last_game", game.id)
	menudata.worldlist:set_filtercriteria(game.id)

	mm_game_theme.set_game(game)

	local index = filterlist.get_current_index(menudata.worldlist,
		tonumber(core.settings:get("mainmenu_last_selected_world")))
	if not index or index < 1 then
		local selected = selected_world_index()
		if selected ~= nil and selected < #menudata.worldlist:get_list() then
			index = selected
		else
			index = #menudata.worldlist:get_list()
		end
	end
	menu_worldmt_legacy(index)
end

local function get_disabled_settings(game)
	if not game then
		return {}
	end

	local gameconfig = Settings(game.path .. "/game.conf")
	local disabled_settings = {}
	if gameconfig then
		local disabled_settings_str = (gameconfig:get("disabled_settings") or ""):split()
		for _, value in pairs(disabled_settings_str) do
			local state = false
			value = value:trim()
			if string.sub(value, 1, 1) == "!" then
				state = true
				value = string.sub(value, 2)
			end
			if valid_disabled_settings[value] then
				disabled_settings[value] = state
			else
				core.log("error", "Invalid disabled setting in game.conf: "..tostring(value))
			end
		end
	end
	return disabled_settings
end

local function get_formspec(tabview, name, tabdata)
	local c = classic_ui.layout()
	local x = c.w / 2 - 155
	if #pkgmgr.games == 0 then
		return "style_type[label;halign=center]" ..
			c:label(0, 80, c.w, fgettext("Install a game to start your adventure")) ..
			c:button(c.w / 2 - 100, 112, 200, "game_open_cdb", fgettext("Install a game")) ..
			c:button(c.w / 2 - 100, c.h - 28, 200, "classic_back", fgettext("Cancel"))
	end

	local games, game_index = {}, 1
	local chosen = current_game()
	for i, game in ipairs(pkgmgr.games) do
		games[i] = core.formspec_escape(game.title)
		if game.id == chosen.id then game_index = i end
	end
	local index = filterlist.get_current_index(menudata.worldlist,
		tonumber(core.settings:get("mainmenu_last_selected_world"))) or 0
	local rows, image_options = {}, {}
	for i, world in ipairs(menudata.worldlist:get_list()) do
		local game = pkgmgr.find_by_gameid(world.gameid)
		-- Use a world's optional icon without ever requiring one.
		local icon = defaulttexturedir .. "classic_world.png"
		for _, filename in ipairs(core.get_dir_list(world.path, false)) do
			if filename == "icon.png" then icon = world.path .. DIR_DELIM .. filename end
		end
		image_options[#image_options + 1] = i .. "=" .. core.formspec_escape(icon)
		rows[#rows + 1] = tostring(i)
		rows[#rows + 1] = core.formspec_escape(world.name .. "\n" ..
			(game and game.title or world.gameid))
	end
	local valid_selection = index > 0 and #rows > 0
	local fs = {
		c:rect("dropdown", x + 5, 30, 300, 20, "classic_game;" .. table.concat(games, ",") .. ";" .. game_index .. ";true"),
		c:rect("box", 0, 54, c.w, c.h - 118, "#00000090"),
		"tableoptions[rowheight=3.5;background=#00000000;border=false;highlight=#000000;highlight_border=#808080]",
		"tablecolumns[image,padding=0.3", #image_options > 0 and "," or "",
			table.concat(image_options, ","), ";text,padding=1]",
		c:rect("table", x + 2, 56, 306, c.h - 122, "sp_worlds;" .. table.concat(rows, ",") .. ";" .. index),
		c:button(x + 5, c.h - 52, 150, "play", fgettext("Play Selected World"), valid_selection),
		c:button(x + 160, c.h - 52, 150, "world_create", fgettext("Create New World")),
		c:button(x + 5, c.h - 28, 72, "world_configure", fgettext("Select Mods"), valid_selection),
		c:button(x + 82, c.h - 28, 72, "world_options", fgettext("World Options..."), valid_selection),
		c:button(x + 159, c.h - 28, 72, "world_delete", fgettext("Delete"), valid_selection),
		c:button(x + 236, c.h - 28, 72, "classic_back", fgettext("Cancel")),
	}
	if #rows == 0 then
		fs[#fs + 1] = "style_type[label;halign=center]" .. c:label(0, 80, c.w,
			fgettext("No worlds yet. Create your first world!"))
	end
	return table.concat(fs)
end

local function world_options_formspec()
	local selected = selected_world_index()
	local world = selected and menudata.worldlist:get_list()[selected]
	local disabled = get_disabled_settings(world and pkgmgr.find_by_gameid(world.gameid) or current_game())
	local fs = {"formspec_version[6]size[10,8]bgcolor[;neither]",
		"style_type[label;halign=center]label[0,0.2;10,0.8;" .. fgettext("World Options...") .. "]",
		"style_type[label;halign=left]"}
	for i, entry in ipairs({{"creative_mode", "cb_creative_mode", "Creative Mode"},
		{"enable_damage", "cb_enable_damage", "Enable Damage"},
		{"enable_server", "cb_server", "Host Server"}}) do
		if disabled[entry[1]] == nil then
			fs[#fs + 1] = ("checkbox[1,%f;%s;%s;%s]"):format(0.9 + i * 0.6,
				entry[2], fgettext(entry[3]), dump(core.settings:get_bool(entry[1])))
		end
	end
	if core.settings:get_bool("enable_server") and disabled.enable_server == nil then
		fs[#fs + 1] = "checkbox[5.1,1.5;cb_server_announce;" .. fgettext("Announce Server") ..
			";" .. dump(core.settings:get_bool("server_announce")) .. "]"
		if cloud_store.info then
			fs[#fs + 1] = "checkbox[5.1,2.1;cb_cloud_host;" .. fgettext("Allow friends to join (cloud)") ..
				";" .. dump(core.settings:get_bool("cloud_host_game")) .. "]"
		end
		fs[#fs + 1] = "field[1,3.3;3.9,0.75;te_playername;" .. fgettext("Name") ..
			";" .. core.formspec_escape(current_name or "") .. "]field_close_on_enter[te_playername;false]"
		fs[#fs + 1] = "pwdfield[5.1,3.3;3.9,0.75;te_passwd;" .. fgettext("Password") .. "]"
		fs[#fs + 1] = "field_close_on_enter[te_passwd;false]"
		fs[#fs + 1] = "field[1,4.8;3.9,0.75;te_serverport;" .. fgettext("Server Port") ..
			";" .. core.formspec_escape(current_port or "30000") .. "]field_close_on_enter[te_serverport;false]"
		fs[#fs + 1] = "field[5.1,4.8;3.9,0.75;te_serveraddr;" .. fgettext("Bind Address") ..
			";" .. core.formspec_escape(core.settings:get("bind_address") or "") .. "]field_close_on_enter[te_serveraddr;false]"
	end
	fs[#fs + 1] = "button[1,6.8;8,0.8;world_options_done;" .. fgettext("Done") .. "]"
	return table.concat(fs)
end

main_button_handler = function(this, fields, name, tabdata)

	assert(name == "local")

	if fields.classic_game then
		local game = pkgmgr.games[tonumber(fields.classic_game)]
		if game and game.id ~= current_game().id then
			apply_game(game)
			core.settings:set("mainmenu_last_selected_world", menudata.worldlist:get_raw_index(1))
			menu_worldmt_legacy(1)
			return true
		end
	end
	if fields.te_passwd then current_password = fields.te_passwd end
	if fields.te_serveraddr then core.settings:set("bind_address", fields.te_serveraddr) end
	if fields.world_options then
		local selected = selected_world_index()
		if not selected or selected < 1 then return true end
		local dlg = dialog_create("world_options", world_options_formspec, function(dialog, values)
			values.key_enter = nil
			main_button_handler(this, values, name, tabdata)
			if values.world_options_done then dialog:delete() end
			return true
		end)
		dlg:set_parent(this)
		this:hide()
		dlg:show()
		return true
	end


	if fields.game_open_cdb then
		local maintab = ui.find_by_name("maintab")
		local dlg = create_contentdb_dlg("game")
		dlg:set_parent(maintab)
		maintab:hide()
		dlg:show()
		return true
	end

	if this.dlg_create_world_closed_at == nil then
		this.dlg_create_world_closed_at = 0
	end

	local world_doubleclick = false

	if fields["te_playername"] then
		current_name = fields["te_playername"]
	end

	if fields["te_serverport"] then
		current_port = fields["te_serverport"]
	end

	if fields["sp_worlds"] ~= nil then
		local event = core.explode_table_event(fields["sp_worlds"])
		local selected = selected_world_index()

		menu_worldmt_legacy(selected)

		if event.type == "DCL" then
			world_doubleclick = true
		end

		if event.type == "CHG" and selected ~= nil then
			core.settings:set("mainmenu_last_selected_world",
				menudata.worldlist:get_raw_index(selected))
			return true
		end
	end

	if menu_handle_key_up_down(fields,"sp_worlds","mainmenu_last_selected_world") then
		return true
	end

	if fields["cb_creative_mode"] then
		core.settings:set("creative_mode", fields["cb_creative_mode"])
		local selected = selected_world_index()
		menu_worldmt(selected, "creative_mode", fields["cb_creative_mode"])

		return true
	end

	if fields["cb_enable_damage"] then
		core.settings:set("enable_damage", fields["cb_enable_damage"])
		local selected = selected_world_index()
		menu_worldmt(selected, "enable_damage", fields["cb_enable_damage"])

		return true
	end

	if fields["cb_server"] then
		core.settings:set("enable_server", fields["cb_server"])

		return true
	end

	if fields["cb_server_announce"] then
		core.settings:set("server_announce", fields["cb_server_announce"])
		local selected = selected_world_index()
		menu_worldmt(selected, "server_announce", fields["cb_server_announce"])

		return true
	end

	if fields["cb_cloud_host"] then
		core.settings:set("cloud_host_game", fields["cb_cloud_host"])
		return true
	end

	if fields["play"] ~= nil or world_doubleclick or fields["key_enter"] then
		local enter_key_duration = core.get_us_time() - this.dlg_create_world_closed_at
		if world_doubleclick and enter_key_duration <= 200000 then -- 200 ms
			this.dlg_create_world_closed_at = 0
			return true
		end

		local selected = selected_world_index()
		gamedata.selected_world = menudata.worldlist:get_raw_index(selected)

		if selected == nil or gamedata.selected_world == 0 then
			return true
		end

		-- Update last game
		local world = menudata.worldlist:get_raw_element(gamedata.selected_world)
		local game_obj
		if world then
			game_obj = pkgmgr.find_by_gameid(world.gameid)
			core.settings:set("menu_last_game", game_obj.id)
		end

		local disabled_settings = get_disabled_settings(game_obj)
		for k, _ in pairs(valid_disabled_settings) do
			local v = disabled_settings[k]
			if v ~= nil then
				if k == "enable_server" and v == true then
					error("Setting 'enable_server' cannot be force-enabled! The game.conf needs to be fixed.")
				end
				core.settings:set_bool(k, disabled_settings[k])
			end
		end

		if core.settings:get_bool("enable_server") then
			gamedata.mode       = "host"
			gamedata.playername = fields["te_playername"] or current_name
			gamedata.password   = fields["te_passwd"] or current_password
			gamedata.port       = fields["te_serverport"] or current_port
			gamedata.address    = ""

			core.settings:set("port",gamedata.port)
			if fields["te_serveraddr"] ~= nil then
				core.settings:set("bind_address",fields["te_serveraddr"])
			end
		else
			gamedata.mode = "singleplayer"
		end

		core.start()
		return true
	end

	if fields["world_create"] ~= nil then
		this.dlg_create_world_closed_at = 0
		local create_world_dlg = create_create_world_dlg()
		create_world_dlg:set_parent(this)
		this:hide()
		create_world_dlg:show()
		return true
	end

	if fields["world_delete"] ~= nil then
		local selected = selected_world_index()
		if selected ~= nil and
			selected <= menudata.worldlist:size() then
			local world = menudata.worldlist:get_list()[selected]
			if world ~= nil and
				world.name ~= nil and
				world.name ~= "" then
				local index = menudata.worldlist:get_raw_index(selected)
				local delete_world_dlg = create_delete_world_dlg(world.name,index)
				delete_world_dlg:set_parent(this)
				this:hide()
				delete_world_dlg:show()
			end
		end

		return true
	end

	if fields["world_configure"] ~= nil then
		local selected = selected_world_index()
		if selected ~= nil then
			local configdialog =
				create_configure_world_dlg(
						menudata.worldlist:get_raw_index(selected))

			if (configdialog ~= nil) then
				configdialog:set_parent(this)
				this:hide()
				configdialog:show()
			end
		end

		return true
	end
end

local function on_change(type, old_tab, new_tab)
	if type == "ENTER" then
		local game = current_game()
		if game then
			apply_game(game)
		else
			mm_game_theme.set_engine()
		end

	elseif type == "LEAVE" then
		if new_tab then menudata.worldlist:set_filtercriteria(nil) end
	end
end

--------------------------------------------------------------------------------
return {
	name = "local",
	classic_pixels = true,
	caption = fgettext("Select World"),
	cbf_formspec = get_formspec,
	cbf_button_handler = main_button_handler,
	on_change = on_change
}
