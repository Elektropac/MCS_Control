# Template App

Copy this folder as a starting point for new apps.

## Steps

1. Copy `apps/_template/` to `apps/your_app_name/`
2. Rename `template_app.*` to `your_app_name.*`
3. Rename the functions (`template_app_init` → `your_app_init`, etc.)
4. Add your app to `apps/app_registry/app_registry.cpp`
5. Add a config entry to `data/config.json` under `"functions"`
6. Build and flash

## Config

```json
{ "id": "instance_1", "type": "your_app_name", "input": 0 }
```

## Checklist

- [ ] Only includes `mcs_api.h` (no direct hardware access)
- [ ] Calls `mcs_process_inbox()` at the top of the loop
- [ ] Has `vTaskDelay()` in the loop (never spins)
- [ ] Documents published topics in README
- [ ] Documents subscribed topics in README
- [ ] Documents config keys in README
