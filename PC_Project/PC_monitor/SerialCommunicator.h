#ifndef SERIAL_COMMUNICATOR_H
#define SERIAL_COMMUNICATOR_H

#include "PacketProtocol.h"
#include "SerialPort.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/*
    Forward declaration.

    SerialCommunicator only stores a pointer to ConsoleLogger,
    so ConsoleLogger.h does not need to be included here.

    This reduces header dependencies and helps avoid circular includes.
*/
class ConsoleLogger;


/*
===============================================================================
SerialCommunicator
===============================================================================

Responsibilities:

    1. Open / close the PC COM port through SerialPort.

    2. Maintain a dedicated TX worker thread.

    3. Maintain a dedicated RX worker thread.

    4. Encode Packet objects into binary serial frames.

    5. Parse raw UART byte streams into complete Packet objects.

    6. Maintain TX and RX queues.

    7. Provide non-blocking SendPacket().

    8. Provide non-blocking TryReceive().

    9. Optionally invoke a receive callback.

    10. Send all debug information to ConsoleLogger.

Serial architecture:

                    Main / other modules
                           |
                    SendPacket()
                           |
                           v
                       TX Queue
                           |
                       TX Thread
                           |
                      SerialPort
                           |
                         COM4
                           |
                         ESP32


                         ESP32
                           |
                         COM4
                           |
                      SerialPort
                           |
                       RX Thread
                           |
                     PacketParser
                      /         \
                     v           v
                RX Queue      Callback


-------------------------------------------------------------------------------
SendPacket()
-------------------------------------------------------------------------------

SendPacket() is non-blocking from the caller's point of view.

It only:

    1. creates Packet
    2. encodes Packet
    3. pushes encoded data into TX queue
    4. wakes TX worker
    5. returns

The actual Windows COM write is performed by TX worker thread.


-------------------------------------------------------------------------------
TryReceive()
-------------------------------------------------------------------------------

TryReceive() is non-blocking.

Returns:

    true
        One packet was available and removed from RX queue.

    false
        RX queue is currently empty.


-------------------------------------------------------------------------------
Receive callback
-------------------------------------------------------------------------------

SetReceiveCallback() is optional.

IMPORTANT:

    The callback executes on the RX worker thread.

Therefore the callback should return quickly.

Do NOT perform long blocking operations inside the callback.


-------------------------------------------------------------------------------
ConsoleLogger
-------------------------------------------------------------------------------

SerialCommunicator does not print directly to the terminal.

If logger is configured:

    SetLogger(&logger);

the module can report:

    COM open / close
    TX packets
    RX raw bytes
    RX decoded packets
    errors

When ConsoleLogger is disabled, serial communication continues normally.


-------------------------------------------------------------------------------
Lifetime requirement
-------------------------------------------------------------------------------

The ConsoleLogger object passed to SetLogger() must remain alive while
SerialCommunicator worker threads are running.

Recommended destruction / shutdown order:

    serialSender.Stop();
    serial.Close();
    logger.StopHardwareLogging();
    monitor.Stop();

===============================================================================
*/


class SerialCommunicator
{
public:

    /*
        Callback type used when a complete valid packet is received.

        PacketParser has already verified:

            SOF
            version
            payload length
            CRC

        before this callback is invoked.
    */
    using ReceiveCallback =
        std::function<
        void(const Packet&)
        >;


public:

    SerialCommunicator();

    ~SerialCommunicator();


    // Prevent copying.
    SerialCommunicator(
        const SerialCommunicator&
    ) = delete;

    SerialCommunicator& operator=(
        const SerialCommunicator&
        ) = delete;


    // ========================================================================
    // Logger
    // ========================================================================

    /*
        Attach the central ConsoleLogger.

        Pass nullptr to disable logger forwarding:

            serial.SetLogger(nullptr);

        Usually configured once before Open().
    */
    void SetLogger(
        ConsoleLogger* logger
    );


    // ========================================================================
    // Serial connection
    // ========================================================================

