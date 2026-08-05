# Dock Distribution Office

**Version 1.0.0**

Dock Distribution Office is a combined building-and-script mod for **Workers & Resources: Soviet Republic**. It adds small and medium ship-dock variants that use a Distribution Office-style task interface and a TesmioLoader controller to dispatch cargo ships between domestic cargo harbours and overseas customs connections.

The mod is designed for deliberate, high-volume water logistics. It is not a general replacement for road, rail, or ordinary harbour routes. Ships are slow, berths are limited, and a badly oversized ship can become a very large floating warehouse.

Steam Workshop item: `3776862813`

Project repository: <https://github.com/Ultimate-Universe/WRSR-TesmioLoader-DockDistributionOffice>

## What the mod adds

The Workshop item contains two buildings:

- **Dock Distribution Office (Small)** — final asset ID `DockDistributionOfficeSmall`
- **Dock Distribution Office (Medium)** — final asset ID `DockDistributionOfficeMedium`

The buildings mirror the geometry and capacity classes of the base-game small and medium ship docks while adding the scripted Distribution Office controller.

The public plugin files are:

```text
DockDistributionOffice.dll
DockDistributionOffice.ini
```

## Requirements

- Workers & Resources: Soviet Republic
- TesmioLoader
- The game must be launched through `tesmiolauncher.exe`

The buildings themselves are normal Workshop assets, but their Distribution Office behaviour is supplied by the TesmioLoader plugin. Launching the game normally through Steam leaves the scripted controller unavailable.

## Installation

1. Subscribe to Workshop item `3776862813`.
2. Open the downloaded Workshop folder:

   ```text
   Steam\steamapps\workshop\content\784150\3776862813\plugins
   ```

3. Copy these files:

   ```text
   DockDistributionOffice.dll
   DockDistributionOffice.ini
   ```

4. Paste them into:

   ```text
   Steam\steamapps\common\SovietRepublic\tesmioloader\build\plugins
   ```

5. Run:

   ```text
   Steam\steamapps\common\SovietRepublic\tesmioloader\build\tesmiolauncher.exe
   ```

6. Enable `DockDistributionOffice.dll` and launch the game through TesmioLoader.

## Basic operation

1. Build either Dock Distribution Office variant.
2. Purchase or transfer compatible cargo ships into the office.
3. Open the building.
4. Press **F8** to switch between the ordinary ship Fleet panel and the Distribution Office task panel.
5. Add at least one source and one destination.
6. Select the cargo resource on the source row.
7. Enable unloading on the destination row and choose its dispatch threshold.
8. Close the Dock Distribution Office panel to apply the finished task configuration and allow dispatch evaluation.

The panel-close step is intentional. The native Distribution Office UI temporarily rebuilds assignment vectors while rows are edited. New dispatch evaluation is suspended while the panel is open and for a short safety period after it closes. Active voyages continue to run.

## Logistics rules

### Destination percentage: dispatch trigger

The destination percentage controls **when a delivery journey begins**.

When the destination’s compatible storage falls below that percentage, the controller may assign an available ship. The percentage is not a refill target and does not tell the ship to carry only the amount required to reach it.

Example:

```text
Destination trigger: 40%
Destination storage: 39%
Result: a delivery may be dispatched
```

Once dispatched, the ship follows the full-load and full-unload rules below.

### Ship loading target: 99.9%

Ships aim to reach **99.9% of their compatible cargo capacity** before leaving the source. The very small margin avoids exact-100% floating-point edge cases in the native vehicle state machine. In ordinary gameplay it behaves as a full load and often rounds to 100% in the interface.

This is why ship selection matters. Use a smaller ship when the source, destination, or consumption rate cannot sensibly support a huge cargo load.

### Source percentage: protected domestic reserve

The source percentage is a protected stock floor for domestic sources. The recommended default is **0%**.

With a non-zero source reserve:

