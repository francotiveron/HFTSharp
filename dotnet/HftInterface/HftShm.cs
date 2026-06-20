using System.Runtime.CompilerServices;
using System.Threading;

namespace HftDemo.Interface;

/// <summary>
/// Attaches to the shared memory region and provides direct typed access
/// to both the strategy params and the execution ring.
///
/// P/Invoke is used only for hft_shm_init / hft_shm_cleanup.
/// All reads and writes go through Volatile.Read / Volatile.Write directly
/// on the mapped HftSharedRegion pointer — no additional P/Invoke calls.
/// </summary>
public sealed unsafe class HftShm : IDisposable
{
    private const nuint RingCapacity = 1024;

    private HftSharedRegion* _region;
    private nuint             _tail;
    private bool              _disposed;

    public HftShm()
    {
        _region = (HftSharedRegion*)HftNative.hft_shm_init();
        if (_region == null)
            throw new InvalidOperationException("Failed to attach shared memory.");
    }

    // -----------------------------------------------------------------------
    // Strategy params — written by F# commander, read by C++ executor.
    //
    // Each field is written with release ordering.
    // strategy_version is written last behind a full fence (seq_cst) so that
    // when C++ sees version > 0, all other param fields are guaranteed visible.
    // -----------------------------------------------------------------------
    public void SetParams(
        double bid_threshold,
        double ask_threshold,
        double max_position,
        bool   trading_enabled,
        long   strategy_version)
    {
        Volatile.Write(ref _region->@params.bid_threshold,   bid_threshold);
        Volatile.Write(ref _region->@params.ask_threshold,   ask_threshold);
        Volatile.Write(ref _region->@params.max_position,    max_position);
        Volatile.Write(ref _region->@params.trading_enabled, trading_enabled ? 1 : 0);
        Thread.MemoryBarrier();
        Volatile.Write(ref _region->@params.strategy_version, (nint)strategy_version);
    }

    // -----------------------------------------------------------------------
    // Execution ring — written by C++ executor, read by F# commander.
    //
    // write_head is read with acquire ordering — guarantees that by the time
    // F# sees the updated head, the event data written before it is visible.
    // read_tail is written with release ordering — C++ reads it to check
    // how much space is available in the ring.
    // -----------------------------------------------------------------------
    public HftExecutionEvent? TryReadEvent()
    {
        nuint tail = _tail;
        nuint head = Volatile.Read(ref _region->execution_ring.write_head);  // acquire

        if (tail >= head) return null;

        // Event data is visible because the acquire on write_head above
        // synchronises with the release store C++ did when it published the head.
        HftExecutionEvent ev = Unsafe.Add(
            ref _region->execution_ring.events[0],
            (int)(tail % RingCapacity));

        _tail = tail + 1;
        Volatile.Write(ref _region->execution_ring.read_tail, _tail);  // release
        return ev;
    }

    public void Dispose()
    {
        if (!_disposed && _region != null)
        {
            HftNative.hft_shm_cleanup(_region);
            _region   = null;
            _disposed = true;
        }
    }
}
