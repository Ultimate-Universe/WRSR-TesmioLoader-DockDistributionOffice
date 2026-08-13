# Dock Distribution Office

Dock Distribution Office v1.1.0 adds small and medium scripted ship docks to **Workers & Resources: Soviet Republic**. Each dock uses the native Distribution Office task interface while a TesmioLoader plugin dispatches cargo ships between domestic cargo harbours and overseas connections.

- Supported game version: **WRSR v1.1.1.9**
- TesmioLoader plugin API: **4**
- Steam Workshop item: **3776862813**
- Licence: **GNU GPL v3 or later**

## Contents

- `ModFiles/` — complete Steam Workshop upload content
- `Source/` — C++ plugin source, configuration, build script, version resource, and import library
- `CHANGELOG.md` — release changelog
- `STEAM_WORKSHOP_CHANGELOG.md` — ready-to-paste Workshop update text
- `BUILDING.md` — build and verification instructions
- `RELEASE_VALIDATION.md` — DLL and package validation record
- `RELEASE_UPLOAD_GUIDE.md` — manual GitHub and Workshop upload steps

## Features

- Small and medium Dock Distribution Office buildings
- Native Distribution Office task editor for ship routes
- Domestic cargo harbour selection
- Explicit Soviet and Western/NATO overseas source buttons
- Destination percentages used as dispatch triggers
- Domestic source percentages used as protected stock reserves
- 99.9% compatible-capacity loading target
- Complete-unload holding at destinations
- One active ship reservation per destination task
- Save/load reconstruction from native assignments and ship routes
- Direct post-unload task selection without an unnecessary return to the office

## Requirements

- Workers & Resources: Soviet Republic v1.1.1.9
- TesmioLoader with plugin API 4
- The game launched through `tesmiolauncher.exe`

The buildings are normal Workshop assets. Their Distribution Office behaviour is supplied by `DockDistributionOffice.dll` and is unavailable when the game is launched normally through Steam.

## Installation

1. Subscribe to Workshop item `3776862813`.
2. Open:

   ```text
   Steam\steamapps\workshop\content\784150\3776862813\plugins
   ```

3. Copy `DockDistributionOffice.dll` and `DockDistributionOffice.ini` to:

   ```text
   Steam\steamapps\common\SovietRepublic\tesmioloader\build\plugins
   ```

4. Start `tesmiolauncher.exe`, enable `DockDistributionOffice.dll`, and launch the game.

When updating, replace both plugin files and restart the game.

## Basic operation

1. Build either Dock Distribution Office variant.
2. Purchase or transfer compatible cargo ships into it.
3. Press **F8** to switch between the ordinary Fleet panel and Distribution Office tasks.
4. Add at least one source and one destination.
5. Select cargo on the source row.
6. Enable unloading on the destination row and set its dispatch trigger.
7. Close the building panel to apply the task configuration.

New dispatch evaluation pauses while the native task interface is rebuilding its assignment vectors and for a short safety period after it closes. Active voyages continue normally.

## Logistics rules

### Destination percentage

The destination percentage controls **when a delivery starts**. When compatible storage is below the configured percentage, the task is eligible for dispatch. It is not a partial-refill target.

### Ship loading

Ships aim to reach **99.9% of compatible cargo capacity**. The small margin avoids exact-full floating-point edge cases while behaving as a full load during ordinary play.

### Domestic source percentage

The source percentage is a protected domestic reserve. The recommended default is **0%**.

With a non-zero reserve, loading pauses when the source reaches its protected stock floor. The ship stays berthed and resumes loading when production or another delivery creates stock above the reserve. It departs after reaching the 99.9% ship-load target. Overseas sources ignore this reserve because their cargo is purchased.

### Complete unloading

A ship remains at its destination until its assigned cargo is empty. If storage is temporarily full, the ship stays at the berth and resumes unloading when space becomes available.

### Next task before returning home

After unloading, the controller evaluates the office's current unreserved tasks before allowing the ship to return home.

- If the same destination still needs cargo, the ship travels directly back to its source.
- If another valid task is waiting, the ship is retasked directly to that job.
- The ship returns to its Dock Distribution Office only when no valid task is available.

This avoids an empty office round trip between consecutive jobs.

### Reservations

Each destination task is reserved to one active ship at a time. Different destinations can be served simultaneously when compatible ships are available.

## Overseas connections

The task panel includes Soviet and Western/NATO connection buttons. Clicking one adds the corresponding native ship-route endpoint as a source task. Overseas endpoints are never added automatically.

## Save and load

No sidecar save file is required. WRSR serializes the standing assignments and each ship's native route. After loading, the plugin reconstructs its runtime office, voyage, policy, and reservation state from those records after the world has settled.

## Configuration

Runtime settings are in `DockDistributionOffice.ini`. The supplied defaults are the supported release configuration. Important values include:

```ini
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

## Building from source

See [BUILDING.md](BUILDING.md). The release build uses freestanding C++17 for the Windows MSVC ABI and exports:

```text
TsmPluginApiVersion
TsmPluginInit
TsmPluginStart
```

## Compatibility and safety

This unofficial script mod hooks live game code through TesmioLoader. Hook sites are byte-validated, relocated signatures are scanned when necessary, and compatible existing Tesmio detours are chained. A game or loader update may still require a new plugin build.

Back up important saves before installing script-mod updates and retain alternative logistics for critical services.

## Troubleshooting

### Buildings appear but ships do not dispatch

- Confirm the game was launched through TesmioLoader.
- Confirm `DockDistributionOffice.dll` is enabled.
- Close the task panel after editing.
- Confirm the destination is below its trigger.
- Confirm a compatible ship is physically present.
- Confirm source and destination support the selected resource.

### A ship waits at its source

The ship has not reached 99.9% and the domestic source is at its protected reserve. Lower the reserve, replenish the source, or use a smaller ship.

### A ship waits at its destination

The ship still carries assigned cargo and the destination cannot currently accept it. It resumes unloading when space becomes available.

### Plugin load check

The TesmioLoader log should contain:

```text
DockDistributionOffice  init v1.1.0 release
```

## Bug reports

Open a GitHub issue with reproduction steps, the complete `tesmioloader.log`, the game and TesmioLoader versions, and screenshots where useful.

## Licence

Dock Distribution Office is distributed under the **GNU General Public License version 3 or later**. See [LICENSE.txt](LICENSE.txt).

This project is not affiliated with, endorsed by, or supported by 3Division.
