"""Configuration management with Pydantic validation."""

import os
from pathlib import Path
from typing import Optional, Literal
import yaml
from pydantic import BaseModel, Field, field_validator


class SerialConfig(BaseModel):
    """Serial port configuration for RS-485 connection."""

    port: str = Field(
        default="/dev/ttyUSB0",
        description="Serial port device path"
    )
    baudrate: int = Field(
        default=9600,
        description="Baud rate"
    )
    databits: int = Field(
        default=8,
        ge=5,
        le=8,
        description="Data bits"
    )
    parity: Literal["none", "even", "odd"] = Field(
        default="none",
        description="Parity setting"
    )
    stopbits: int = Field(
        default=1,
        ge=1,
        le=2,
        description="Stop bits"
    )

    @property
    def parity_char(self) -> str:
        """Get single-character parity for pyserial."""
        return {"none": "N", "even": "E", "odd": "O"}[self.parity]


class IntelliChemConfig(BaseModel):
    """IntelliChem device configuration."""

    address: int = Field(
        default=144,
        ge=144,
        le=158,
        description="IntelliChem address on RS-485 bus"
    )
    poll_interval: int = Field(
        default=30,
        ge=5,
        le=300,
        description="Polling interval in seconds"
    )
    timeout: int = Field(
        default=5,
        ge=1,
        le=30,
        description="Response timeout in seconds"
    )


class MQTTConfig(BaseModel):
    """MQTT broker configuration."""

    host: str = Field(
        default="localhost",
        description="MQTT broker hostname or IP"
    )
    port: int = Field(
        default=1883,
        ge=1,
        le=65535,
        description="MQTT broker port"
    )
    username: Optional[str] = Field(
        default=None,
        description="MQTT username (optional)"
    )
    password: Optional[str] = Field(
        default=None,
        description="MQTT password (optional)"
    )
    client_id: str = Field(
        default="intellichem2mqtt",
        description="MQTT client identifier"
    )
    discovery_prefix: str = Field(
        default="homeassistant",
        description="Home Assistant MQTT discovery prefix"
    )
    topic_prefix: str = Field(
        default="intellichem2mqtt",
        description="Topic prefix for state publishing"
    )
    retain: bool = Field(
        default=True,
        description="Retain MQTT messages"
    )
    qos: int = Field(
        default=1,
        ge=0,
        le=2,
        description="MQTT QoS level"
    )

    @field_validator("username", "password", mode="before")
    @classmethod
    def empty_str_to_none(cls, v):
        """Convert empty strings to None."""
        if v == "":
            return None
        return v


class LoggingConfig(BaseModel):
    """Logging configuration."""

    level: Literal["DEBUG", "INFO", "WARNING", "ERROR"] = Field(
        default="INFO",
        description="Logging level"
    )
    file: Optional[Path] = Field(
        default=None,
        description="Log file path (optional)"
    )
    format: str = Field(
        default="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
        description="Log message format"
    )


class AppConfig(BaseModel):
    """Complete application configuration."""

    serial: SerialConfig = Field(
        default_factory=SerialConfig,
        description="Serial port settings"
    )
    intellichem: IntelliChemConfig = Field(
        default_factory=IntelliChemConfig,
        description="IntelliChem device settings"
    )
    mqtt: MQTTConfig = Field(
        default_factory=MQTTConfig,
        description="MQTT broker settings"
    )
    logging: LoggingConfig = Field(
        default_factory=LoggingConfig,
        description="Logging settings"
    )


def load_config(config_path: str) -> AppConfig:
    """Load configuration from a YAML file.

    Args:
        config_path: Path to the YAML configuration file

    Returns:
        Validated AppConfig instance

    Raises:
        FileNotFoundError: If config file doesn't exist
        ValueError: If config validation fails
    """
    path = Path(config_path)

    if not path.exists():
        raise FileNotFoundError(f"Configuration file not found: {config_path}")

    with open(path, "r") as f:
        raw_config = yaml.safe_load(f) or {}

    # Support environment variable substitution
    raw_config = _substitute_env_vars(raw_config)

    return AppConfig(**raw_config)


def _substitute_env_vars(config: dict) -> dict:
    """Recursively substitute environment variables in config values.

    Environment variables are referenced as ${VAR_NAME} or $VAR_NAME.
    """
    if isinstance(config, dict):
        return {k: _substitute_env_vars(v) for k, v in config.items()}
    elif isinstance(config, list):
        return [_substitute_env_vars(item) for item in config]
    elif isinstance(config, str):
        # Handle ${VAR_NAME} format
        if config.startswith("${") and config.endswith("}"):
            var_name = config[2:-1]
            return os.environ.get(var_name, config)
        # Handle $VAR_NAME format
        elif config.startswith("$") and not config.startswith("${"):
            var_name = config[1:]
            return os.environ.get(var_name, config)
        return config
    else:
        return config


def create_default_config() -> str:
    """Generate default configuration as YAML string."""
    config = AppConfig()
    return yaml.dump(
        config.model_dump(exclude_none=True),
        default_flow_style=False,
        sort_keys=False,
    )
