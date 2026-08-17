# MCS Control — Status

## Sidst opdateret: 2026-08-17

## Branch: `feature/cleanup` (merges til main ugentligt)

## Arkitektur
- **FreeRTOS** — alt kører som tasks, ingen Arduino loop()
- **lib/** — genbrugelige drivers (hal, tca9535, ads1115, ssd1306, buzzer, buttons, logging)
- **src/hardfunc/** — produktspecifikke hardware-funktioner
- **src/tasks/** — FreeRTOS tasks (flow_guard, buttons, display, network, serial_cmd)
- **src/debug/** — diagnostik, task registry, serial commands
- **I2C mutex** — alle bus-accesses er trådsikre
- **Niklas' netværk** — integreret som FreeRTOS task (W5500, WiFi, WebSocket, web server)

## Menu-system (OLED)
- Config-drevet (menu.items i config.json)
- Animated ikoner (kun fremhævet item animerer)
- Smooth scroll med cirkulær wrap
- Afrundet ramme + shadow på valgt item
- Display task med adaptiv framerate
- Custom screen mode for full-screen views (tank grafik osv.)
- Dynamiske submenuer fra config (tanks bygges fra functions/probe)
- Serial navigation: 8=op, 2=ned, 4=tilbage, 6=ind, 5=OK

## Config
- Embedded default config i firmware (ingen uploadfs nødvendig)
- LittleFS override hvis fil eksisterer
- Config loades i setup() FØR tasks (garanterer korrekt init-rækkefølge)
- Format: JSON med type, channels, outputs, functions, menu

## Næste skridt
- [ ] Probe-ikoner til tank submenu
- [ ] Pumps submenu (dynamisk fra functions type=pump_controller)
- [ ] Function_silo kommandoer (relay_set, voltage_set, adc_read, status)
- [ ] Forfin menu-ikoner (pixel-editor)
- [ ] Network submenu (vis IP, MAC, server status)
- [ ] Diagnostics submenu (task list, memory, ADC live)
- [ ] PSRAM-allocator til ArduinoJson
- [ ] Interface-lag diskussion med Niklas

## Toolchain
- PlatformIO, espressif32 6.12.0
- Board: esp32-s3-devkitc-1-16mb (custom board def)
- C++17, -fpermissive (toolchain workaround)
- glibcxx_fix.h for stdlib debug symbol bug

## Vigtige beslutninger
- Config styrer kun top-menu, submenuer er hardcoded per modul
- PSRAM til interface-lag (JSON buffere), intern RAM til tidskritisk
- Branches: feature/* for arbejde, main for stabil, merge 1x/uge
- Niklas' header-only libs: inline vars (C++17) for at undgå multiple definition
