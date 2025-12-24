"""Command message builders for IntelliChem control operations.

These messages send configuration commands to IntelliChem controllers
via Action 146 (21-byte payload).
"""

from dataclasses import dataclass
from typing import Optional

from .message import Message
from .constants import (
    ACTION_CONFIG_COMMAND,
    CONTROLLER_ADDRESS,
    DEFAULT_INTELLICHEM_ADDRESS,
    CONFIG_PAYLOAD_LENGTH,
    PH_SETPOINT_MIN,
    PH_SETPOINT_MAX,
    ORP_SETPOINT_MIN,
    ORP_SETPOINT_MAX,
    CALCIUM_HARDNESS_MIN,
    CALCIUM_HARDNESS_MAX,
    CYANURIC_ACID_MIN,
    CYANURIC_ACID_MAX,
    ALKALINITY_MIN,
    ALKALINITY_MAX,
    TANK_LEVEL_MIN,
    TANK_LEVEL_MAX,
)


@dataclass
class IntelliChemSettings:
    """Current IntelliChem settings for building commands.

    Used to preserve existing values when making partial updates.
    """
    ph_setpoint: float = 7.2
    orp_setpoint: int = 650
    ph_tank_level: int = 7
    orp_tank_level: int = 7
    calcium_hardness: int = 300
    cyanuric_acid: int = 30
    alkalinity: int = 80


