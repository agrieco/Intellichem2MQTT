"""Async RS-485 serial connection for IntelliChem communication."""

import asyncio
import logging
from typing import Optional, Callable, Awaitable

import serial_asyncio

from ..config import SerialConfig
from .buffer import PacketBuffer

logger = logging.getLogger(__name__)


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

        logger.debug(f"Sending: {data.hex()}")
        self._writer.write(data)
        await self._writer.drain()

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

        while asyncio.get_event_loop().time() < deadline:
            # Check if we already have a complete packet in buffer
            packet = self._buffer.get_packet()
            if packet:
                return packet

            # Calculate remaining timeout
            remaining = deadline - asyncio.get_event_loop().time()
            if remaining <= 0:
                break

            # Read more bytes with timeout
            try:
                data = await asyncio.wait_for(
                    self._reader.read(256),
                    timeout=min(remaining, 1.0),
                )
                if data:
                    self._buffer.add_bytes(data)
            except asyncio.TimeoutError:
                continue

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