1. The ship loads cargo available above the reserve.
2. When the source reaches the reserve, cargo transfer pauses.
3. The ship stays berthed at the source.
4. The ship keeps its original 99.9% departure target.
5. When production or another delivery creates cargo above the reserve, loading resumes.
6. The ship departs only after reaching 99.9%.

This can intentionally hold a ship at the source indefinitely when the source never replenishes. Lower the reserve, improve supply, or use a smaller ship when that is undesirable.

If a reserve is edited while a ship is already waiting, close the Dock Distribution Office panel. Version 1.0.0 refreshes active voyage policy from the live task table; the waiting ship then resumes loading when cargo is available above the new reserve.

Overseas Soviet and Western sources ignore the domestic reserve because their cargo is purchased rather than drained from local storage.

### Complete unloading

A ship remains at its destination until the assigned cargo is completely unloaded.

When destination storage is temporarily full, the ship stays at the berth as floating inventory. As the destination consumes or transfers cargo, unloading resumes automatically. The ship does not leave with undelivered cargo merely because the destination crossed its dispatch threshold.

### Direct repeat cycles

After a ship empties at a destination, the controller checks the same destination again.

- If the destination is still below its trigger, the ship keeps that job reservation and travels directly back to the source for another full load.
- If the destination has reached or exceeded its trigger, the ship returns to its Dock Distribution Office and becomes available for another job.

This avoids an unnecessary empty round trip to the office when the destination clearly requires another shipment.

### One ship per destination job

The controller reserves each destination/resource job to one ship at a time. Different destinations can dispatch different ships simultaneously, but the office will not send several ships to duplicate the same reserved job.

### Overseas connections

The Distribution panel includes explicit Soviet and Western/NATO connection buttons. These create overseas source rows using the game’s native ship-route endpoints. Overseas targets are never added automatically.

## Save and load behaviour

The mod does not require a separate sidecar save file.

The game already serializes the standing Distribution Office assignments and each ship’s native route. After loading a save, the plugin scans the restored buildings and vehicles, reconstructs its runtime office/job/reservation tables, and continues active voyages.

Reconstruction preserves:

- the owning Dock Distribution Office;
- the assigned ship;
- source, destination, and resource;
- route stage;
- destination reservation;
- current source reserve and destination trigger policy.

The controller waits for the world, building links, resources, and vehicle vectors to settle before performing normal dispatch checks.

## Fleet management and depot transfers

Idle ships may be moved from a Dock Distribution Office to an ordinary ship depot using the native move-to-depot tool. Version 1.0.0 includes sentinel-safe descriptor validation so clicking ordinary ship docks does not pass invalid native pointers into the Workshop marker scanner.

Moving a ship that is already performing a managed delivery is not recommended. Allow it to finish and berth at its home office first.

## Configuration

The configuration file is `DockDistributionOffice.ini`.

Important settings include:

```ini
[DockDistributionOffice]
enabled = 1

default_panel = distribution
enable_panel_hotkey = 1
panel_toggle_vk = 119

minimum_departure_load_permille = 999
wait_until_fully_unloaded = 1
direct_repeat_if_below_trigger = 1

idle_controller_ticks = 300
active_controller_ticks = 15
source_reserve_control_ticks = 1
active_task_policy_refresh_ticks = 300
world_scan_ticks = 600

manager_load_grace_ticks = 600
manager_assignment_stable_ticks = 300
ui_edit_quiet_ticks = 300
max_dispatch_per_pass = 3
```

`minimum_departure_load_permille = 999` means 99.9%.

`source_reserve_control_ticks` is intentionally fast only while an active ship must be pinned at a domestic source. Idle offices use the much slower controller cadence.

## How it works

### Player-facing configuration

The plugin grafts the native road Distribution Office panel onto marked ship-dock buildings. The native interface remains responsible for editing and saving task rows, selected resources, load/unload flags, and percentages.

### Manager-owned dispatch controller

The native road Distribution Office planner is not used for marked docks. The plugin maintains a runtime manager containing:

