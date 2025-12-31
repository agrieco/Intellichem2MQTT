"""Packet buffer for RS-485 byte stream assembly."""

import logging
from typing import Optional

from ..protocol.constants import PREAMBLE

logger = logging.getLogger(__name__)

# Enable VERY verbose debugging
DEBUG_BUFFER = True


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
        iteration = 0
        while True:
            iteration += 1
            if DEBUG_BUFFER and iteration == 1 and len(self._buffer) > 0:
                logger.debug(f"[BUFFER] get_packet() called, buffer size: {len(self._buffer)}")
                logger.debug(f"[BUFFER]   Contents: {bytes(self._buffer).hex()}")
                logger.debug(f"[BUFFER]   Raw:      {list(self._buffer)}")

            # Find preamble
            preamble_idx = self._find_preamble()
            if preamble_idx == -1:
                # No preamble found, keep only last 2 bytes
                if DEBUG_BUFFER and len(self._buffer) > 2:
                    logger.debug(f"[BUFFER] No preamble [255,0,255] found in {len(self._buffer)} bytes")
                    logger.debug(f"[BUFFER]   Looking for: {list(PREAMBLE)}")
                    logger.debug(f"[BUFFER]   Buffer has:  {list(self._buffer[:min(20, len(self._buffer))])}")
                if len(self._buffer) > 2:
                    self._buffer = self._buffer[-2:]
                return None

            # Discard bytes before preamble
            if preamble_idx > 0:
                if DEBUG_BUFFER:
                    logger.debug(f"[BUFFER] Found preamble at index {preamble_idx}")
                    logger.debug(f"[BUFFER]   Discarding {preamble_idx} bytes: {list(self._buffer[:preamble_idx])}")
                self._buffer = self._buffer[preamble_idx:]

            # Need at least MIN_PACKET_SIZE bytes
            if len(self._buffer) < self.MIN_PACKET_SIZE:
                if DEBUG_BUFFER:
                    logger.debug(f"[BUFFER] Buffer too small: {len(self._buffer)} < {self.MIN_PACKET_SIZE}")
                    logger.debug(f"[BUFFER]   Need more bytes, waiting...")
                return None

            # Validate header start byte
            if self._buffer[3] != 165:
                # Invalid header, skip this preamble and try again
                if DEBUG_BUFFER:
                    logger.debug(f"[BUFFER] Invalid header start byte: {self._buffer[3]} (expected 165)")
                    logger.debug(f"[BUFFER]   Header bytes: {list(self._buffer[3:9])}")
                    logger.debug(f"[BUFFER]   Skipping this preamble...")
                self._buffer = self._buffer[1:]
                continue

            # Get payload length from header byte 5 (index 8 in buffer)
            payload_len = self._buffer[8]

            # Calculate total packet length
            packet_len = 9 + payload_len + 2  # header(9) + payload + checksum(2)

            if DEBUG_BUFFER:
                logger.debug(f"[BUFFER] Valid header found:")
                logger.debug(f"[BUFFER]   Header bytes: {list(self._buffer[3:9])}")
                logger.debug(f"[BUFFER]   Start: {self._buffer[3]}, Sub: {self._buffer[4]}")
                logger.debug(f"[BUFFER]   Dest: {self._buffer[5]}, Src: {self._buffer[6]}")
                logger.debug(f"[BUFFER]   Action: {self._buffer[7]}, PayLen: {payload_len}")
                logger.debug(f"[BUFFER]   Expected packet size: {packet_len} bytes")
                logger.debug(f"[BUFFER]   Buffer has: {len(self._buffer)} bytes")

            # Wait for complete packet
            if len(self._buffer) < packet_len:
                if DEBUG_BUFFER:
                    logger.debug(f"[BUFFER] Incomplete packet: have {len(self._buffer)}, need {packet_len}")
                    logger.debug(f"[BUFFER]   Missing {packet_len - len(self._buffer)} bytes")
                return None

            # Extract the packet
            packet = bytes(self._buffer[:packet_len])

            if DEBUG_BUFFER:
                logger.debug(f"[BUFFER] Extracted packet for validation:")
                logger.debug(f"[BUFFER]   Hex: {packet.hex()}")
                logger.debug(f"[BUFFER]   Raw: {list(packet)}")

            # Validate checksum
            if self._validate_checksum(packet):
                # Remove packet from buffer
                self._buffer = self._buffer[packet_len:]
                self._stats["packets_received"] += 1
                if DEBUG_BUFFER:
                    logger.info(f"[BUFFER] *** VALID PACKET EXTRACTED ***")
                    logger.debug(f"[BUFFER]   Remaining buffer: {len(self._buffer)} bytes")
                return packet
            else:
                # Invalid checksum, skip this preamble and try again
                if DEBUG_BUFFER:
                    # Calculate and show checksum details
                    data = packet[3:-2]
                    calculated = sum(data)
                    received = (packet[-2] << 8) | packet[-1]
                    logger.warning(f"[BUFFER] !!! INVALID CHECKSUM !!!")
                    logger.warning(f"[BUFFER]   Packet: {packet.hex()}")
                    logger.warning(f"[BUFFER]   Checksum data (bytes 3 to -2): {list(data)}")
                    logger.warning(f"[BUFFER]   Calculated checksum: {calculated} (0x{calculated:04x})")
                    logger.warning(f"[BUFFER]   Received checksum:   {received} (0x{received:04x})")
                    logger.warning(f"[BUFFER]   Received bytes:      [{packet[-2]}, {packet[-1]}]")
                    logger.warning(f"[BUFFER]   Skipping this preamble...")
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
