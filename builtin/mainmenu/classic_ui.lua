-- LuantiCN
-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Classic block-game navigation, using the existing tabs and their handlers.

classic_ui = {}

local function centered(y, text, width)
	width = width or MAIN_TAB_W
	return "style_type[label;halign=center;valign=center]" ..
		("label[0,%f;%f,0.6;%s]"):format(y, width, text) ..
		"style_type[label;halign=left]"
end

function classic_ui.background(home)
	core.set_topleft_text("")
	core.set_clouds(false)
	for _, layer in ipairs({"overlay", "header", "footer"}) do
		core.set_background(layer, "")
	end
	core.set_background("background", defaulttexturedir ..
		(home and "classic_panorama.png" or "classic_dirt.png"), not home, 64)
end

function classic_ui.styles()
	return table.concat({
		"style_type[button,button_exit;bgcolor=#747474;textcolor=#FFFFFF]",
		"style_type[button:hovered,button_exit:hovered;bgcolor=#7C80A4;textcolor=#FFFFA0]",
		"style_type[button:focused,button_exit:focused;bgcolor=#7C80A4;textcolor=#FFFFA0]",
		"style_type[button:pressed,button_exit:pressed;bgcolor=#50506C]",
		"style_type[field,pwdfield;border=true;bgcolor=#000000;textcolor=#FFFFFF]",
		"tableoptions[background=#171310;border=false;highlight=#555555;highlight_text=#FFFFFF]",
		"listcolors[#8B8B8B;#C5C5C5;#100010F0;#FFFFFF;#FFFFFF]",
	})
end

local function open_settings(parent, page)
	local dlg = create_settings_dlg(page)
	dlg:set_parent(parent)
	parent:hide()
	classic_ui.background(false)
	dlg:show()
end

function classic_ui.home_tab()
	return {
		name = "home",
		caption = fgettext("Main menu"),
		cbf_formspec = function()
			local version = core.get_version()
			return table.concat({
				"image[2.1,0.25;11.3,2.25;", core.formspec_escape(defaulttexturedir .. "classic_logo.png"), "]",
				"animated_image[9.25,1.45;4.35,1.15;classic_splash;",
					core.formspec_escape(defaulttexturedir .. "classic_splash.png"), ";12;90]",
				"button[3.75,3.25;8,0.8;nav_local;", fgettext("Singleplayer"), "]",
				"button[3.75,4.25;8,0.8;nav_online;", fgettext("Multiplayer"), "]",
				"button[3.75,5.25;8,0.8;nav_community;", fgettext("Cloud & Community"), "]",
				"button[3.75,6.7;3.9,0.8;nav_options;", fgettext("Options..."), "]",
				"button[7.85,6.7;3.9,0.8;classic_quit;", fgettext("Quit Game"), "]",
				"button[2.75,6.7;0.8,0.8;nav_language;@]",
				"tooltip[nav_language;", fgettext("Language"), "]",
				"button[11.95,6.7;0.8,0.8;nav_accessibility;A]",
				"tooltip[nav_accessibility;", fgettext("Accessibility"), "]",
				"style_type[label;halign=left;font_size=14]",
				"label[0.1,8.35;", core.formspec_escape("LuantiCN " .. version.string), "]",
				"style_type[label;halign=right]",
				"label[9.5,8.05;5.9,0.6;", fgettext("Free and open source"), "]",
			})
		end,
		on_change = function(event)
			if event == "ENTER" then
				mm_game_theme.stop_music()
				classic_ui.background(true)
			end
		end,
	}
end

function classic_ui.community_tab()
	return {
		name = "community",
		caption = fgettext("Cloud & Community"),
		cbf_formspec = function()
			return table.concat({
				centered(0.35, fgettext("Play together. Make it yours.")),
				"button[3.75,1.5;8,0.8;nav_cloud;", fgettext("Cloud Sync"), "]",
				"button[3.75,2.5;3.9,0.8;nav_cloud_friends;", fgettext("Friends"), "]",
				"button[7.85,2.5;3.9,0.8;nav_cloud_party;", fgettext("Party"), "]",
				"button[3.75,3.5;8,0.8;nav_cloud_dm;", fgettext("Direct Messages"), "]",
				"button[3.75,4.95;3.9,0.8;nav_content;", fgettext("Content"), "]",
				"button[7.85,4.95;3.9,0.8;nav_about;", fgettext("About"), "]",
			})
		end,
	}
