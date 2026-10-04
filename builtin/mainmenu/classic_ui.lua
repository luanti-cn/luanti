-- LuantiCN
-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Classic block-game navigation, using the existing tabs and their handlers.

classic_ui = {}

-- Logical GUI pixels, not inventory units. Keep the 320x240 minimum and use
-- integer raster scales, including when the window is resized or HiDPI changes.
function classic_ui.layout()
	local win = core.get_window_info and core.get_window_info() or {
		size = {x = 800, y = 600}, max_formspec_size = {x = 15, y = 11.25},
		real_gui_scaling = 1,
	}
	local fit = math.max(1, math.floor(math.min(win.size.x / 320, win.size.y / 240)))
	local preference = core.settings.get and tonumber(core.settings:get("gui_scaling")) or 1
	local scale = math.max(1, math.min(fit, math.floor(fit * (preference or 1))))
	local img = math.max(1, math.floor(win.size.x / win.max_formspec_size.x))
	local c = {w = win.size.x / scale, h = win.size.y / scale, scale = scale,
		unit = scale / img, font = math.max(1, math.floor(8 * scale / win.real_gui_scaling + 0.5))}
	function c:rect(kind, x, y, w, h, rest)
		return ("%s[%.8f,%.8f;%.8f,%.8f;%s]"):format(kind,
			x * self.unit, y * self.unit, w * self.unit, h * self.unit, rest)
	end
	function c:button(x, y, w, id, text, enabled)
		return "style[" .. id .. ";enabled=" .. tostring(enabled ~= false) .. "]" ..
			self:rect("button", x, y, w, 20, id .. ";" .. text)
	end
	function c:label(x, y, w, text)
		return self:rect("label", x, y, w, 10, text)
	end
	function c:header(text)
		return "style_type[label;halign=center]" .. self:label(0, 12, self.w, text) ..
			"style_type[label;halign=left]"
	end
	function c:formspec()
		-- Small epsilon keeps calculateImgsize from rounding below imgsize.
		return ("formspec_version[6]size[%.8f,%.8f]position[0,0]anchor[0,0]padding[0,0]bgcolor[;neither]")
			:format(win.size.x / img - 0.000001, win.size.y / img - 0.000001) ..
			classic_ui.styles() .. ("style_type[label,button,button_exit,image_button,field,pwdfield,textarea," ..
				"table,textlist,dropdown,checkbox;" ..
				"font_size=%d]"):format(self.font)
	end
	return c
end

-- Adapt existing fork-specific forms without changing their field IDs or
-- event handlers. Only numeric geometry is converted; text is never rewritten.
function classic_ui.legacy(content, c)
	local size_elements = {button = true, button_exit = true, button_url = true,
		button_url_exit = true, image_button = true, item_image_button = true, hypertext = true,
		image = true, animated_image = true, field = true, pwdfield = true,
		textarea = true, table = true, textlist = true, dropdown = true,
		box = true, scroll_container = true, scrollbar = true, label = true}
	local factor = 20 * c.unit
	return content:gsub("([%a_]+)%[([^%]]*)%]", function(kind, body)
		if not size_elements[kind] and kind ~= "container" and kind ~= "checkbox" then
			return kind .. "[" .. body .. "]"
		end
		local function pair(x, y)
			return ("%.8f,%.8f"):format(tonumber(x) * factor, tonumber(y) * factor)
		end
		body = body:gsub("^([%d%.%-]+),([%d%.%-]+)", pair, 1)
		if size_elements[kind] then
			body = body:gsub(";([%d%.%-]+),([%d%.%-]+);", function(x, y)
				return ";" .. pair(x, y) .. ";"
			end, 1)
		end
		return kind .. "[" .. body .. "]"
	end)
end

function classic_ui.account_prompt(message, pair_id, browser_id)
	-- Legacy translations contain a literal newline escape, already formspec-escaped.
	message = message:gsub("\\\\n", "\n")
	return "style_type[textarea;border=false;bgcolor=;textcolor=#FFFFFF]" ..
		"textarea[0.25,1.2;15,3.5;;;" .. message .. "]" ..
		"button[3.05,5;4.6,1;" .. pair_id .. ";" .. fgettext("Enter Pairing Code") .. "]" ..
		"button[7.85,5;4.6,1;" .. browser_id .. ";" .. fgettext("Open Website") .. "]"
end

function classic_ui.background(home)
	core.set_topleft_text("")
	core.set_clouds(false)
	for _, layer in ipairs({"overlay", "header", "footer"}) do
		core.set_background(layer, "")
	end
	core.set_background("background", defaulttexturedir ..
		(home and "classic_panorama.png" or "classic_dirt.png"), not home,
		32 * classic_ui.layout().scale)
end

