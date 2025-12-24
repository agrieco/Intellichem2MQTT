"""MQTT command handler for IntelliChem control operations.

Processes incoming MQTT commands, validates them, and queues
protocol commands for transmission.
"""

import asyncio
import logging
import time
from typing import Optional, Callable, Awaitable
from dataclasses import dataclass

from ..models import (
    IntelliChemState,
    CommandResult,
    validate_command,
)
from ..protocol.commands import CommandBuilder, ConfigurationCommand
from ..config import ControlConfig

logger = logging.getLogger(__name__)

# Type for command send callback
SendCommandCallback = Callable[[ConfigurationCommand], Awaitable[bool]]


@dataclass
class PendingCommand:
    """A command waiting to be sent."""
    command: ConfigurationCommand
    command_type: str
    value: str
    timestamp: float


class CommandHandler:
    """Handle incoming MQTT commands and route to protocol layer.

    Validates commands, applies rate limiting, and maintains
    a queue of pending commands.
    """

    # Supported command types
    SUPPORTED_COMMANDS = {
        "ph_setpoint",
        "orp_setpoint",
        "calcium_hardness",
        "cyanuric_acid",
        "alkalinity",
        "ph_dosing",
        "orp_dosing",
    }

    def __init__(
        self,
        config: ControlConfig,
        intellichem_address: int = 144,
    ):
        """Initialize the command handler.

        Args:
            config: Control configuration
            intellichem_address: Target IntelliChem address
        """
        self._config = config
        self._address = intellichem_address
        self._last_command_time = 0.0
        self._current_state: Optional[IntelliChemState] = None
        self._command_queue: asyncio.Queue[PendingCommand] = asyncio.Queue()
        self._send_callback: Optional[SendCommandCallback] = None
        self._result_callback: Optional[Callable[[CommandResult], Awaitable[None]]] = None

    def set_current_state(self, state: IntelliChemState) -> None:
        """Update the current IntelliChem state.

        This state is used to preserve existing values when
        making partial updates.

        Args:
            state: Current IntelliChem state from polling
        """
        self._current_state = state

    def set_send_callback(self, callback: SendCommandCallback) -> None:
        """Set the callback for sending commands.

        Args:
            callback: Async function that sends a command and returns success
        """
        self._send_callback = callback

    def set_result_callback(
        self,
        callback: Callable[[CommandResult], Awaitable[None]],
    ) -> None:
        """Set the callback for publishing command results.

        Args:
            callback: Async function that publishes a CommandResult
        """
        self._result_callback = callback

    async def handle_message(self, topic: str, payload: bytes) -> Optional[CommandResult]:
        """Handle an incoming MQTT command message.

        Args:
            topic: MQTT topic (e.g., 'intellichem2mqtt/intellichem/set/ph_setpoint')
            payload: Raw payload bytes

        Returns:
            CommandResult if command was processed, None if ignored
        """
        # Extract command type from topic
        # Expected format: {prefix}/intellichem/set/{command_type}
        parts = topic.split("/")
        if len(parts) < 4 or parts[-2] != "set":
            logger.debug(f"Ignoring non-command topic: {topic}")
            return None

        command_type = parts[-1]

        # Check if control is enabled
        if not self._config.enabled:
            logger.warning(f"Control disabled, ignoring command: {command_type}")
            return CommandResult(
                success=False,
                command=command_type,
                message="Control features are disabled",
            )

        # Check if command type is supported
        if command_type not in self.SUPPORTED_COMMANDS:
            logger.warning(f"Unknown command type: {command_type}")
            return CommandResult(
                success=False,
                command=command_type,
                message=f"Unknown command: {command_type}",
            )

        # Parse payload
        try:
            payload_str = payload.decode("utf-8").strip()
        except UnicodeDecodeError:
            logger.error(f"Invalid UTF-8 payload for {command_type}")
            return CommandResult(
                success=False,
                command=command_type,
                message="Invalid payload encoding",
            )

        logger.info(f"Received command: {command_type} = {payload_str}")

        # Validate the command
        try:
            validated = validate_command(command_type, payload_str)
        except ValueError as e:
            logger.error(f"Validation error for {command_type}: {e}")
            return CommandResult(
                success=False,
                command=command_type,
                message=str(e),
                value=payload_str,
            )

        # Check rate limiting
        now = time.monotonic()
        time_since_last = now - self._last_command_time
        if time_since_last < self._config.rate_limit_seconds:
            wait_time = self._config.rate_limit_seconds - time_since_last
            logger.warning(
                f"Rate limit: waiting {wait_time:.1f}s before {command_type}"
            )
            await asyncio.sleep(wait_time)

        # Build protocol command
        try:
            command = self._build_command(command_type, validated)
        except Exception as e:
            logger.error(f"Failed to build command for {command_type}: {e}")
            return CommandResult(
                success=False,
                command=command_type,
                message=f"Failed to build command: {e}",
                value=payload_str,
            )

        # Send the command
        if self._send_callback:
            try:
                success = await self._send_callback(command)
                self._last_command_time = time.monotonic()

                result = CommandResult(
                    success=success,
                    command=command_type,
                    message="Command sent" if success else "Failed to send command",
                    value=payload_str,
                )

                # Publish result if callback is set
                if self._result_callback:
                    await self._result_callback(result)

                return result

            except Exception as e:
                logger.error(f"Error sending command {command_type}: {e}")
                return CommandResult(
                    success=False,
                    command=command_type,
                    message=f"Send error: {e}",
                    value=payload_str,
                )
        else:
            logger.error("No send callback configured")
            return CommandResult(
                success=False,
                command=command_type,
                message="No send callback configured",
                value=payload_str,
            )

    def _build_command(self, command_type: str, validated) -> ConfigurationCommand:
        """Build a protocol command from validated input.

        Args:
            command_type: Type of command
            validated: Validated Pydantic model

        Returns:
            ConfigurationCommand ready to send
        """
        # Create builder with current state (or defaults)
        if self._current_state:
            builder = CommandBuilder.from_state(
                self._current_state,
                intellichem_address=self._address,
            )
        else:
            builder = CommandBuilder(intellichem_address=self._address)

        # Apply the specific change
        if command_type == "ph_setpoint":
            builder.with_ph_setpoint(validated.value)
        elif command_type == "orp_setpoint":
            builder.with_orp_setpoint(validated.value)
        elif command_type == "calcium_hardness":
            builder.with_calcium_hardness(validated.value)
        elif command_type == "cyanuric_acid":
            builder.with_cyanuric_acid(validated.value)
        elif command_type == "alkalinity":
            builder.with_alkalinity(validated.value)
        elif command_type == "ph_dosing":
            builder.with_ph_dosing_enabled(validated.enabled)
        elif command_type == "orp_dosing":
            builder.with_orp_dosing_enabled(validated.enabled)

        return builder.build()
