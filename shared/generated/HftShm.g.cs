using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HftDemo.Interface
{
    public partial struct HftStrategyParams
    {
        public double bid_threshold;

        public double ask_threshold;

        public double max_position;

        [NativeTypeName("atomic<int32_t>")]
        public volatile int strategy_version;

        [NativeTypeName("int32_t")]
        public int trading_enabled;

        [NativeTypeName("uint8_t[32]")]
        public __pad_e__FixedBuffer _pad;

        [InlineArray(32)]
        public partial struct __pad_e__FixedBuffer
        {
            public byte e0;
        }
    }

    public partial struct HftExecutionEvent
    {
        [NativeTypeName("int64_t")]
        public nint timestamp_ns;

        [NativeTypeName("int64_t")]
        public nint order_id;

        public double price;

        public double quantity;

        [NativeTypeName("int32_t")]
        public int side;

        [NativeTypeName("int32_t")]
        public int event_type;
    }

    public partial struct HftExecutionRing
    {
        [NativeTypeName("atomic<uint32_t>")]
        public volatile uint write_head;

        [NativeTypeName("uint8_t[60]")]
        public __pad1_e__FixedBuffer _pad1;

        [NativeTypeName("atomic<uint32_t>")]
        public volatile uint read_tail;

        [NativeTypeName("uint8_t[60]")]
        public __pad2_e__FixedBuffer _pad2;

        [NativeTypeName("HftExecutionEvent[1024]")]
        public _events_e__FixedBuffer events;

        [InlineArray(60)]
        public partial struct __pad1_e__FixedBuffer
        {
            public byte e0;
        }

        [InlineArray(60)]
        public partial struct __pad2_e__FixedBuffer
        {
            public byte e0;
        }

        [InlineArray(1024)]
        public partial struct _events_e__FixedBuffer
        {
            public HftExecutionEvent e0;
        }
    }

    public partial struct HftSharedRegion
    {
        public HftStrategyParams pars;

        public HftExecutionRing execution_ring;
    }

    public static unsafe partial class HftNative
    {
        [DllImport("hft_shm", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("HftSharedMemory")]
        public static extern void* hft_shm_init();

        [DllImport("hft_shm", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        [return: NativeTypeName("HftSharedMemory")]
        public static extern void* hft_shm_attach();

        [DllImport("hft_shm", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        public static extern void hft_shm_cleanup([NativeTypeName("HftSharedMemory")] void* shm);

        [NativeTypeName("#define HFT_RING_CAPACITY 1024")]
        public const int HFT_RING_CAPACITY = 1024;

        [NativeTypeName("#define HFT_CACHE_LINE 64")]
        public const int HFT_CACHE_LINE = 64;
    }
}
