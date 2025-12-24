"""Configuration management with Pydantic validation.

Supports three configuration sources (in priority order):
1. Environment variables (for Docker)
2. YAML config file (for traditional deployments)
3. Default values
"""

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

    host: Optional[str] = Field(
        default=None,
        description="MQTT broker hostname or IP (None = log-only mode)"
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

    @field_validator("host", "username", "password", mode="before")
    @classmethod
    def empty_str_to_none(cls, v):
        """Convert empty strings to None."""
        if v == "":
            return None
        return v

    @property
    def enabled(self) -> bool:
        """Check if MQTT is enabled (host is configured)."""
        return self.host is not None


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


# Environment variable mapping
ENV_MAPPING = {
    # Serial
    "SERIAL_PORT": ("serial", "port"),
    "SERIAL_BAUDRATE": ("serial", "baudrate", int),

    # IntelliChem
    "INTELLICHEM_ADDRESS": ("intellichem", "address", int),
    "INTELLICHEM_POLL_INTERVAL": ("intellichem", "poll_interval", int),
    "INTELLICHEM_TIMEOUT": ("intellichem", "timeout", int),

    # MQTT
    "MQTT_HOST": ("mqtt", "host"),
    "MQTT_PORT": ("mqtt", "port", int),
    "MQTT_USERNAME": ("mqtt", "username"),
    "MQTT_PASSWORD": ("mqtt", "password"),
    "MQTT_CLIENT_ID": ("mqtt", "client_id"),
    "MQTT_DISCOVERY_PREFIX": ("mqtt", "discovery_prefix"),
    "MQTT_TOPIC_PREFIX": ("mqtt", "topic_prefix"),
    "MQTT_RETAIN": ("mqtt", "retain", lambda x: x.lower() in ("true", "1", "yes")),
    "MQTT_QOS": ("mqtt", "qos", int),

    # Logging
    "LOG_LEVEL": ("logging", "level"),
}


def _get_env_value(env_var: str, mapping: tuple):
    """Get environment variable value with optional type conversion."""
    value = os.environ.get(env_var)
    if value is None:
        return None

    # Apply type conversion if specified
    if len(mapping) > 2:
        converter = mapping[2]
        try:
            return converter(value)
        except (ValueError, TypeError):
            return value
    return value


def load_config_from_env() -> AppConfig:
    """Load configuration from environment variables.

    Returns:
        AppConfig with values from environment (or defaults)
    """
    config_dict = {
        "serial": {},
        "intellichem": {},
        "mqtt": {},
        "logging": {},
    }

    for env_var, mapping in ENV_MAPPING.items():
        value = _get_env_value(env_var, mapping)
        if value is not None:
            section = mapping[0]
            key = mapping[1]
            config_dict[section][key] = value

    return AppConfig(**config_dict)


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


def get_config(config_path: Optional[str] = None) -> AppConfig:
    """Get configuration from config file or environment variables.

    Priority:
    1. Config file (if path provided and file exists)
    2. Environment variables
    3. Default values (log-only mode if no MQTT_HOST)

    Args:
        config_path: Optional path to YAML config file

    Returns:
        Validated AppConfig instance
    """
    # Try config file first
    if config_path:
        path = Path(config_path)
        if path.exists():
            return load_config(config_path)

    # Fall back to environment variables (includes defaults)
    return load_config_from_env()


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


def print_env_help() -> str:
    """Generate help text for environment variables."""
    lines = [
        "Environment Variables:",
        "",
        "  Serial Port:",
        "    SERIAL_PORT          Serial device path (default: /dev/ttyUSB0)",
        "    SERIAL_BAUDRATE      Baud rate (default: 9600)",
        "",
        "  IntelliChem:",
        "    INTELLICHEM_ADDRESS       Device address 144-158 (default: 144)",
        "    INTELLICHEM_POLL_INTERVAL Poll interval seconds (default: 30)",
        "    INTELLICHEM_TIMEOUT       Response timeout seconds (default: 5)",
        "",
        "  MQTT (required for env-based config):",
        "    MQTT_HOST             Broker hostname/IP (required)",
        "    MQTT_PORT             Broker port (default: 1883)",
        "    MQTT_USERNAME         Username (optional)",
        "    MQTT_PASSWORD         Password (optional)",
        "    MQTT_CLIENT_ID        Client ID (default: intellichem2mqtt)",
        "    MQTT_DISCOVERY_PREFIX HA discovery prefix (default: homeassistant)",
        "    MQTT_TOPIC_PREFIX     Topic prefix (default: intellichem2mqtt)",
        "",
        "  Logging:",
        "    LOG_LEVEL            DEBUG, INFO, WARNING, ERROR (default: INFO)",
    ]
    return "\n".join(lines)
