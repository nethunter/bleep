# Scenes

## Purpose

A scene coordinates devices and groups through a non-blocking, ordered
sequence. It must remain transport- and brand-independent.

First panel scenario — Press Record (Start):

1. start recording on the Canon camera;
2. wait 500 ms;
3. start recording on the Tascam recorder.

Press Stop (authored Stop, not reversed Start):

1. stop recording on the Canon camera;
2. stop recording on the Tascam recorder.

Later example with lights/groups (full Phase 6 gate):

1. turn on the light group;
2. wait one second;
3. start recording on the camera;
4. wait one second;
5. start recording on the Tascam recorder.

Generated reverse-Stop remains a later option (ADR-008). The first on-device
tranche uses explicit Start and Stop lists (ADR-019) and holds every target
connected concurrently for the run (ADR-020).

## Data model

A scene contains:

- stable ID, name, enabled state, and schema version;
- ordered Start steps;
- Stop mode: generated inverse or explicit override;
- default timeout and failure policy;
- optional descriptive notes.

Step types:

- `Action`: send one typed command to a device or group;
- `Wait`: schedule the next step after a duration;
- `Parallel`: execute a bounded list of child actions together.

An action stores a stable target ID and capability command, not a brand-specific
function name.

Conceptual example:

```json
{
  "id": "studio-record",
  "name": "Studio Record",
  "stopMode": "generated",
  "start": [
    {
      "type": "action",
      "target": "group:lights",
      "command": "light.power_on"
    },
    {
      "type": "wait",
      "milliseconds": 1000
    },
    {
      "type": "action",
      "target": "device:main-camera",
      "command": "recorder.start"
    },
    {
      "type": "wait",
      "milliseconds": 1000
    },
    {
      "type": "action",
      "target": "device:tascam",
      "command": "recorder.start"
    }
  ]
}
```

The final schema may use a compact binary representation in firmware, but the
Portal-mode HTTP API should expose a versioned, readable format.

## Reversibility

Commands declare whether they are reversible and identify their inverse:

- light on ↔ light off;
- recording start ↔ recording stop;
- run start ↔ run stop, when safe for that driver.

A wait reverses to the same duration. A parallel step reverses its completed
children as a parallel group unless an explicit Stop sequence says otherwise.

The editor must reject generated Stop mode when a Start step has no safe
inverse. The operator may then define an explicit Stop sequence.

## Execution journal

Start records every successfully completed action and group member result.
Generated Stop walks that journal backward and inverts only successful work.
This avoids stopping or changing devices that never completed their Start
action.

Journal state includes:

- scene and run IDs;
- current step and direction;
- started, completed, failed, skipped, and canceled results;
- per-device group results;
- timestamps and state quality.

Persistence across power loss is a later policy decision. The initial engine
must fail safe and show that the previous run ended unexpectedly.

## Timing and concurrency

`Wait` is scheduled from monotonic time. It must not call `delay()` or block
Bluetooth, HTTP, LVGL, reconnect, or cancellation.

Parallel execution is bounded by configured queue and connection limits.
“Parallel” means dispatched without an intentional ordering delay, not
electrically or transactionally simultaneous.

## Failure policies

Each step may use:

- `abort`: stop Start execution and expose completed actions for rollback;
- `continue`: record failure and proceed;
- `retry`: retry within a bounded count and timeout.

No scene is atomic across independent hardware. UI and HTTP responses must show
per-step and per-device results.

## Validation

Before saving or running, validate:

- target IDs exist and are enabled;
- target drivers are compiled into this firmware;
- device or group exposes the command capability;
- generated Stop has safe inverses;
- waits, retries, and timeouts are within configured limits;
- queue and nesting limits are respected;
- movement actions satisfy additional safety rules.

## Portal-mode HTTP editing

The temporary Portal-mode access point provides:

- scene list, create, duplicate, rename, enable/disable, and delete;
- ordered drag/reorder editing;
- device/group target picker filtered by capability;
- action parameters and wait duration;
- generated Stop preview;
- optional explicit Stop editor;
- validation results;
- import/export and a dry-run validation endpoint.

Editing APIs exist only while Portal mode is active. Exiting Portal mode or
reaching its inactivity timeout stops the HTTP server and access point.

