"""Pydantic data models for IntelliChem state."""

from enum import IntEnum
from typing import Optional
from datetime import datetime
from pydantic import BaseModel, Field


class DosingStatus(IntEnum):
    """Dosing status values from IntelliChem."""
    DOSING = 0
    MONITORING = 1
    MIXING = 2

    def __str__(self) -> str:
        return self.name.capitalize()


class WaterChemistry(IntEnum):
    """Water chemistry warning status."""
    OK = 0
    CORROSIVE = 1
    SCALING = 2

    def __str__(self) -> str:
        return self.name.capitalize()


class ChemicalState(BaseModel):
    """State for a chemical measurement (pH or ORP)."""

    level: float = Field(
        default=0.0,
        description="Current reading (pH value or ORP mV)"
    )
    setpoint: float = Field(
        default=0.0,
        description="Target setpoint"
    )
    dose_time: int = Field(
        default=0,
        ge=0,
        description="Current dosing time in seconds"
    )
    dose_volume: int = Field(
        default=0,
        ge=0,
        description="Dose volume in mL"
    )
    tank_level: int = Field(
        default=0,
        ge=0,
        le=6,
        description="Tank level 0-6 (7 levels)"
    )
    dosing_status: DosingStatus = Field(
        default=DosingStatus.MONITORING,
        description="Current dosing status"
    )
    is_dosing: bool = Field(
        default=False,
        description="Whether actively dosing"
    )

    @property
    def tank_level_percent(self) -> float:
        """Convert tank level to percentage (0-100)."""
        return (self.tank_level / 6.0) * 100.0

    model_config = {"use_enum_values": False}


class Alarms(BaseModel):
    """Alarm states from IntelliChem."""

    flow: bool = Field(
        default=False,
        description="Flow alarm - no water flow detected"
    )
    ph_tank_empty: bool = Field(
        default=False,
        description="pH chemical tank empty"
    )
    orp_tank_empty: bool = Field(
        default=False,
        description="ORP chemical tank empty"
    )
    probe_fault: bool = Field(
        default=False,
        description="Probe fault detected"
    )
    comms: bool = Field(
        default=False,
        description="Communication lost with IntelliChem"
    )

    @property
    def any_active(self) -> bool:
        """Check if any alarm is active."""
        return any([
            self.flow,
            self.ph_tank_empty,
            self.orp_tank_empty,
            self.probe_fault,
            self.comms,
        ])


class Warnings(BaseModel):
    """Warning states from IntelliChem."""

    ph_lockout: bool = Field(
        default=False,
        description="pH dosing locked out"
    )
    ph_daily_limit: bool = Field(
        default=False,
        description="pH daily dosing limit reached"
    )
    orp_daily_limit: bool = Field(
        default=False,
        description="ORP daily dosing limit reached"
    )
    invalid_setup: bool = Field(
        default=False,
        description="Invalid setup configuration"
    )
    chlorinator_comm_error: bool = Field(
        default=False,
        description="Cannot communicate with chlorinator"
    )
    water_chemistry: WaterChemistry = Field(
        default=WaterChemistry.OK,
        description="Water chemistry status"
    )

    @property
    def any_active(self) -> bool:
        """Check if any warning is active."""
        return any([
            self.ph_lockout,
            self.ph_daily_limit,
            self.orp_daily_limit,
            self.invalid_setup,
            self.chlorinator_comm_error,
            self.water_chemistry != WaterChemistry.OK,
        ])

    model_config = {"use_enum_values": False}


class IntelliChemState(BaseModel):
    """Complete IntelliChem state model."""

    address: int = Field(
        default=144,
        ge=144,
        le=158,
        description="IntelliChem address on RS-485 bus"
    )
    ph: ChemicalState = Field(
        default_factory=ChemicalState,
        description="pH measurement state"
    )
    orp: ChemicalState = Field(
        default_factory=ChemicalState,
        description="ORP measurement state"
    )
    lsi: float = Field(
        default=0.0,
        description="Langelier Saturation Index"
    )
    calcium_hardness: int = Field(
        default=0,
        ge=0,
        description="Calcium Hardness in ppm"
    )
    cyanuric_acid: int = Field(
        default=0,
        ge=0,
        le=210,
        description="Cyanuric Acid in ppm"
    )
    alkalinity: int = Field(
        default=0,
        ge=0,
        description="Alkalinity in ppm"
    )
    salt_level: int = Field(
        default=0,
        ge=0,
        description="Salt level in ppm (from IntelliChlor)"
    )
    temperature: int = Field(
        default=0,
        description="Water temperature (typically Fahrenheit)"
    )
    firmware: str = Field(
        default="",
        description="Firmware version string"
    )
    alarms: Alarms = Field(
        default_factory=Alarms,
        description="Active alarms"
    )
    warnings: Warnings = Field(
        default_factory=Warnings,
        description="Active warnings"
    )
    flow_detected: bool = Field(
        default=True,
        description="Water flow is detected"
    )
    comms_lost: bool = Field(
        default=False,
        description="Communication with IntelliChem lost"
    )
    last_update: Optional[datetime] = Field(
        default=None,
        description="Timestamp of last successful update"
    )

    def to_mqtt_dict(self) -> dict:
        """Convert state to dictionary suitable for MQTT publishing."""
        return {
            "address": self.address,
            "ph": {
                "level": round(self.ph.level, 2),
                "setpoint": round(self.ph.setpoint, 2),
                "tank_level": self.ph.tank_level,
                "tank_level_percent": round(self.ph.tank_level_percent, 1),
                "dose_time": self.ph.dose_time,
                "dose_volume": self.ph.dose_volume,
                "dosing_status": str(self.ph.dosing_status),
                "is_dosing": self.ph.is_dosing,
            },
            "orp": {
                "level": self.orp.level,
                "setpoint": self.orp.setpoint,
                "tank_level": self.orp.tank_level,
                "tank_level_percent": round(self.orp.tank_level_percent, 1),
                "dose_time": self.orp.dose_time,
                "dose_volume": self.orp.dose_volume,
                "dosing_status": str(self.orp.dosing_status),
                "is_dosing": self.orp.is_dosing,
            },
            "lsi": round(self.lsi, 2),
            "calcium_hardness": self.calcium_hardness,
            "cyanuric_acid": self.cyanuric_acid,
            "alkalinity": self.alkalinity,
            "salt_level": self.salt_level,
            "temperature": self.temperature,
            "firmware": self.firmware,
            "alarms": {
                "flow": self.alarms.flow,
                "ph_tank_empty": self.alarms.ph_tank_empty,
                "orp_tank_empty": self.alarms.orp_tank_empty,
                "probe_fault": self.alarms.probe_fault,
                "any_active": self.alarms.any_active,
            },
            "warnings": {
                "ph_lockout": self.warnings.ph_lockout,
                "ph_daily_limit": self.warnings.ph_daily_limit,
                "orp_daily_limit": self.warnings.orp_daily_limit,
                "invalid_setup": self.warnings.invalid_setup,
                "chlorinator_comm_error": self.warnings.chlorinator_comm_error,
                "water_chemistry": str(self.warnings.water_chemistry),
                "any_active": self.warnings.any_active,
            },
            "flow_detected": self.flow_detected,
            "comms_lost": self.comms_lost,
            "last_update": (
                self.last_update.isoformat() if self.last_update else None
            ),
        }

    model_config = {"use_enum_values": False}
