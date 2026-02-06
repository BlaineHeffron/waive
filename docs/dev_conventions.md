# Waive Development Conventions

## Threading Rules

- **Message thread only** — All UI components, `EditSession`, `UndoableCommandHandler`,
  and `CommandHandler` are message-thread only.  Never call them from background threads.
- **Background work** — Use `waive::JobQueue::submit()`.  The job function runs on a pool
  thread; the `onComplete` callback and `JobQueue::Listener::jobEvent` are delivered on the
  message thread.
- **Progress coalescing** — `JobQueue` coalesces progress updates at ~10 Hz so the message
  thread is never flooded.

## Component Conventions

| Directory      | Contents                                              |
|----------------|-------------------------------------------------------|
| `gui/src/ui/`  | JUCE `Component` subclasses (UI panels, views)       |
| `gui/src/edit/`| Edit-layer classes (`EditSession`, `UndoableCommandHandler`) |
| `gui/src/tools/`| Background job framework (`JobQueue`)                |
| `gui/src/util/`| Shared helpers (`CommandHelpers`)                     |

## Naming

- Classes: `PascalCase`
- Files: match class name (`SessionComponent.h/.cpp`)
- Namespace helpers: `waive::` namespace for free functions
- Member variables: `camelCase`, no prefix

## Edit Layer Rules

- **All mutations** go through `UndoableCommandHandler`, never call `CommandHandler` directly
  from UI code.
- **Slider coalescing** — Use `runCommandCoalesced()` for continuous controls (volume, pan)
  so a full drag produces a single undo entry.
- **Read-only commands** (ping, get_tracks, get_edit_state, list_plugins, transport_*)
  pass through without undo wrapping.
- **engine/src/ is shared** — `CommandHandler.h/.cpp` must not depend on GUI-layer types.
  The `UndoableCommandHandler` wrapper is the GUI-only layer that adds undo.

## CMake

- All new source files must be listed in `gui/CMakeLists.txt` under `target_sources`.
- Include directories for subdirectories are set via `target_include_directories` so
  `#include "Foo.h"` works without path prefixes.