- tracked Dock Distribution Offices;
- parsed source and destination tasks;
- available in-port ships;
- active voyage records;
- destination/resource reservations;
- live source-reserve and destination-trigger policy.

The manager selects a compatible ship physically present in the office and writes a native ship schedule directly:

```text
source -> destination -> home office
```

Native ship pathfinding, movement, docking, cargo transfer, fuel use, and route serialization remain handled by the game.

### Storage and cargo statistics

Building demand and source availability use the same native building-statistics chain identified in the decompiled game:

- `FUN_1401e82e0` — base building amount/capacity statistics;
- `FUN_1401e5010` — linked-storage availability test;
- `FUN_1401e9270` — linked-storage statistics.

Vehicle cargo helpers are used only for actual ships.

### Runtime hook safety

Hook and callable locations were rechecked against the supplied decompiled `SOVIET64.exe` and `C3DDLL64.dll` files before the 1.0.0 build.

At runtime the plugin:

- validates preferred function RVAs against known prologue bytes;
- scans for a unique signature when the preferred address does not match;
- detects TesmioLoader absolute-jump detours;
- chains an existing compatible hook instead of blindly overwriting it;
- refuses a hook when the target cannot be verified;
- rejects null, sentinel, non-canonical, kernel-range, and overflowing pointer ranges before reading native structures.

This improves resilience when another Tesmio plugin intersects the same function or when a minor game update relocates code without changing the verified signature.

## Building from source

The plugin is written as a freestanding C++17 DLL for the Windows MSVC ABI and is built with Clang/LLD.

Required tools:

- `clang++`
- `lld-link`
- the included `vcruntime140_import.lib`

From the `source` directory, run `build_plugin_clang.bat`, or execute the equivalent commands:

```text
clang++ --target=x86_64-pc-windows-msvc -std=c++17 -O2 \
  -ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
  -fno-stack-protector -fno-threadsafe-statics -nostdlib \
  -c DockDistributionOffice.cpp -o DockDistributionOffice.obj

lld-link /dll /noentry /nodefaultlib /machine:x64 /opt:ref /opt:icf \
  /timestamp:0 /out:DockDistributionOffice.dll \
  DockDistributionOffice.obj vcruntime140_import.lib \
  /export:TsmPluginApiVersion /export:TsmPluginInit /export:TsmPluginStart
```

## Compatibility and safety

This is an unofficial script mod that modifies live game behaviour through TesmioLoader. Back up important saves before installing script-mod updates. A Workers & Resources or TesmioLoader update may require a new plugin build.

Do not make one scripted building the sole supply route for critical services. Maintain alternative logistics for essential industries where practical.

## Troubleshooting

### Buildings appear, but ships never dispatch

- Confirm the game was launched through TesmioLoader.
- Confirm `DockDistributionOffice.dll` is enabled.
- Close the Dock Distribution Office panel after editing.
- Confirm the destination is below its dispatch trigger.
- Confirm a compatible ship is physically in the office.
- Confirm the source and destination both support the selected resource.

### Ship waits at the source

This is expected when it has not reached 99.9% and the domestic source is at or below its protected reserve. Set the source reserve to 0%, replenish the source, or use a smaller ship.

### Ship waits at the destination

This is expected while it still has assigned cargo aboard and the destination cannot currently accept more. The ship resumes unloading when space becomes available.

### Plugin does not load

Check `tesmioloader.log` for:

```text
DockDistributionOffice  init v1.0.0 release
```

### Reporting a bug

Open a GitHub issue and include:

- the exact steps required to reproduce the problem;
- the complete `tesmioloader.log`;
- screenshots where useful.

## Licence

Dock Distribution Office is free software distributed under the **GNU General Public License version 3 or later**. See [LICENSE](LICENSE).

## Disclaimer

This project is not affiliated with, endorsed by, or supported by 3Division. Workers & Resources: Soviet Republic and related names are the property of their respective owners.
