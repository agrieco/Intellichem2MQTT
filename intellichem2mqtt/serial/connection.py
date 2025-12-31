"""Async RS-485 serial connection for IntelliChem communication."""

import asyncio
import logging
import time
from typing import Optional, Callable, Awaitable

import serial_asyncio

from ..config import SerialConfig
from .buffer import PacketBuffer

logger = logging.getLogger(__name__)

# Enable VERY verbose debugging
DEBUG_COMMS = True


class RS485Connection:
    """Async RS-485 serial connection manager.

    Handles connecting to the serial port, sending packets,
    and receiving complete packets from the byte stream.
    """

    def __init__(self, config: SerialConfig):
        """Initialize the connection manager.

        Args:
            config: Serial port configuration
        """
        self.config = config
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._buffer = PacketBuffer()
        self._connected = False
        self._read_task: Optional[asyncio.Task] = None
        self._packet_callback: Optional[Callable[[bytes], Awaitable[None]]] = None

    @property
    def connected(self) -> bool:
        """Check if connected to serial port."""
        return self._connected

    async def connect(self) -> None:
        """Open the serial port connection.

        Raises:
            SerialException: If connection fails
        """
        logger.info(f"Connecting to {self.config.port} at {self.config.baudrate} baud")

        try:
            self._reader, self._writer = await serial_asyncio.open_serial_connection(
                url=self.config.port,
                baudrate=self.config.baudrate,
                bytesize=self.config.databits,
                parity=self.config.parity_char,
                stopbits=self.config.stopbits,
            )
            self._connected = True
            logger.info(f"Connected to {self.config.port}")

        except Exception as e:
            logger.error(f"Failed to connect to {self.config.port}: {e}")
            raise

    async def disconnect(self) -> None:
        """Close the serial port connection."""
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
            self._read_task = None

        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None

        self._reader = None
        self._connected = False
        self._buffer.clear()
        logger.info("Disconnected from serial port")

    async def send(self, data: bytes) -> None:
        """Send data over the serial connection.

        Args:
            data: Bytes to send

        Raises:
            ConnectionError: If not connected
        """
        if not self._writer or not self._connected:
            raise ConnectionError("Not connected to serial port")

        if DEBUG_COMMS:
            logger.info("=" * 70)
            logger.info(">>> SENDING DATA <<<")
            logger.info(f"  Raw hex:    {data.hex()}")
            logger.info(f"  Length:     {len(data)} bytes")
            logger.info(f"  Raw bytes:  {list(data)}")
            # Parse the packet structure
            if len(data) >= 9:
                logger.info(f"  Preamble:   {list(data[0:3])}")
                logger.info(f"  Header:     {list(data[3:9])}")
                logger.info(f"    Start:    {data[3]} (expect 165)")
                logger.info(f"    Sub:      {data[4]} (expect 0)")
                logger.info(f"    Dest:     {data[5]}")
                logger.info(f"    Source:   {data[6]}")
                logger.info(f"    Action:   {data[7]}")
                logger.info(f"    PayLen:   {data[8]}")
                if len(data) > 9:
                    payload_len = data[8]
                    payload = data[9:9+payload_len]
                    checksum = data[9+payload_len:] if len(data) > 9+payload_len else b''
                    logger.info(f"  Payload:    {list(payload)}")
                    logger.info(f"  Checksum:   {list(checksum)}")
            logger.info(f"  Timestamp:  {time.time():.3f}")
            logger.info("=" * 70)
        else:
            logger.debug(f"Sending: {data.hex()}")

        self._writer.write(data)
        await self._writer.drain()

        if DEBUG_COMMS:
            logger.info(">>> DATA SENT SUCCESSFULLY <<<")

    async def receive_packet(self, timeout: float = 5.0) -> Optional[bytes]:
        """Wait for and receive a complete packet.

        This method reads from the serial port and assembles bytes
        into complete packets using the packet buffer.

        Args:
            timeout: Maximum time to wait in seconds

        Returns:
            Complete packet bytes if received, None on timeout
        """
        if not self._reader or not self._connected:
            raise ConnectionError("Not connected to serial port")

        deadline = asyncio.get_event_loop().time() + timeout
        start_time = time.time()
        read_count = 0
        total_bytes_read = 0

        if DEBUG_COMMS:
            logger.info("")
            logger.info("<<< WAITING FOR RESPONSE >>>")
            logger.info(f"  Timeout:    {timeout}s")
            logger.info(f"  Start:      {start_time:.3f}")
            logger.info(f"  Buffer:     {self._buffer.pending_bytes} bytes pending")

        while asyncio.get_event_loop().time() < deadline:
            # Check if we already have a complete packet in buffer
            packet = self._buffer.get_packet()
            if packet:
                if DEBUG_COMMS:
                    elapsed = time.time() - start_time
                    logger.info("")
                    logger.info("=" * 70)
                    logger.info("<<< PACKET RECEIVED <<<")
                    logger.info(f"  Raw hex:    {packet.hex()}")
                    logger.info(f"  Length:     {len(packet)} bytes")
                    logger.info(f"  Raw bytes:  {list(packet)}")
                    logger.info(f"  Elapsed:    {elapsed:.3f}s")
                    logger.info(f"  Read ops:   {read_count}")
                    logger.info(f"  Total read: {total_bytes_read} bytes")
                    # Parse the packet structure
                    if len(packet) >= 9:
                        logger.info(f"  Preamble:   {list(packet[0:3])}")
                        logger.info(f"  Header:     {list(packet[3:9])}")
                        logger.info(f"    Start:    {packet[3]} (expect 165)")
                        logger.info(f"    Sub:      {packet[4]} (expect 0)")
                        logger.info(f"    Dest:     {packet[5]}")
                        logger.info(f"    Source:   {packet[6]}")
                        logger.info(f"    Action:   {packet[7]}")
                        logger.info(f"    PayLen:   {packet[8]}")
                        if len(packet) > 9:
                            payload_len = packet[8]
                            payload = packet[9:9+payload_len]
                            checksum = packet[9+payload_len:] if len(packet) > 9+payload_len else b''
                            logger.info(f"  Payload:    {list(payload[:20])}{'...' if len(payload) > 20 else ''}")
                            logger.info(f"  Checksum:   {list(checksum)}")
                    logger.info("=" * 70)
                return packet

            # Calculate remaining timeout
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break

            # Read more bytes with timeout
            try:
                read_timeout = min(remaining, 1.0)
                data = await asyncio.wait_for(
                    self._reader.read(256),
                    timeout=read_timeout,
                )
                read_count += 1
                if data:
                    total_bytes_read += len(data)
                    if DEBUG_COMMS:
                        elapsed = time.time() - start_time
                        logger.info(f"  [RX @ {elapsed:.3f}s] {len(data)} bytes: {data.hex()}")
                        logger.info(f"                Raw: {list(data)}")
                    self._buffer.add_bytes(data)
                    if DEBUG_COMMS:
                        logger.info(f"                Buffer now: {self._buffer.pending_bytes} bytes")
                else:
                    if DEBUG_COMMS:
                        elapsed = time.time() - start_time
                        logger.info(f"  [RX @ {elapsed:.3f}s] Empty read (0 bytes)")
            except asyncio.TimeoutError:
                if DEBUG_COMMS:
                    elapsed = time.time() - start_time
                    logger.info(f"  [RX @ {elapsed:.3f}s] Read timeout ({read_timeout:.1f}s), buffer: {self._buffer.pending_bytes} bytes")
                continue

        # Timeout with no complete packet
        elapsed = time.time() - start_time
        if DEBUG_COMMS:
            logger.info("")
            logger.info("!" * 70)
            logger.info("!!! RECEIVE TIMEOUT - NO COMPLETE PACKET !!!")
            logger.info(f"  Elapsed:    {elapsed:.3f}s")
            logger.info(f"  Read ops:   {read_count}")
            logger.info(f"  Total read: {total_bytes_read} bytes")
            logger.info(f"  Buffer:     {self._buffer.pending_bytes} bytes remaining")
            if self._buffer.pending_bytes > 0:
                buffer_contents = bytes(self._buffer._buffer)
                logger.info(f"  Buffer hex: {buffer_contents.hex()}")
                logger.info(f"  Buffer raw: {list(buffer_contents)}")
            logger.info("!" * 70)
        else:
            logger.debug("Receive timeout - no complete packet")
        return None

    async def start_reading(
        self,
        callback: Callable[[bytes], Awaitable[None]],
    ) -> None:
        """Start continuous reading with a callback for each packet.

        Args:
            callback: Async function to call with each received packet
        """
        self._packet_callback = callback
        self._read_task = asyncio.create_task(self._read_loop())

    async def _read_loop(self) -> None:
        """Continuous reading loop."""
        logger.info("Starting continuous read loop")

        while self._connected and self._reader:
            try:
                # Read available bytes
                data = await self._reader.read(256)
                if data:
                    self._buffer.add_bytes(data)

                    # Process all complete packets in buffer
                    while True:
                        packet = self._buffer.get_packet()
                        if packet is None:
                            break
                        if self._packet_callback:
                            await self._packet_callback(packet)

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Error in read loop: {e}")
                await asyncio.sleep(1.0)

        logger.info("Read loop stopped")

    @property
    def stats(self) -> dict:
        """Get connection statistics."""
        return {
            "connected": self._connected,
            "port": self.config.port,
            **self._buffer.stats,
        }
