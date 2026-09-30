-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

local function status_line(tabdata)
	if tabdata.checking then
		return {"label[0.5,5.9;", fgettext("Connecting to cloud sync service..."), "]"}
	end
	if tabdata.error then
		return {
			"box[0.5,5.6;8,0.6;#600]",
			"label[0.75,5.9;", core.formspec_escape(tabdata.error), "]",
		}
	end
	if tabdata.confirm_logout then
		return {"label[0.5,5.9;", fgettext("Log out? Click \"Log Out\" again to confirm"), "]"}
	end
	return {}
end

return {
	name = "cloud",
	caption = fgettext("Cloud Sync"),

	cbf_formspec = function(tabview, name, tabdata)
		local info = cloud_store.info
		local fs = {
			"label[0.5,1;", fgettext("Cloud Sync"), "]",
		}

		if not info then
			table.insert_all(fs, {
				"label[0.5,1.7;",
				fgettext("After pairing with a luanti.cn account:\\n· Server credentials are fetched/created automatically, no manual registration\\n· Characters are managed in the cloud and survive device changes\\n· See friends' online status and join their server in one click"), "]",
				"button[0.5,4.3;3,0.8;cloud_browser;", fgettext("Open Website"), "]",
				"button[4,4.3;3,0.8;cloud_pair;", fgettext("Enter Pairing Code"), "]",
			})
			return table.concat(fs)
		end

		table.insert_all(fs, {
			"label[0.5,2.3;", fgettext("Site account: $1",
					info.siteUsername or "?"), "]",
			"label[0.5,2.9;", fgettext("Nickname: $1",
					info.displayName or info.siteUsername or "?"), "]",
			"label[0.5,3.5;", fgettext("This device: $1",
					info.deviceName or "?"), "]",
			"label[0.5,4.1;", fgettext("Default in-game name: $1",
					info.defaultServerUsername or "?"), "]",
			"button[0.5,5;2.6,0.8;cloud_refresh;", fgettext("Refresh Identity"), "]",
			"button[3.3,5;2.6,0.8;cloud_browser;", fgettext("Open Website"), "]",
			"button[6.1,5;2.6,0.8;cloud_logout;", fgettext("Log Out"), "]",
		})
		table.insert_all(fs, status_line(tabdata))

		return table.concat(fs)
	end,

	cbf_button_handler = function(this, fields, name, tabdata)
		tabdata.error = nil

		if fields.cloud_pair then
			local dlg = create_cloud_pair_dialog()
			dlg:set_parent(this)
			this:hide()
			dlg:show()
			return true
		end

		if fields.cloud_browser then
			core.open_url(cloud_api.site_url())
			return true
		end

		if fields.cloud_refresh then
			if not tabdata.checking then
				tabdata.checking = true
				ui.update()
				cloud_api.me(function(result)
					tabdata.checking = false
					if result.success and cloud_store.info then
						local info = cloud_store.info
						info.siteUsername = result.data.user.username
						info.displayName = result.data.user.display_name
						info.defaultServerUsername =
								result.data.user.default_server_username
						if type(result.data.device) == "table"
								and result.data.device.name then
							info.deviceName = result.data.device.name
						end
						cloud_store.save(info)
					elseif not result.success then
						tabdata.error = result.error
					end
					ui.update()
				end)
			end
			return true
		end

		if fields.cloud_logout then
			if tabdata.confirm_logout then
				tabdata.confirm_logout = nil
				cloud_store.clear()
				core.cloud_reload_auth() -- notify the C++ cloud service
			else
				tabdata.confirm_logout = true
			end
			ui.update()
			return true
		end

		return false
	end,
}