class ConfigurationCommand(Message):
    """Configuration command to IntelliChem (Action 146).

    Sends a 21-byte configuration payload to set various IntelliChem parameters.

    Payload format (21 bytes):
        Byte  0: pH setpoint high byte (value × 100 / 256)
        Byte  1: pH setpoint low byte (value × 100 % 256)
        Byte  2: ORP setpoint high byte (value / 256)
        Byte  3: ORP setpoint low byte (value % 256)
        Byte  4: pH tank level (1-7, 0=disabled)
        Byte  5: ORP tank level (1-7, 0=disabled)
        Byte  6: Calcium hardness high byte
        Byte  7: Calcium hardness low byte
        Byte  8: Reserved (0)
        Byte  9: Cyanuric acid (0-210)
        Byte 10: Alkalinity high byte
        Byte 11: Reserved (0)
        Byte 12: Alkalinity low byte
        Bytes 13-20: Reserved (0)
    """

    def __init__(
        self,
        intellichem_address: int = DEFAULT_INTELLICHEM_ADDRESS,
        ph_setpoint: float = 7.2,
        orp_setpoint: int = 650,
        ph_tank_level: int = 7,
        orp_tank_level: int = 7,
        calcium_hardness: int = 300,
        cyanuric_acid: int = 30,
        alkalinity: int = 80,
    ):
        """Initialize a configuration command.

        Args:
            intellichem_address: Target IntelliChem address (144-158)
            ph_setpoint: pH setpoint (7.0-7.6)
            orp_setpoint: ORP setpoint in mV (400-800)
            ph_tank_level: pH tank level (1-7), 0 to disable pH dosing
            orp_tank_level: ORP tank level (1-7), 0 to disable ORP dosing
            calcium_hardness: Calcium hardness in ppm (25-800)
            cyanuric_acid: Cyanuric acid (CYA) in ppm (0-210)
            alkalinity: Total alkalinity in ppm (25-800)

        Raises:
            ValueError: If any parameter is out of valid range
        """
        # Validate parameters
        self._validate_params(
            ph_setpoint, orp_setpoint, ph_tank_level, orp_tank_level,
            calcium_hardness, cyanuric_acid, alkalinity
        )

        # Store for reference
        self.ph_setpoint = ph_setpoint
        self.orp_setpoint = orp_setpoint
        self.ph_tank_level = ph_tank_level
        self.orp_tank_level = orp_tank_level
        self.calcium_hardness = calcium_hardness
        self.cyanuric_acid = cyanuric_acid
        self.alkalinity = alkalinity

        # Build payload
        payload = self._build_payload()

        super().__init__(
            dest=intellichem_address,
            source=CONTROLLER_ADDRESS,
            action=ACTION_CONFIG_COMMAND,
            payload=payload,
        )

    def _validate_params(
        self,
        ph_setpoint: float,
        orp_setpoint: int,
        ph_tank_level: int,
        orp_tank_level: int,
        calcium_hardness: int,
        cyanuric_acid: int,
        alkalinity: int,
    ) -> None:
        """Validate all parameters are within acceptable ranges."""
        if not (PH_SETPOINT_MIN <= ph_setpoint <= PH_SETPOINT_MAX):
            raise ValueError(
                f"pH setpoint must be {PH_SETPOINT_MIN}-{PH_SETPOINT_MAX}, got {ph_setpoint}"
            )

        if not (ORP_SETPOINT_MIN <= orp_setpoint <= ORP_SETPOINT_MAX):
            raise ValueError(
                f"ORP setpoint must be {ORP_SETPOINT_MIN}-{ORP_SETPOINT_MAX}, got {orp_setpoint}"
            )

        if not (TANK_LEVEL_MIN <= ph_tank_level <= TANK_LEVEL_MAX):
            raise ValueError(
                f"pH tank level must be {TANK_LEVEL_MIN}-{TANK_LEVEL_MAX}, got {ph_tank_level}"
            )

        if not (TANK_LEVEL_MIN <= orp_tank_level <= TANK_LEVEL_MAX):
            raise ValueError(
                f"ORP tank level must be {TANK_LEVEL_MIN}-{TANK_LEVEL_MAX}, got {orp_tank_level}"
            )

        if not (CALCIUM_HARDNESS_MIN <= calcium_hardness <= CALCIUM_HARDNESS_MAX):
            raise ValueError(
                f"Calcium hardness must be {CALCIUM_HARDNESS_MIN}-{CALCIUM_HARDNESS_MAX}, "
                f"got {calcium_hardness}"
            )

        if not (CYANURIC_ACID_MIN <= cyanuric_acid <= CYANURIC_ACID_MAX):
            raise ValueError(
                f"Cyanuric acid must be {CYANURIC_ACID_MIN}-{CYANURIC_ACID_MAX}, "
                f"got {cyanuric_acid}"
            )

        if not (ALKALINITY_MIN <= alkalinity <= ALKALINITY_MAX):
            raise ValueError(
                f"Alkalinity must be {ALKALINITY_MIN}-{ALKALINITY_MAX}, got {alkalinity}"
            )

    def _build_payload(self) -> bytes:
        """Build the 21-byte configuration payload."""
        payload = bytearray(CONFIG_PAYLOAD_LENGTH)

        # pH setpoint (bytes 0-1): value × 100 as 16-bit big-endian
        ph_value = int(self.ph_setpoint * 100)
        payload[0] = (ph_value >> 8) & 0xFF
        payload[1] = ph_value & 0xFF

        # ORP setpoint (bytes 2-3): value as 16-bit big-endian
        payload[2] = (self.orp_setpoint >> 8) & 0xFF
        payload[3] = self.orp_setpoint & 0xFF

        # Tank levels (bytes 4-5)
        payload[4] = self.ph_tank_level
        payload[5] = self.orp_tank_level

        # Calcium hardness (bytes 6-7): 16-bit big-endian
        payload[6] = (self.calcium_hardness >> 8) & 0xFF
        payload[7] = self.calcium_hardness & 0xFF

        # Byte 8: Reserved
        payload[8] = 0

        # Cyanuric acid (byte 9)
        payload[9] = self.cyanuric_acid

        # Alkalinity (bytes 10-12): high byte, reserved, low byte
        payload[10] = (self.alkalinity >> 8) & 0xFF
        payload[11] = 0  # Reserved
        payload[12] = self.alkalinity & 0xFF

        # Bytes 13-20: Reserved (already zeroed)

        return bytes(payload)

    def __repr__(self) -> str:
        return (
            f"ConfigurationCommand(ph={self.ph_setpoint}, orp={self.orp_setpoint}, "
            f"ph_tank={self.ph_tank_level}, orp_tank={self.orp_tank_level}, "
            f"ca={self.calcium_hardness}, cya={self.cyanuric_acid}, alk={self.alkalinity})"
        )


