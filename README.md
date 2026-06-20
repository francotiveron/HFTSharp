# HFT Sharp Demo

A working demo of an HFT architecture where **F# acts as the commander** (strategy/params) and **C++ acts as the executor** (hot path), communicating via **POSIX shared memory** with zero serialization overhead.

This accompanies the article: *Interfacing F# and C++ on the HFT Hot Path*.

## Architecture

```
┌─────────────────────────┐        POSIX shared memory        ┌─────────────────────────┐
│   F# Commander          │ ─────── HftSharedRegion ────────► │   C++ Executor          │
│                         │                                    │                         │
│  Sets strategy params   │  HftStrategyParams (64 bytes,     │  Reads params on every  │
│  (thresholds, kill      │  one cache line, atomic access)   │  tick via atomic_ref    │
│   switch, version)      │                                    │                         │
│                         │ ◄────── HftExecutionRing ──────── │  Pushes execution       │
│  Reads execution events │                                    │  events (fills,         │
│  from SPSC ring         │  Lock-free SPSC ring buffer       │  rejects) into ring     │
└─────────────────────────┘        1024 slots                 └─────────────────────────┘
```

**Key properties:**
- No serialization — both processes map the same memory region
- No P/Invoke on the hot path — F# reads/writes directly via `Volatile.Read`/`Volatile.Write`
- Cache-line aligned structs prevent false sharing between producer and consumer
- Lock-free SPSC ring with `memory_order_acquire`/`release` on C++ side, exact equivalents on .NET side

## Shared Memory Layout

```
HftSharedRegion
├── HftStrategyParams        (64 bytes, aligned to cache line)
│   ├── bid_threshold        double
│   ├── ask_threshold        double
│   ├── max_position         double
│   ├── strategy_version     int64   ← F# increments on each update
│   ├── trading_enabled      int32   ← kill switch
│   └── _pad[28]             padding to 64 bytes
└── HftExecutionRing
    ├── write_head           uint64  (cache line 1 — written by C++)
    ├── read_tail            uint64  (cache line 2 — written by F#)
    └── events[1024]         HftExecutionEvent[]
```

## Memory Model Equivalence

| Semantics       | C++                              | .NET                        |
|-----------------|----------------------------------|-----------------------------|
| Acquire load    | `memory_order_acquire`           | `Volatile.Read<T>`          |
| Release store   | `memory_order_release`           | `Volatile.Write<T>`         |
| Full fence      | `memory_order_seq_cst`           | `Thread.MemoryBarrier()`    |

On x86-64 these are exact equivalents — not approximations.

## Project Structure

```
HFTSharp/
├── shared/
│   ├── hft_shm.h               # Cross-language ABI contract (C header)
│   ├── hft_shm.c               # shm_open/mmap/mlock, SPSC ring write
│   └── generated/
│       └── HftShm.g.cs         # Auto-generated C# P/Invoke bindings
├── dotnet/
│   └── HftInterface/
│       ├── HftShm.cs           # Safe C# wrapper — direct Volatile.Read/Write
│       └── HftInterface.csproj
├── fsharp/
│   ├── Program.fs              # F# commander — sets params, reads events
│   └── Commander.fsproj
├── cpp/
│   ├── executor.cpp            # C++ executor — price simulation, ring writes
│   └── CMakeLists.txt
├── CMakeLists.txt
├── hft-demo.sln
├── build.sh                    # cmake + dotnet build
└── regen-bindings.sh           # Regenerate HftShm.g.cs via ClangSharpPInvokeGenerator
```

## Building

**Prerequisites:** CMake, Ninja, .NET 8 SDK, clang

```bash
./build.sh
```

This builds:
- `build/libhft_shm.so` — shared C library
- `build/cpp/executor` — C++ executor binary
- `dotnet/HftInterface/bin/` — C# interface library
- `fsharp/bin/` — F# commander executable

## Running the Demo

In two terminals:

```bash
# Terminal 1 — start the executor first (creates shared memory)
./build/cpp/executor

# Terminal 2 — start the commander
dotnet run --project fsharp/Commander.fsproj
```

The F# commander cycles through three phases:
1. **Wide thresholds** — executor fires frequently
2. **Kill switch** — `trading_enabled = 0`, executor halts immediately
3. **Tight thresholds** — executor fires only on sharp moves

## Regenerating C# Bindings

The file `shared/generated/HftShm.g.cs` is generated from `shared/hft_shm.h` using [ClangSharpPInvokeGenerator](https://github.com/dotnet/ClangSharp):

```bash
dotnet tool install -g ClangSharpPInvokeGenerator
./regen-bindings.sh
```

Commit the generated file — it is part of the source.

## Design Notes

**Why shared memory over sockets/pipes?**  
Zero-copy, kernel bypass on the read path, and predictable latency. Once `mmap` is set up, reads and writes are plain memory accesses.

**Why only two P/Invoke calls?**  
`hft_shm_init` and `hft_shm_cleanup` are called once at startup/shutdown. Everything in between — reading params, writing events — goes directly through the mapped pointer. P/Invoke has call overhead; shared memory does not.

**Why F# as commander?**  
Discriminated unions and pattern matching make strategy state machines expressive and correct. The hot path stays in C++; F# owns the logic that decides *what* to do, not *how fast* to do it.
