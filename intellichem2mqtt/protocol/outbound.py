"""Outbound message builders for IntelliChem protocol."""

from .message import Message
from .constants import (
    ACTION_STATUS_REQUEST,
    CONTROLLER_ADDRESS,
    DEFAULT_INTELLICHEM_ADDRESS,
)


class StatusRequestMessage(Message):
    """Request IntelliChem status (Action 210).

    This message asks the IntelliChem to respond with its current status.
    The response will be an Action 18 message with 41 bytes of payload.

    Packet format:
        [255, 0, 255]           # Preamble
        [165, 0, DEST, 16, 210, 1]  # Header (dest=IntelliChem addr, src=16)
        [210]                   # Payload (echo action code)
        [CHK_HI, CHK_LO]        # Checksum
    """

    def __init__(self, intellichem_address: int = DEFAULT_INTELLICHEM_ADDRESS):
        """Initialize a status request message.

        Args:
            intellichem_address: Target IntelliChem address (144-158)
        """
        # Payload is just the action code echoed
        payload = bytes([ACTION_STATUS_REQUEST])

        super().__init__(
            dest=intellichem_address,
            source=CONTROLLER_ADDRESS,
            action=ACTION_STATUS_REQUEST,
            payload=payload,
        )


class ConfigurationMessage(Message):
    """Configuration command to IntelliChem (Action 146).

    NOTE: This is a placeholder for future write operations.
    The exact payload format for configuration commands needs
    to be reverse-engineered from the nodejs-poolController code.
    """

    def __init__(
        self,
        intellichem_address: int = DEFAULT_INTELLICHEM_ADDRESS,
        payload: bytes = b"",
    ):
        """Initialize a configuration command message.

        Args:
            intellichem_address: Target IntelliChem address (144-158)
            payload: Configuration payload bytes
        """
        super().__init__(
            dest=intellichem_address,
            source=CONTROLLER_ADDRESS,
            action=146,  # ACTION_CONFIG_COMMAND
            payload=payload,
        )