class CommandBuilder:
    """Builder for creating commands with partial updates.

    Preserves existing settings when only changing specific values.

    Usage:
        builder = CommandBuilder(current_state)
        command = builder.with_ph_setpoint(7.4).build()
    """

    def __init__(
        self,
        settings: Optional[IntelliChemSettings] = None,
        intellichem_address: int = DEFAULT_INTELLICHEM_ADDRESS,
    ):
        """Initialize the command builder.

        Args:
            settings: Current IntelliChem settings to preserve. If None, uses defaults.
            intellichem_address: Target IntelliChem address (144-158)
        """
        self._settings = settings or IntelliChemSettings()
        self._address = intellichem_address

        # Working values (start with current settings)
        self._ph_setpoint = self._settings.ph_setpoint
        self._orp_setpoint = self._settings.orp_setpoint
        self._ph_tank_level = self._settings.ph_tank_level
        self._orp_tank_level = self._settings.orp_tank_level
        self._calcium_hardness = self._settings.calcium_hardness
        self._cyanuric_acid = self._settings.cyanuric_acid
        self._alkalinity = self._settings.alkalinity

    def with_ph_setpoint(self, value: float) -> "CommandBuilder":
        """Set pH setpoint."""
        self._ph_setpoint = value
        return self

    def with_orp_setpoint(self, value: int) -> "CommandBuilder":
        """Set ORP setpoint."""
        self._orp_setpoint = value
        return self

    def with_ph_dosing_enabled(self, enabled: bool) -> "CommandBuilder":
        """Enable or disable pH dosing.

        When disabled, sets pH tank level to 0.
        When enabled, restores to previous level (or 7 if unknown).
        """
        if enabled:
            # Restore to previous level or max
            self._ph_tank_level = self._settings.ph_tank_level or 7
        else:
            self._ph_tank_level = 0
        return self

    def with_orp_dosing_enabled(self, enabled: bool) -> "CommandBuilder":
        """Enable or disable ORP dosing.

        When disabled, sets ORP tank level to 0.
        When enabled, restores to previous level (or 7 if unknown).
        """
        if enabled:
            # Restore to previous level or max
            self._orp_tank_level = self._settings.orp_tank_level or 7
        else:
            self._orp_tank_level = 0
        return self

    def with_calcium_hardness(self, value: int) -> "CommandBuilder":
        """Set calcium hardness."""
        self._calcium_hardness = value
        return self

    def with_cyanuric_acid(self, value: int) -> "CommandBuilder":
        """Set cyanuric acid (CYA)."""
        self._cyanuric_acid = value
        return self

    def with_alkalinity(self, value: int) -> "CommandBuilder":
        """Set total alkalinity."""
        self._alkalinity = value
        return self

    def build(self) -> ConfigurationCommand:
        """Build the configuration command with all values."""
        return ConfigurationCommand(
            intellichem_address=self._address,
            ph_setpoint=self._ph_setpoint,
            orp_setpoint=self._orp_setpoint,
            ph_tank_level=self._ph_tank_level,
            orp_tank_level=self._orp_tank_level,
            calcium_hardness=self._calcium_hardness,
            cyanuric_acid=self._cyanuric_acid,
            alkalinity=self._alkalinity,
        )

    @classmethod
    def from_state(
        cls,
        state: "IntelliChemState",  # Forward reference
        intellichem_address: int = DEFAULT_INTELLICHEM_ADDRESS,
    ) -> "CommandBuilder":
        """Create a command builder from current IntelliChem state.

        Args:
            state: Current IntelliChemState from polling
            intellichem_address: Target IntelliChem address

        Returns:
            CommandBuilder initialized with current state values
        """
        settings = IntelliChemSettings(
            ph_setpoint=state.ph.setpoint,
            orp_setpoint=int(state.orp.setpoint),
            ph_tank_level=state.ph.tank_level,
            orp_tank_level=state.orp.tank_level,
            calcium_hardness=state.calcium_hardness,
            cyanuric_acid=state.cyanuric_acid,
            alkalinity=state.alkalinity,
        )
        return cls(settings=settings, intellichem_address=intellichem_address)