end

function classic_ui.options_tab()
	local categories = {
		{"audio", "graphics_and_audio_audio", "Sound & Music..."},
		{"graphics", "graphics_and_audio_graphics", "Video Settings..."},
		{"controls", "controls_keyboard_and_mouse", "Controls..."},
		{"client_and_server", "client_and_server_client", "Client & Server..."},
		{"ui", "graphics_and_audio_user_interfaces", "User Interface..."},
		{"accessibility", "accessibility", "Accessibility..."},
	}
	return {
		name = "options",
		caption = fgettext("Options..."),
		cbf_formspec = function()
			local fs = {centered(0.35, fgettext("Customize your game"))}
			for i, category in ipairs(categories) do
				local x = i % 2 == 1 and 1.65 or 7.85
				local y = 1.5 + math.floor((i - 1) / 2) * 1.1
				fs[#fs + 1] = ("button[%f,%f;6,0.8;settings_%s;%s]"):format(
					x, y, category[1], fgettext(category[3]))
			end
			fs[#fs + 1] = "button[3.75,5.25;8,0.8;settings_all;" .. fgettext("All Settings...") .. "]"
			return table.concat(fs)
		end,
		cbf_button_handler = function(parent, fields)
			for _, category in ipairs(categories) do
				if fields["settings_" .. category[1]] then
					open_settings(parent, category[2])
					return true
				end
			end
			if fields.settings_all then
				open_settings(parent)
				return true
			end
		end,
	}
end

function classic_ui.install(tabview)
	-- Keep tab lifecycle callbacks (game filters, gamebar, async cloud updates)
	-- intact. Only the shell and navigation change, not the tab data contracts.
	function tabview:get_formspec()
		if self.hidden or (self.parent and self.parent.hidden) then
			return ""
		end
		local tab = self.tablist[self.last_tab_index]
		classic_ui.background(tab.name == "home")
		local content = tab.get_formspec(self, tab.name, tab.tabdata, tab.tabsize)
		local fs = "formspec_version[6]size[15.5,9]padding[0.025,0.025]bgcolor[;neither]"
		fs = fs .. classic_ui.styles()
		if tab.name == "home" then
			return fs .. content
		end
		local caption = type(tab.caption) == "function" and tab.caption(self) or tab.caption
		fs = fs .. "style_type[label;halign=center;valign=center]" .. centered(0.05, caption)
		fs = fs .. "style_type[label;halign=left]"
		fs = fs .. "container[0,0.85]" .. content .. "container_end[]"
		fs = fs .. "button[3.75,8.1;8,0.8;classic_back;" ..
			(tab.name == "local" and fgettext("Cancel") or fgettext("Done")) .. "]"
		return fs
	end

	tabview:set_global_button_handler(function(self, fields)
		if fields.classic_back then
			local tab = self.tablist[self.last_tab_index]
			if tab.name == "online" and tab.tabdata.show_connect then
				tab.tabdata.show_connect = false
				return true
			end
			self:set_tab((self.current_tab == "community" or self.current_tab == "options" or
				self.current_tab == "local" or self.current_tab == "online") and "home" or "community")
			return true
		end
		if fields.classic_quit then
			core.close()
			return true
		end
		if fields.nav_language then
			open_settings(self, "accessibility")
			return true
		end
		if fields.nav_accessibility then
			open_settings(self, "accessibility")
			return true
		end
		for _, tab in ipairs(self.tablist) do
			if fields["nav_" .. tab.name] then
				self:set_tab(tab.name)
				return true
			end
		end
	end)
end
