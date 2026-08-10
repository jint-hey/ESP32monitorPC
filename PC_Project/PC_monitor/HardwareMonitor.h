#ifndef HARDWARE_MONITOR_H
#define HARDWARE_MONITOR_H

#include "CpuMonitor.h"
#include "GpuMonitor.h"
#include "MemoryMonitor.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

/*
===============================================================================
HardwareUsage
===============================================================================

This structure contains one complete hardware usage snapshot.

sequence:
    Local PC-side sample sequence number.

    0:
        No hardware sample has completed yet.

    1, 2, 3, ...:
        Indicates successive completed samples.

    This sequence is NOT the serial packet sequence number.

cpu:
    CPU usage percentage.
    Range: 0.0 ~ 100.0

memory:
    Physical memory usage percentage.
    Range: 0.0 ~ 100.0

gpus:
    GPU information.

    Supported GPU count:
        0
        1
        2

    Maximum GPU count is 2.

    If the underlying GPU monitor unexpectedly reports more than
    two adapters, HardwareMonitor keeps only the first two.

===============================================================================
*/

struct HardwareUsage
{
    std::uint64_t sequence = 0;

    double cpu = 0.0;

    double memory = 0.0;

    std::vector<GpuUsageInfo> gpus;
};


/*
===============================================================================
HardwareMonitor
===============================================================================

Responsibilities:

    1. Periodically collect CPU usage.
    2. Periodically collect GPU usage.
    3. Periodically collect memory usage.
    4. Run the collection process on a dedicated background thread.
    5. Cache the latest complete HardwareUsage snapshot.
    6. Provide thread-safe access to the latest data.

Timing implementation:

    std::thread
    +
    std::condition_variable
    +
    std::chrono::steady_clock

The main application thread is NOT blocked by the sampling interval.

Example:

    HardwareMonitor monitor;

    monitor.Start(1000);

    // Main thread can continue doing other work.

    HardwareUsage usage =
        monitor.GetUsage();

    monitor.Stop();

===============================================================================
*/

class HardwareMonitor
{
public:
    // Maximum number of GPUs exposed by HardwareMonitor.
    static constexpr std::size_t MAX_GPU_COUNT = 2;

public:
    HardwareMonitor();

    ~HardwareMonitor();

    // Disable copying.
    HardwareMonitor(
        const HardwareMonitor&
    ) = delete;

    HardwareMonitor& operator=(
        const HardwareMonitor&
        ) = delete;


    /*
    ===========================================================================
    Start
    ===========================================================================

    Starts the hardware monitoring background thread.

    intervalMs:
        Sampling interval in milliseconds.

    Example:

        Start(1000);

    means:

        approximately one sample every 1000 ms.

    Returns:

        true:
            Monitoring started successfully.

        false:
            Invalid interval or thread creation failed.

    If already running, this function returns true without creating
    another worker thread.
    ===========================================================================
    */

    bool Start(
        unsigned int intervalMs = 1000
    );


    /*
    ===========================================================================
    Stop
    ===========================================================================

    Stops the background monitoring thread.

    condition_variable is used to wake the worker immediately.

    Therefore, if the worker is waiting for another 800 ms,
    Stop() does NOT have to wait for those 800 ms to expire.
    ===========================================================================
    */

    void Stop();


    /*
    ===========================================================================
    GetUsage
    ===========================================================================

    Returns the most recently completed hardware snapshot.

    IMPORTANT:

        This function DOES NOT query CPU/GPU/RAM.

        It only copies the cached HardwareUsage structure.

    Therefore it is suitable for:

        ConsolePrinter
        Serial sender
        UI
        Network sender
        Other modules

    sequence == 0 means that the first hardware sample has not
    completed yet.
    ===========================================================================
    */

    HardwareUsage GetUsage() const;


    /*
    ===========================================================================
    SetInterval
    ===========================================================================

    Changes the hardware sampling interval while the monitor is running.

    Example:

        SetInterval(500);

    changes the sampling interval to approximately 500 ms.

    The worker thread is immediately notified and recalculates its
    next sampling time.
    ===========================================================================
    */

    void SetInterval(
        unsigned int intervalMs
    );


    // Returns true while monitoring is active.
    bool IsRunning() const;


    // Returns whether GPU monitoring initialized successfully.
    bool IsGpuAvailable() const;


private:
    // ------------------------------------------------------------------------
    // Hardware data collectors
    // ------------------------------------------------------------------------

    CpuMonitor cpu_;

    GpuMonitor gpu_;

    MemoryMonitor memory_;


    // ------------------------------------------------------------------------
    // Latest complete hardware snapshot
    // ------------------------------------------------------------------------

    HardwareUsage usage_;

    /*
        Protects usage_.

        Hardware queries themselves are NOT performed while holding
        this mutex.

        The mutex is only held briefly while replacing or copying
        the final HardwareUsage snapshot.
    */
    mutable std::mutex dataMutex_;


    // ------------------------------------------------------------------------
    // Worker thread
    // ------------------------------------------------------------------------

    std::thread workerThread_;


    // ------------------------------------------------------------------------
    // Worker control
    // ------------------------------------------------------------------------

    /*
        Protects:

            running_
            stopRequested_
            intervalChanged_
            interval_
    */
    mutable std::mutex controlMutex_;

    std::condition_variable condition_;

    bool running_ = false;

    bool stopRequested_ = false;

    bool intervalChanged_ = false;


    // ------------------------------------------------------------------------
    // Sampling interval
    // ------------------------------------------------------------------------

    std::chrono::milliseconds interval_{
        1000
    };


    // ------------------------------------------------------------------------
    // Local hardware sample sequence
    // ------------------------------------------------------------------------

    /*
        Modified only by the HardwareMonitor worker thread.

        0:
            no sample yet

        1,2,3,...:
            completed hardware samples
    */
    std::uint64_t sequenceCounter_ = 0;


private:
    /*
        Main worker thread function.

        Performs:

            wait
              ↓
            UpdateOnce()
              ↓
            wait
              ↓
            UpdateOnce()
              ↓
            ...
    */
    void WorkerLoop();


    /*
        Performs one complete CPU/GPU/RAM sample.

        The new sample is prepared locally first.

        Only after all hardware data has been collected is dataMutex_
        locked and usage_ replaced.

        This prevents GetUsage() from being blocked during a potentially
        slower GPU query.
    */
    void UpdateOnce();
};

#endif // HARDWARE_MONITOR_H
