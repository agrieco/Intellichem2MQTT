"""Logging configuration for Intellichem2MQTT."""

import logging
import sys
from pathlib import Path
from typing import Optional


def setup_logging(
    level: str = "INFO",
    log_file: Optional[Path] = None,
    format_string: Optional[str] = None,
) -> None:
    """Configure application logging.

    Args:
        level: Logging level (DEBUG, INFO, WARNING, ERROR)
        log_file: Optional file path for log output
        format_string: Custom log format string
    """
    if format_string is None:
        format_string = "%(asctime)s - %(name)s - %(levelname)s - %(message)s"

    # Convert string level to logging constant
    numeric_level = getattr(logging, level.upper(), logging.INFO)

    # Create formatter
    formatter = logging.Formatter(format_string)

    # Configure root logger
    root_logger = logging.getLogger()
    root_logger.setLevel(numeric_level)

    # Remove any existing handlers
    for handler in root_logger.handlers[:]:
        root_logger.removeHandler(handler)

    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(numeric_level)
    console_handler.setFormatter(formatter)
    root_logger.addHandler(console_handler)

    # File handler (if specified)
    if log_file:
        log_file = Path(log_file)
        log_file.parent.mkdir(parents=True, exist_ok=True)

        file_handler = logging.FileHandler(log_file)
        file_handler.setLevel(numeric_level)
        file_handler.setFormatter(formatter)
        root_logger.addHandler(file_handler)

    # Set specific logger levels
    # Reduce noise from libraries
    logging.getLogger("asyncio").setLevel(logging.WARNING)
    logging.getLogger("aiomqtt").setLevel(logging.WARNING)

    logging.info(f"Logging configured: level={level}")
    if log_file:
        logging.info(f"Log file: {log_file}")
