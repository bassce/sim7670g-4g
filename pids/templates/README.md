# Online PID templates

This directory contains downloadable vehicle templates. `../catalog.json` supplies the menu; adding a compatible template and a catalog entry does not require a firmware release.

- Keep `schemaVersion: 1` and a stable `id` (up to 64 characters).
- `title` and each state's `description` can contain `en`, `zh-CN`, `ja`, or additional language keys. The UI uses its selected language, then English. Keep each state's `name` stable: MQTT and expressions depend on it.
- Keep query parameters separate from translated text. Only include read-only vehicle queries.
- Bump the template version after changes. Write the final UTF-8 file with LF line endings, then update its catalog `bytes` and SHA-256. The catalog path is relative to `pids/`, for example `templates/vw-id4-basic.json`.
- Publish the template and matching catalog together. Mirror the same bytes to Gitee. On the device, refresh the online catalog, choose a template, update it if needed, select the data to enable, and apply.

Downloading or updating a template only refreshes its cache. Applying backs up and replaces the current query list; unchecked entries remain saved with `enabled: false`. The new editor shows installed online entries as names and checkboxes. Custom queries remain available as expandable forms. Compatible array-only imports remain under `../import/`.

`vw-id4-basic.json` contains four queries verified on one Volkswagen ID.4 with iCar Pro 2S: `batterySocObd`, `batteryCurrent`, `batterySoc`, and `hvBatteryVoltage`. Other model years and vehicles require verification. `generic-petrol-basic.json` contains standard OBD examples, not a claim of support by every petrol vehicle.
