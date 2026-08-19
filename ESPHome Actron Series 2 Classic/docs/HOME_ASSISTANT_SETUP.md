# Home Assistant setup

Most of this is handled automatically once the ESP32 is flashed and comes
online -- Home Assistant's ESPHome integration auto-discovers the device and
creates every entity defined in `actron-ac.yaml`. This document covers the
handful of things that aren't automatic.

## 1. Add the device

If not auto-discovered: **Settings -> Devices & Services -> Add Integration
-> ESPHome**, enter the device's IP or `actron-classic-interface.local`, and
supply the API encryption key from your `secrets.yaml` if prompted.

## 2. Expect "unknown" until the first valid PL1

Every entity reports `unknown` until the ESP32 has received and validated
its first PL1 frame from the bus -- this is intentional (per the design
brief) rather than a fault. If entities stay `unknown` for more than a
minute or two after boot, check the RS485 wiring and the `AC Bus Checksum
Failures` diagnostic sensor (see `TESTING.md`).

## 3. Optional: template "unavailable" on stale bus data

As explained in `ARCHITECTURE.md`, ESPHome exposes bus staleness as a
binary sensor (`binary_sensor.ac_bus_data_stale`) rather than a native HA
"unavailable" state. If you want the climate card itself to grey out when
the bus goes stale, wrap it in a template, e.g.:

```yaml
template:
  - climate:
      - name: "Air Conditioner (with availability)"
        availability: "{{ not is_state('binary_sensor.ac_bus_data_stale', 'on') }}"
        # ... proxy the rest of the attributes/services through to
        # climate.air_conditioner as needed, or simply leave the original
        # entity as-is and only use the stale sensor in your automations/dashboards.
```

Most people will find it simpler to just add `binary_sensor.ac_bus_data_stale`
as a dashboard warning/notification trigger rather than templating every
entity -- your call.

## 4. Clock drift automation (optional)

The design brief calls out the AC's internal clock as "not actionable by
the interface" but useful for an external automation to flag drift. Example:

```yaml
automation:
  - alias: "Warn if AC clock has drifted"
    trigger:
      - platform: time_pattern
        hours: "/1"
    condition:
      - condition: template
        value_template: >
          {{ (as_timestamp(now()) - as_timestamp(strptime(
               now().strftime('%Y-%m-%d ') ~ states('text_sensor.ac_system_clock'),
               '%Y-%m-%d %H:%M:%S'))) | abs > 120 }}
    action:
      - service: notify.notify
        data:
          message: "Actron AC clock has drifted from Home Assistant's clock by more than 2 minutes."
```

## 5. Dashboard suggestions

- Add the `climate.air_conditioner` entity as a standard thermostat card.
- Add the two fan `select` entities next to it (a Fan Mode / Fan Speed pair
  reads more clearly as two dropdowns than as HA's built-in climate fan
  modes, which is why the design brief asked for them separately).
- Put the diagnostic sensors (checksum failures, poll responses, latency,
  queue depth, retries, seconds since last PL1, bus data stale) in a
  collapsed "AC Interface Diagnostics" card -- they matter for
  troubleshooting, not daily use.

## 6. Firmware updates

OTA is enabled in `actron-ac.yaml` (`ota: platform: esphome`). After the
first USB flash, subsequent updates can be pushed from the ESPHome
dashboard or `esphome upload actron-ac.yaml` over WiFi.
