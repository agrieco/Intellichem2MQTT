"""Packet buffer for RS-485 byte stream assembly."""

import logging
from typing import Optional

from ..protocol.constants import PREAMBLE

logger = logging.getLogger(__name__)


class PacketBuffer:
    """Buffer for assembling complete packets from byte stream.

    The RS-485 bus may deliver bytes in chunks, so this buffer
    accumulates bytes and extracts complete packets.

    Packet format:
        [255, 0, 255]           # Preamble (3 bytes)
        [165, 0, DEST, SRC, ACTION, LEN]  # Header (6 bytes)
        [...payload...]         # Payload (LEN bytes)
        [CHK_HI, CHK_LO]        # Checksum (2 bytes)
    """

    # Minimum packet size: 3 preamble + 6 header + 0 payload + 2 checksum
    MIN_PACKET_SIZE = 11

    def __init__(self):
        """Initialize an empty packet buffer."""
        self._buffer = bytearray()
        self._stats = {
            "packets_received": 0,
            "bytes_received": 0,
            "invalid_checksums": 0,
            "buffer_overflows": 0,
        }

    def add_bytes(self, data: bytes) -> None:
        """Add received bytes to the buffer.

        Args:
            data: Bytes received from serial port
        """
        self._buffer.extend(data)
        self._stats["bytes_received"] += len(data)

        # Prevent buffer from growing too large
        if len(self._buffer) > 4096:
            logger.warning("Buffer overflow, clearing old data")
            self._stats["buffer_overflows"] += 1
            # Keep only the last 256 bytes
            self._buffer = self._buffer[-256:]

    def get_packet(self) -> Optional[bytes]:
        """Extract a complete packet from the buffer if available.

        Returns:
            Complete packet bytes if available, None otherwise
        """
        while True:
            # Find preamble
            preamble_idx = self._find_preamble()
            if preamble_idx == -1:
                # No preamble found, keep only last 2 bytes
                if len(self._buffer) > 2:
                    self._buffer = self._buffer[-2:]
                return None

            # Discard bytes before preamble
            if preamble_idx > 0:
                logger.debug(f"Discarding {preamble_idx} bytes before preamble")
                self._buffer = self._buffer[preamble_idx:]

            # Need at least MIN_PACKET_SIZE bytes
            if len(self._buffer) < self.MIN_PACKET_SIZE:
                return None

            # Validate header start byte
            if self._buffer[3] != 165:
                # Invalid header, skip this preamble and try again
                logger.debug("Invalid header start byte, skipping")
                self._buffer = self._buffer[1:]
                continue

            # Get payload length from header byte 5 (index 8 in buffer)
            payload_len = self._buffer[8]

            # Calculate total packet length
            packet_len = 9 + payload_len + 2  # header(9) + payload + checksum(2)

            # Wait for complete packet
            if len(self._buffer) < packet_len:
                return None

            # Extract the packet
            packet = bytes(self._buffer[:packet_len])

            # Validate checksum
            if self._validate_checksum(packet):
                # Remove packet from buffer
                self._buffer = self._buffer[packet_len:]
                self._stats["packets_received"] += 1
                logger.debug(f"Valid packet received: {packet.hex()}")
                return packet
            else:
                # Invalid checksum, skip this preamble and try again
                logger.debug(f"Invalid checksum, skipping: {packet.hex()}")
                self._stats["invalid_checksums"] += 1
                self._buffer = self._buffer[1:]
                continue

    def _find_preamble(self) -> int:
        """Find the index of the preamble in the buffer.

        Returns:
            Index of preamble start, or -1 if not found
        """
        preamble = bytes(PREAMBLE)
        try:
            return self._buffer.index(preamble)
        except ValueError:
            return -1

    def _validate_checksum(self, packet: bytes) -> bool:
        """Validate the checksum of a complete packet.

        Args:
            packet: Complete packet including preamble and checksum

        Returns:
            True if checksum is valid
        """
        if len(packet) < self.MIN_PACKET_SIZE:
            return False

        # Checksum is sum of header + payload (bytes 3 to -2)
        data = packet[3:-2]
        calculated = sum(data)

        # Received checksum is last 2 bytes (big endian)
        received = (packet[-2] << 8) | packet[-1]

        return calculated == received

    def clear(self) -> None:
        """Clear the buffer."""
        self._buffer.clear()

    @property
    def stats(self) -> dict:
        """Get buffer statistics."""
        return self._stats.copy()

    @property
    def pending_bytes(self) -> int:
        """Get number of bytes pending in buffer."""
        return len(self._buffer)

    def __len__(self) -> int:
        """Get buffer length."""
        return len(self._buffer)
