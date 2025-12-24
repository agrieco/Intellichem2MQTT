"""Command validation models for MQTT input validation.

These Pydantic models validate incoming MQTT command payloads before
they are converted to protocol commands.
"""

from typing import Optional, Literal
from pydantic import BaseModel, Field, field_validator

from ..protocol.constants import (
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
)


class SetPHSetpointCommand(BaseModel):
    """Validate pH setpoint change request."""

    value: float = Field(
        ...,
        ge=PH_SETPOINT_MIN,
        le=PH_SETPOINT_MAX,
        description=f"pH setpoint ({PH_SETPOINT_MIN}-{PH_SETPOINT_MAX})"
    )

    @field_validator('value', mode='before')
    @classmethod
    def parse_value(cls, v):
        """Parse string or numeric value."""
        if isinstance(v, str):
            return float(v)
        return v


class SetORPSetpointCommand(BaseModel):
    """Validate ORP setpoint change request."""

    value: int = Field(
        ...,
        ge=ORP_SETPOINT_MIN,
        le=ORP_SETPOINT_MAX,
        description=f"ORP setpoint in mV ({ORP_SETPOINT_MIN}-{ORP_SETPOINT_MAX})"
    )

    @field_validator('value', mode='before')
    @classmethod
    def parse_value(cls, v):
        """Parse string or numeric value."""
        if isinstance(v, str):
            return int(float(v))
        return int(v)


class SetCalciumHardnessCommand(BaseModel):
    """Validate calcium hardness change request."""

    value: int = Field(
        ...,
        ge=CALCIUM_HARDNESS_MIN,
        le=CALCIUM_HARDNESS_MAX,
        description=f"Calcium hardness in ppm ({CALCIUM_HARDNESS_MIN}-{CALCIUM_HARDNESS_MAX})"
    )

    @field_validator('value', mode='before')
    @classmethod
    def parse_value(cls, v):
        """Parse string or numeric value."""
        if isinstance(v, str):
            return int(float(v))
        return int(v)


class SetCyanuricAcidCommand(BaseModel):
    """Validate cyanuric acid change request."""

    value: int = Field(
        ...,
        ge=CYANURIC_ACID_MIN,
        le=CYANURIC_ACID_MAX,
        description=f"Cyanuric acid in ppm ({CYANURIC_ACID_MIN}-{CYANURIC_ACID_MAX})"
    )

    @field_validator('value', mode='before')
    @classmethod
    def parse_value(cls, v):
        """Parse string or numeric value."""
        if isinstance(v, str):
            return int(float(v))
        return int(v)


class SetAlkalinityCommand(BaseModel):
    """Validate alkalinity change request."""

    value: int = Field(
        ...,
        ge=ALKALINITY_MIN,
        le=ALKALINITY_MAX,
        description=f"Alkalinity in ppm ({ALKALINITY_MIN}-{ALKALINITY_MAX})"
    )

    @field_validator('value', mode='before')
    @classmethod
    def parse_value(cls, v):
        """Parse string or numeric value."""
        if isinstance(v, str):
            return int(float(v))
        return int(v)


class SetDosingCommand(BaseModel):
    """Validate dosing enable/disable command."""

    enabled: bool = Field(
        ...,
        description="Enable (true/ON) or disable (false/OFF) dosing"
    )

    @field_validator('enabled', mode='before')
    @classmethod
    def parse_enabled(cls, v):
        """Parse string, boolean, or numeric value."""
        if isinstance(v, bool):
            return v
        if isinstance(v, str):
            return v.lower() in ('true', '1', 'yes', 'on')
        if isinstance(v, (int, float)):
            return bool(v)
        return v


class CommandResult(BaseModel):
    """Result of a command execution."""

    success: bool = Field(
        ...,
        description="Whether the command succeeded"
    )
    command: str = Field(
        ...,
        description="The command that was executed"
    )
    message: Optional[str] = Field(
        default=None,
        description="Optional message or error details"
    )
    value: Optional[str] = Field(
        default=None,
        description="The value that was set (if applicable)"
    )

    def to_json(self) -> dict:
        """Convert to JSON-serializable dict."""
        result = {
            "success": self.success,
            "command": self.command,
        }
        if self.message:
            result["message"] = self.message
        if self.value is not None:
            result["value"] = self.value
        return result


# Mapping of topic suffixes to validation models
COMMAND_VALIDATORS = {
    "ph_setpoint": SetPHSetpointCommand,
    "orp_setpoint": SetORPSetpointCommand,
    "calcium_hardness": SetCalciumHardnessCommand,
    "cyanuric_acid": SetCyanuricAcidCommand,
    "alkalinity": SetAlkalinityCommand,
    "ph_dosing": SetDosingCommand,
    "orp_dosing": SetDosingCommand,
}


def validate_command(command_type: str, payload: str) -> BaseModel:
    """Validate a command payload.

    Args:
        command_type: The type of command (e.g., 'ph_setpoint', 'orp_dosing')
        payload: The raw payload string from MQTT

    Returns:
        Validated command model

    Raises:
        ValueError: If command_type is unknown
        pydantic.ValidationError: If payload is invalid
    """
    if command_type not in COMMAND_VALIDATORS:
        raise ValueError(f"Unknown command type: {command_type}")

    validator = COMMAND_VALIDATORS[command_type]

    # For dosing commands, payload is just ON/OFF
    if command_type in ("ph_dosing", "orp_dosing"):
        return validator(enabled=payload)

    # For setpoint commands, payload is the value
    return validator(value=payload)
