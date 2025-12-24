"""Base message class for IntelliChem protocol packets."""

from .constants import PREAMBLE, HEADER_START_BYTE, HEADER_SUB_BYTE


class Message:
    """Base class for IntelliChem protocol messages."""

    def __init__(
        self,
        dest: int,
        source: int,
        action: int,
        payload: bytes = b"",
    ):
        """Initialize a message.

        Args:
            dest: Destination address (144-158 for IntelliChem)
            source: Source address (16 for controller)
            action: Action code
            payload: Message payload bytes
        """
        self.dest = dest
        self.source = source
        self.action = action
        self.payload = payload

    @property
    def header(self) -> bytes:
        """Build the 6-byte header."""
        return bytes([
            HEADER_START_BYTE,
            HEADER_SUB_BYTE,
            self.dest,
            self.source,
            self.action,
            len(self.payload),
        ])

    @property
    def checksum(self) -> int:
        """Calculate checksum as sum of header + payload bytes."""
        return sum(self.header) + sum(self.payload)

    @property
    def checksum_bytes(self) -> bytes:
        """Get checksum as 2 bytes (high, low)."""
        chk = self.checksum
        return bytes([(chk >> 8) & 0xFF, chk & 0xFF])

    def to_bytes(self) -> bytes:
        """Serialize the complete message to bytes."""
        return PREAMBLE + self.header + self.payload + self.checksum_bytes

    @staticmethod
    def validate_checksum(packet: bytes) -> bool:
        """Validate a packet's checksum.

        Args:
            packet: Complete packet including preamble and checksum

        Returns:
            True if checksum is valid, False otherwise
        """
        if len(packet) < 11:  # Minimum: 3 preamble + 6 header + 2 checksum
            return False

        # Extract header and payload (skip preamble, exclude checksum)
        data = packet[3:-2]
        calculated = sum(data)

        # Get received checksum (last 2 bytes)
        received = (packet[-2] << 8) | packet[-1]

        return calculated == received

    @staticmethod
    def extract_payload(packet: bytes) -> bytes:
        """Extract payload from a packet.

        Args:
            packet: Complete packet including preamble and checksum

        Returns:
            Payload bytes
        """
        if len(packet) < 11:
            return b""

        # Payload length is in header byte 5 (index 8 in full packet)
        payload_len = packet[8]

        # Payload starts at byte 9 (after 3 preamble + 6 header)
        return packet[9:9 + payload_len]

    @staticmethod
    def get_action(packet: bytes) -> int:
        """Extract action code from a packet.

        Args:
            packet: Complete packet including preamble

        Returns:
            Action code, or -1 if packet is too short
        """
        if len(packet) < 8:
            return -1
        return packet[7]

    @staticmethod
    def get_source(packet: bytes) -> int:
        """Extract source address from a packet.

        Args:
            packet: Complete packet including preamble

        Returns:
            Source address, or -1 if packet is too short
        """
        if len(packet) < 7:
            return -1
        return packet[6]

    @staticmethod
    def get_dest(packet: bytes) -> int:
        """Extract destination address from a packet.

        Args:
            packet: Complete packet including preamble

        Returns:
            Destination address, or -1 if packet is too short
        """
        if len(packet) < 6:
            return -1
        return packet[5]

    def __repr__(self) -> str:
        return (
            f"Message(dest={self.dest}, source={self.source}, "
            f"action={self.action}, payload={self.payload.hex()})"
        )
