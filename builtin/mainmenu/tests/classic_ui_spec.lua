-- SPDX-License-Identifier: LGPL-2.1-or-later

-- Restore globals so this suite can run alongside the existing builtin specs.
local function setup_globals(setup)
	local names = {
		"MAIN_TAB_W", "MAIN_TAB_H", "defaulttexturedir", "fgettext", "core",
		"mm_game_theme", "ui", "create_settings_dlg", "tabview_create", "classic_ui",
		"serverlistmgr", "is_server_protocol_compat_or_error", "gamedata",
		"cloud_join", "cloud_store", "pkgmgr", "menudata", "filterlist",
		"menu_worldmt_legacy", "menu_handle_key_up_down", "create_create_world_dlg", "apply_game",
	}
	local saved
	before_each(function()
		saved = {}
		for _, name in ipairs(names) do saved[name] = {_G[name]} end
		setup()
	end)
	after_each(function()
		for name, value in pairs(saved) do _G[name] = value[1] end
	end)
end

describe("classic navigation", function()
	local view, transitions, settings_page
	setup_globals(function()
		_G.MAIN_TAB_W, _G.MAIN_TAB_H = 15.5, 7.1
		_G.defaulttexturedir = "textures/"
		_G.fgettext = function(text) return text end
		_G.core = {
			set_topleft_text = function() end,
			set_clouds = function() end,
			set_background = function() end,
			formspec_escape = function(text) return text end,
			get_version = function() return {string = "test"} end,
			settings = {set = function() end},
		}
		_G.mm_game_theme = {stop_music = function() end}
		_G.ui = {add = function() end}
		_G.create_settings_dlg = function(page)
			settings_page = page
			return {set_parent = function() end, show = function() end}
		end
		dofile("builtin/fstk/tabview.lua")
		dofile("builtin/mainmenu/classic_ui.lua")
		view = tabview_create("maintab", {x = 15.5, y = 7.1}, {x = 0, y = 0})
		view:add(classic_ui.home_tab())
		view:add(classic_ui.community_tab())
		view:add(classic_ui.options_tab())
		transitions = {}
		for _, name in ipairs({"local", "online", "cloud", "cloud_friends", "cloud_dm", "cloud_party", "content", "about"}) do
			view:add({name = name, caption = name, cbf_formspec = function() return "" end,
				on_change = function(event, from, to)
					transitions[#transitions + 1] = {event, from, to}
				end})
		end
		classic_ui.install(view)
		view:show()
	end)

	it("starts on the title screen and preserves tab lifecycle callbacks", function()
		assert.equal("home", view.current_tab)
		assert.is_true(view:handle_buttons({nav_local = true}))
		assert.equal("local", view.current_tab)
		assert.same({"ENTER", "home", "local"}, transitions[1])
		view:handle_buttons({classic_back = true})
		assert.equal("home", view.current_tab)
		assert.same({"LEAVE", "local", "home"}, transitions[2])
	end)

	it("keeps every fork-specific destination reachable and returns through community", function()
		for _, name in ipairs({"cloud", "cloud_friends", "cloud_dm", "cloud_party", "content", "about"}) do
			view:handle_buttons({nav_community = true})
			view:handle_buttons({["nav_" .. name] = true})
			assert.equal(name, view.current_tab)
			view:handle_buttons({classic_back = true})
			assert.equal("community", view.current_tab)
		end
	end)

	it("does not redraw a hidden parent over its child dialog", function()
		view:handle_buttons({nav_options = true})
		view:handle_buttons({settings_audio = true})
		assert.equal("graphics_and_audio_audio", settings_page)
		assert.equal("", view:get_formspec())
		view:show()
		assert.equal("options", view.current_tab)
		assert.is_not.equal("", view:get_formspec())
	end)

	it("wraps legacy cloud messages with real line breaks", function()
		local fs = classic_ui.account_prompt("First\\\\nSecond\\;value", "pair", "browser")
		assert.is_truthy(fs:find("First\nSecond\\;value", 1, true))
		assert.is_falsy(fs:find("\\\\n", 1, true))
	end)

	it("returns from connection details to the server list before the title screen", function()
		view:handle_buttons({nav_online = true})
		view.tablist[view.last_tab_index].tabdata.show_connect = true
		view:handle_buttons({classic_back = true})
		assert.equal("online", view.current_tab)
		assert.is_false(view.tablist[view.last_tab_index].tabdata.show_connect)
		view:handle_buttons({classic_back = true})
		assert.equal("home", view.current_tab)
	end)

	it("keeps the title controls and corner text in the viewport at different raster scales", function()
		for _, size in ipairs({{1280, 720, 3}, {800, 600, 2}, {1280, 600, 2}, {1920, 1080, 4}}) do
			core.get_window_info = function()
				return {size = {x = size[1], y = size[2]},
					max_formspec_size = {x = size[1] / 53.328, y = size[2] / 53.328},
					real_gui_scaling = 1}
			end
			local c = classic_ui.layout()
			assert.equal(size[3], c.scale)
			local fs = view:get_formspec()
			assert.is_truthy(fs:find("position[0,0]anchor[0,0]", 1, true))
			assert.is_falsy(fs:find("nav_language;@", 1, true))
			assert.is_falsy(fs:find("nav_accessibility;A]", 1, true))
			for x, y, w, h in fs:gmatch("button%[([%d%.%-]+),([%d%.%-]+);([%d%.%-]+),([%d%.%-]+);") do
				assert.is_true(tonumber(x) >= 0 and tonumber(y) >= 0)
				assert.is_true((tonumber(x) + tonumber(w)) / c.unit <= c.w)
				assert.is_true((tonumber(y) + tonumber(h)) / c.unit <= c.h)
			end
		end
	end)

	it("converts legacy geometry without changing escaped labels or submitted field values", function()
		local c = classic_ui.layout()
		local fs = classic_ui.legacy("field[1,2;3,1;test;;a\\]b\\;1,2]" ..
			"table[1,2;3,4;servers;0,1,2;3]style[test;enabled=false]", c)
		assert.is_truthy(fs:find(";test;;a\\]b\\;1,2]", 1, true))
		assert.is_truthy(fs:find(";servers;0,1,2;3]", 1, true))
		assert.is_truthy(fs:find("style[test;enabled=false]", 1, true))
	end)
end)

describe("classic multiplayer actions", function()
	local tab, data, favorite, checked_protocol, started
	setup_globals(function()
		_G.fgettext = function(text) return text end
		local selected = {address = "localhost", port = 30000, proto_min = 99, proto_max = 100}
		local values = {address = selected.address, remote_port = "30000"}
		favorite, checked_protocol, started = nil, nil, false
		_G.core = {
			get_table_index = function() return nil end,
			start = function() started = true end,
			settings = {
				get = function(_, key) return values[key] end,
				set = function(_, key, value) values[key] = value end,
				get_bool = function() return false end,
			},
		}
		_G.serverlistmgr = {
			servers = {selected},
			get_favorites = function() return {selected} end,
			add_favorite = function(server) favorite = server end,
			delete_favorite = function(server) favorite = server end,
		}
		_G.is_server_protocol_compat_or_error = function(min, max)
			checked_protocol = {min, max}
			return false
		end
		_G.gamedata = {}
		_G.cloud_join = nil
		_G.cloud_store = {}
		tab = dofile("builtin/mainmenu/tab_online.lua")
		data = {lookup = {}}
	end)

	it("accepts list events without connection fields", function()
		assert.is_false(tab.cbf_button_handler({}, {te_search = ""}, "online", data))
		assert.is_true(tab.cbf_button_handler({}, {btn_show_connect = true}, "online", data))
		assert.is_true(data.show_connect)
	end)

	it("removes the selected favorite when the table is hidden", function()
		assert.is_true(tab.cbf_button_handler({}, {btn_delete_favorite = true}, "online", data))
		assert.equal("localhost", favorite.address)
	end)

	it("retains protocol validation when joining from connection details", function()
		assert.is_true(tab.cbf_button_handler({}, {
			btn_mp_login = true, te_address = "localhost", te_port = "30000",
			te_name = "player", te_pwd = "",
		}, "online", data))
		assert.same({99, 100}, checked_protocol)
		assert.equal("localhost", favorite.address)
		assert.is_false(started)
	end)
end)

describe("classic world actions", function()
	local tab, created, changes, values
	setup_globals(function()
		values = {menu_last_game = "test", mainmenu_last_selected_world = "1"}
		created, changes = false, {}
		_G.fgettext = function(text) return text end
		_G.core = {
			get_textlist_index = function() return 1 end,
			settings = {
				get = function(_, key) return values[key] end,
				set = function(_, key, value) values[key] = value end,
			},
		}
		_G.pkgmgr = {games = {{id = "test"}}, find_by_gameid = function() return {id = "test"} end}
		_G.menudata = {worldlist = {get_current_index = function() return 1 end,
			set_filtercriteria = function(_, value) changes[#changes + 1] = value end}}
		_G.filterlist = {get_current_index = function() return 1 end}
		_G.mm_game_theme = {set_game = function() end}
		_G.menu_worldmt_legacy = function() end
		_G.menu_handle_key_up_down = function() return false end
		_G.create_create_world_dlg = function()
			created = true
			return {set_parent = function() end, show = function() end}
		end
		tab = dofile("builtin/mainmenu/tab_local.lua")
	end)

	it("does not swallow button actions when the unchanged dropdown is submitted", function()
		local view = {hide = function() end}
		assert.is_true(tab.cbf_button_handler(view, {classic_game = "1", world_create = true}, "local", {}))
		assert.is_true(created)
	end)

	it("retains the game filter while a world dialog is open", function()
		tab.on_change("LEAVE", "local", nil)
		assert.equal(0, #changes)
	end)
end)
