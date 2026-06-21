module Commander

open System
open System.Threading
open HftDemo.Interface

// Seqlock: bump to odd (write in progress), write params, bump to even (done).
// Executor spins on odd before reading.
let mutable private seqVer = 0

let setParams (shm: HftShm) bid ask maxPos enabled =
    seqVer <- seqVer + 2
    Volatile.Write(&shm.Region.pars.strategy_version, seqVer - 1)  // odd  → write start
    shm.Region.pars.bid_threshold <- bid
    shm.Region.pars.ask_threshold <- ask
    shm.Region.pars.max_position <- maxPos
    shm.Region.pars.trading_enabled <- if enabled then 1 else 0
    Volatile.Write(&shm.Region.pars.strategy_version, seqVer)       // even → write done

let [<Literal>] RingMask = HftNative.HFT_RING_CAPACITY - 1u

let mutable tail : uint32 = 0u

let tryReadEvent (shm: HftShm) : HftExecutionEvent voption =
    let head = shm.Region.execution_ring.write_head
    
    if tail >= head then ValueNone
    else
        let ev = shm.Events[int (tail &&& RingMask)]
        tail <- tail + 1u
        shm.Region.execution_ring.read_tail <- tail
        ValueSome ev

let sideStr = function
    | 1 -> "BUY "
    | -1 -> "SELL"
    | s -> sprintf "?%d?" s

let eventStr = function
    | 0 -> "NEW"
    | 1 -> "FILL"
    | 2 -> "REJECT"
    | e -> sprintf "?%d?" e

let ts (ns: nativeint) =
    DateTimeOffset.FromUnixTimeMilliseconds(ns.ToInt64() / 1_000_000L).ToString("HH:mm:ss.fff")

let printEvent (ev: HftExecutionEvent) =
    printfn "[commander] %s  #%-6d  %s  price=%.4f  qty=%.0f  (%s)"
        (ts ev.timestamp_ns)
        (ev.order_id.ToInt64())
        (sideStr ev.side)
        ev.price
        ev.quantity
        (eventStr ev.event_type)

let drainRing (shm: HftShm) =
    let mutable cont = true
    while cont do
        match tryReadEvent shm with
        | ValueSome ev -> printEvent ev
        | ValueNone -> cont <- false

let runPhase (shm: HftShm) (seconds: float) =
    let deadline = DateTime.UtcNow.AddSeconds(seconds)
    while DateTime.UtcNow < deadline do
        drainRing shm
        Thread.Sleep(50)

[<EntryPoint>]
let main _ =
    use shm = new HftShm()
    printfn "[commander] shared memory attached"

    printfn "[commander] Phase 1 — bid<=99.80  ask>=100.20  TRADING ON"
    setParams shm 99.80 100.20 5.0 true
    runPhase shm 5.0

    printfn "[commander] Phase 2 — KILL SWITCH"
    setParams shm 99.80 100.20 5.0 false
    runPhase shm 2.0

    printfn "[commander] Phase 3 — bid<=99.95  ask>=100.05  TRADING ON"
    setParams shm 99.95 100.05 5.0 true
    runPhase shm 5.0

    drainRing shm
    printfn "[commander] done"
    0