function classic_ui.styles()
	return table.concat({
		"style_type[button,button_exit,image_button;bgcolor=;textcolor=#FFFFFF;content_offset=0,0]",
		"style_type[button:hovered,button_exit:hovered,button:focused,button_exit:focused;textcolor=#FFFFFF]",
		"style_type[field,pwdfield;border=true;bgcolor=#000000;textcolor=#FFFFFF]",
		"tableoptions[background=#00000000;border=false;highlight=#000000;highlight_text=#FFFFFF;highlight_border=#808080]",
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
		classic_pixels = true,
		caption = fgettext("Main menu"),
		cbf_formspec = function()
			local version = core.get_version()
			local c = classic_ui.layout()
			local x, y = c.w / 2 - 100, math.floor(c.h / 4) + 48
			return table.concat({
				c:rect("image", c.w / 2 - 137, 30, 274, 44,
					core.formspec_escape(defaulttexturedir .. "classic_logo.png")),
				c:rect("image", c.w / 2 - 64, 73, 128, 14,
					core.formspec_escape(defaulttexturedir .. "classic_edition.png")),
				c:rect("animated_image", c.w / 2 + 10, 38, 160, 64,
					"classic_splash;" .. core.formspec_escape(defaulttexturedir .. "classic_splash.png") .. ";16;63"),
				c:button(x, y, 200, "nav_local", fgettext("Singleplayer")),
				c:button(x, y + 24, 200, "nav_online", fgettext("Multiplayer")),
				c:button(x, y + 48, 200, "nav_community", fgettext("Cloud & Community")),
				c:button(x, y + 84, 98, "nav_options", fgettext("Options...")),
				c:button(x + 102, y + 84, 98, "classic_quit", fgettext("Quit Game")),
				("style[nav_language,nav_accessibility;padding=%d]"):format(2 * c.scale),
				c:rect("image_button", x - 24, y + 84, 20, 20,
					core.formspec_escape(defaulttexturedir .. "classic_language.png") .. ";nav_language;"),
				"tooltip[nav_language;" .. fgettext("Language") .. "]",
				c:rect("image_button", x + 204, y + 84, 20, 20,
					core.formspec_escape(defaulttexturedir .. "classic_accessibility.png") .. ";nav_accessibility;"),
				"tooltip[nav_accessibility;" .. fgettext("Accessibility") .. "]",
				c:label(2, c.h - 12, c.w - 4, core.formspec_escape("LuantiCN " .. version.string)),
				"style_type[label;halign=right]",
				c:label(2, c.h - 12, c.w - 4, fgettext("Free and open source")),
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
		classic_pixels = true,
		caption = fgettext("Cloud & Community"),
		cbf_formspec = function()
			local c = classic_ui.layout()
			local x, y = c.w / 2 - 100, 64
			return table.concat({
				"style_type[label;halign=center]", c:label(0, 36, c.w, fgettext("Play together. Make it yours.")),
				c:button(x, y, 200, "nav_cloud", fgettext("Cloud Sync")),
				c:button(x, y + 24, 98, "nav_cloud_friends", fgettext("Friends")),
				c:button(x + 102, y + 24, 98, "nav_cloud_party", fgettext("Party")),
				c:button(x, y + 48, 200, "nav_cloud_dm", fgettext("Direct Messages")),
				c:button(x, y + 84, 98, "nav_content", fgettext("Content")),
				c:button(x + 102, y + 84, 98, "nav_about", fgettext("About")),
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
		classic_pixels = true,
		caption = fgettext("Options..."),
		cbf_formspec = function()
			local c = classic_ui.layout()
			local fs = {}
			for i, category in ipairs(categories) do
				local x = c.w / 2 + (i % 2 == 1 and -155 or 5)
				local y = c.h / 6 + 24 + math.floor((i - 1) / 2) * 24
				fs[#fs + 1] = c:button(x, y, 150, "settings_" .. category[1], fgettext(category[3]))
			end
			fs[#fs + 1] = c:button(c.w / 2 - 100, c.h / 6 + 108, 200, "settings_all", fgettext("All Settings..."))
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
		local c = classic_ui.layout()
		classic_ui.background(tab.name == "home")
		local content = tab.get_formspec(self, tab.name, tab.tabdata, tab.tabsize)
		local fs = c:formspec()
		if tab.name == "home" then
			return fs .. content
		end
		local caption = type(tab.caption) == "function" and tab.caption(self) or tab.caption
		fs = fs .. c:header(caption)
		if tab.name == "online" and not tab.tabdata.show_connect then
			fs = fs .. c:rect("box", 0, 54, c.w, c.h - 118, "#00000090")
		end
		if tab.classic_pixels then
			fs = fs .. content
		else
			fs = fs .. ("container[%.8f,%.8f]"):format((c.w - 310) / 2 * c.unit, 32 * c.unit) ..
				classic_ui.legacy(content, c) .. "container_end[]"
		end
		if tab.name ~= "local" then
			fs = fs .. c:button(c.w / 2 - 100, c.h - 28, 200, "classic_back", fgettext("Done"))
		end
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
