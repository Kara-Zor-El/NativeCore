# NativeCore - A ROM Recompilation Framework

A recompilation framework that translates game ROMs into standalone native desktop applications.

Supports: MacOS, Linux, Windows
>[!NOTE]
> Any other systems will not be supported at this point in time.
> Open to contributions for other platforms.

## Supported Source Platforms

- **Game Boy** (Sharp LR35902 CPU) - More info in [core/gb/README.md](cores/gb/README.md)

The recompiler analyzes a ROM, builds a control flow graph, lowers to a internal IR, then emits LLVM IR. Any code paths not discovered statically are compiled (currently) on the fly via LLVM ORC JIT.

## Shared Architecture

Every output application includes a shared common library providing:
- Audio management (volume, channel mute/solo)
- Input mapping (live key/gamepad rebinding and configuration)
- Video scaling
- Configurable FPS limiter
- In-game settings overlay

## Prerequisites

- **CMake 3.20+**
- **LLVM 21+**
- **C++17 compiler** (Only tested with Clang)
- **Task (taskfile)** (Optional, but recommended)
- **Nix** (Optional but makes building much easier)

SDL3, Dear ImGui, and other dependencies are fetched automatically via CMake FetchContent.

## Building

>[!NOTE]
> Windows is not officially supported. 
> I have not validated that this builds sucessfully for windows.

```bash
task release
```

## Running

```bash
task compile-game path/to/game.extension
```

## In-Game settings

- **F1 (or navbar button)**: Toggle settings overlay (pauses game)
  - Audio: master volume, per-channel mute/solo
  - Input: rebind keys/gamepads
  - Video: scaling mode, fullscreen toggle
  - Performance: FPS cap slider, frame timing display
