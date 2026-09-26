-- Luanti
-- SPDX-License-Identifier: LGPL-2.1-or-later

local function normalize_code(input)
	if not input then
		return nil
	end
	local code = input:upper():gsub("[^%w]", "")
	if #code ~= 8 or not code:match("^%w+$") then
		return nil
	end
	return code:sub(1, 4) .. "-" .. code:sub(5)
end

local function pair_formspec(dialogdata)
	local buttons_y = dialogdata.error and 4.75 or 4.1
	local retval = {
		"formspec_version[4]",
		"size[8,", tostring(buttons_y + 1.175), "]",
		"label[0.375,0.8;", fgettext("配对设备"), "]",
	}
	if dialogdata.pending then
		table.insert_all(retval, {
			"label[6.4,0.8;", fgettext("正在配对……"), "]",
		})
	end
	table.insert_all(retval, {
		"label[0.375,1.4;",
		fgettext("1. 在浏览器打开 $1 并登录\\n2. 进入「云同步 → 设备」页面生成配对码(10 分钟内有效)\\n3. 在下方输入 8 位配对码",
				cloud_api.site_url()), "]",
		"field[0.375,3.1;7.25,0.8;code;",
		fgettext("配对码(如 ABCD-EF23)"), ";",
		core.formspec_escape(dialogdata.code or ""), "]",
	})

	if dialogdata.error then
		table.insert_all(retval, {
			"box[0.375,", tostring(buttons_y - 0.75), ";7.25,0.6;#600]",
			"label[0.625,", tostring(buttons_y - 0.45), ";",
			core.formspec_escape(dialogdata.error), "]",
		})
	end

	table.insert_all(retval, {
		"container[0.375,", tostring(buttons_y), "]",
		"button[0,0;2.3,0.8;dlg_pair_browser;", fgettext("打开网站"), "]",
		"button[2.475,0;2.3,0.8;dlg_pair_cancel;", fgettext("取消"), "]",
		"button[4.95,0;2.3,0.8;dlg_pair_confirm;", fgettext("配对"), "]",
		"container_end[]",
	})

	return table.concat(retval)
end

local function pair_buttonhandler(this, fields)
	this.data.error = nil

	if fields.dlg_pair_cancel then
		this:delete()
		return true
	end

	if fields.dlg_pair_browser then
		core.open_url(cloud_api.site_url())
		return true
	end

	if (fields.dlg_pair_confirm or fields.key_enter) and not this.data.pending then
		this.data.code = fields.code or ""
		local code = normalize_code(fields.code)
		if not code then
			this.data.error = fgettext("配对码格式不正确:应为 8 位字母数字,可带连字符(如 ABCD-EF23)")
			return true
		end

		this.data.pending = true
		ui.update()
		cloud_api.pair(code, nil, function(result)
			this.data.pending = false
			if not result.success then
				this.data.error = result.error
				ui.update()
				return
			end

			cloud_store.save({
				deviceToken = result.data.deviceToken,
				deviceId = result.data.deviceId,
				deviceName = result.data.deviceName,
				siteUsername = result.data.siteUsername,
				defaultServerUsername = result.data.defaultServerUsername,
			})
			core.cloud_reload_auth() -- notify the C++ cloud service
			this:delete()
			ui.update()
		end)
		return true
	end

	return false
end

function create_cloud_pair_dialog()
	return dialog_create("dlg_cloud_pair",
			pair_formspec,
			pair_buttonhandler,
			nil)
end