    /*
        Open COM port and start TX/RX worker threads.

        Example:

            serial.Open(
                L"COM4",
                115200
            );

        Returns true only if:

            COM port opened successfully
            AND
            worker threads started successfully.
    */
    bool Open(
        const std::wstring& portName,
        DWORD baudRate = 115200
    );


    /*
        Stop worker threads and close COM port.

        Safe to call multiple times.
    */
    void Close();


    /*
        Returns true while SerialCommunicator is operational.
    */
    bool IsRunning() const;


    // ========================================================================
    // Transmit
    // ========================================================================

    /*
        Queue one packet for transmission.

        This function does NOT directly wait for COM transmission.

        Returns:

            true
                Packet was successfully encoded and placed in TX queue.

            false
                Serial is not running,
                payload is invalid,
                encoding failed,
                or TX queue is full.

        NOTE:

            true means "queued successfully".

            The actual physical COM write happens later
            on the TX worker thread.
    */
    bool SendPacket(
        PacketType type,
        const std::vector<std::uint8_t>& payload = {},
        std::uint8_t flags = 0
    );


    // ========================================================================
    // Receive
    // ========================================================================

    /*
        Non-blocking receive.

        true:
            Returns one complete packet.

        false:
            No packet is currently available.
    */
    bool TryReceive(
        Packet& packet
    );


    /*
        Optional asynchronous receive callback.

        The callback executes on the RX worker thread.

        Passing an empty std::function disables it:

            serial.SetReceiveCallback({});
    */
    void SetReceiveCallback(
        ReceiveCallback callback
    );


private:

    // ========================================================================
    // Queue limits
    // ========================================================================

    /*
        Queue limits prevent unlimited RAM growth if:

            ESP32 stops consuming data,
            main application stops reading RX queue,
            serial device becomes abnormal.
    */

    static constexpr std::size_t
        MAX_TX_QUEUE_SIZE = 128;

    static constexpr std::size_t
        MAX_RX_QUEUE_SIZE = 128;


    // ========================================================================
    // TX queue item
    // ========================================================================

    /*
        Keep both representations:

            Packet
            raw encoded bytes

        Packet is useful for debug logging.

        raw is what SerialPort actually transmits.
    */
    struct TxItem
    {
        Packet packet;

        std::vector<std::uint8_t>
            raw;
    };


private:

    // ========================================================================
    // Serial port
    // ========================================================================

    SerialPort port_;


    // ========================================================================
    // Logger
    // ========================================================================

    /*
        Atomic pointer makes reading the logger pointer from TX/RX threads
        safe if SetLogger() is called from another thread.

        Logger ownership belongs to the caller.
    */
    std::atomic<ConsoleLogger*>
        logger_{ nullptr };


    // ========================================================================
    // Global communication state
    // ========================================================================

    std::atomic<bool>
        running_{ false };


    /*
        Packet sequence number.

        Each outgoing Packet gets:

            0
            1
            2
            ...
            65535
            0
            ...

        This is the SERIAL PACKET sequence number.

        It is unrelated to HardwareUsage::sequence.
    */
    std::atomic<std::uint16_t>
        nextSequence_{ 0 };


    // ========================================================================
    // TX
    // ========================================================================

    std::thread
        txThread_;

    std::mutex
        txMutex_;

    std::condition_variable
        txCondition_;

    std::deque<TxItem>
        txQueue_;


    // ========================================================================
    // RX
    // ========================================================================

    std::thread
        rxThread_;

    std::mutex
        rxMutex_;

    std::deque<Packet>
        rxQueue_;


    // ========================================================================
    // Receive callback
    // ========================================================================

    std::mutex
        callbackMutex_;

    ReceiveCallback
        receiveCallback_;


private:

    // Dedicated transmit thread.
    void TxWorker();


    // Dedicated receive thread.
    void RxWorker();


    /*
        Called after PacketParser successfully produces
        one complete valid packet.
    */
    void HandleReceivedPacket(
        const Packet& packet
    );


    /*
        Notify the communicator that a worker encountered
        a fatal serial I/O error.

        This marks the communicator as stopped and cancels
        pending serial operations.
    */
    void HandleCommunicationFailure(
        const std::wstring& message
    );
};

#endif // SERIAL_COMMUNICATOR_H
