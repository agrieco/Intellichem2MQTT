"""Main application orchestrator for Intellichem2MQTT."""

import asyncio
import logging
import signal
from datetime import datetime
from typing import Optional, Union

from .config import AppConfig, get_config
from .serial.connection import RS485Connection
from .protocol.outbound import StatusRequestMessage
from .protocol.inbound import StatusResponseParser
from .protocol.commands import ConfigurationCommand
from .mqtt.client import MQTTClient
from .mqtt.discovery import DiscoveryManager
from .mqtt.publisher import StatePublisher
from .mqtt.command_handler import CommandHandler
from .models import IntelliChemState, CommandResult
from .utils.logging import setup_logging

logger = logging.getLogger(__name__)


class IntelliChem2MQTT:
    """Main application class.

    Orchestrates the RS-485 communication with IntelliChem
    and MQTT publishing to Home Assistant.
    """

    def __init__(self, config: Union[AppConfig, str, None] = None):
        """Initialize the application.

        Args:
            config: AppConfig instance, path to YAML config file, or None for env/defaults
        """
        if isinstance(config, AppConfig):
            self.config = config
        elif isinstance(config, str):
            self.config = get_config(config)
        else:
            self.config = get_config()

        self.running = False
        self._shutdown_event = asyncio.Event()

        # Components (initialized in start())
        self.serial: Optional[RS485Connection] = None
        self.mqtt: Optional[MQTTClient] = None
        self.discovery: Optional[DiscoveryManager] = None
        self.publisher: Optional[StatePublisher] = None
        self.command_handler: Optional[CommandHandler] = None
        self.parser = StatusResponseParser()

        # State caching for partial command updates
        self._last_state: Optional[IntelliChemState] = None

        # Statistics
        self._stats = {
            "polls": 0,
            "successful_polls": 0,
            "failed_polls": 0,
            "commands_sent": 0,
            "commands_failed": 0,
            "last_success": None,
            "start_time": None,
        }

    async def start(self) -> None:
        """Start the application.

        Connects to serial port and MQTT broker, publishes
        discovery configs, and starts the polling loop.
        """
        # Configure logging
        setup_logging(
            level=self.config.logging.level,
            log_file=self.config.logging.file,
            format_string=self.config.logging.format,
        )

        logger.info("Starting Intellichem2MQTT")
        self._stats["start_time"] = datetime.now()
        self.running = True

        # Check if MQTT is enabled
        self._mqtt_enabled = self.config.mqtt.enabled
        if not self._mqtt_enabled:
            logger.info("MQTT not configured - running in LOG-ONLY mode")
            logger.info("Set MQTT_HOST environment variable to enable MQTT publishing")

        # Set up signal handlers
        self._setup_signal_handlers()

        # Check if control is enabled
        self._control_enabled = self.config.control.enabled
        if self._control_enabled:
            logger.info("Control features ENABLED")
        else:
            logger.info("Control features disabled (read-only mode)")

        try:
            # Initialize MQTT connection (if enabled)
            if self._mqtt_enabled:
                self.mqtt = MQTTClient(self.config.mqtt)
                await self.mqtt.connect()
                await self.mqtt.publish_availability("online")

                # Initialize discovery and publish configs
                self.discovery = DiscoveryManager(
                    self.mqtt,
                    self.config.mqtt,
                    control_enabled=self._control_enabled,
                )
                await self.discovery.publish_discovery_configs()

                # Initialize state publisher
                self.publisher = StatePublisher(self.mqtt, self.config.mqtt)

                # Initialize command handler (if control enabled)
                if self._control_enabled:
                    self.command_handler = CommandHandler(
                        config=self.config.control,
                        intellichem_address=self.config.intellichem.address,
                    )
                    self.command_handler.set_send_callback(self._send_command)
                    self.command_handler.set_result_callback(self._publish_command_result)

                    # Subscribe to command topics
                    await self.mqtt.subscribe_commands()

            # Initialize serial connection
            self.serial = RS485Connection(self.config.serial)
            await self.serial.connect()

            # Start polling loop (and message loop if control enabled)
            logger.info(
                f"Starting poll loop (interval={self.config.intellichem.poll_interval}s)"
            )

            if self._mqtt_enabled and self._control_enabled:
                # Run both poll loop and message loop concurrently
                await asyncio.gather(
                    self._poll_loop(),
                    self._message_loop(),
                )
            else:
                # Just run poll loop
                await self._poll_loop()

        except asyncio.CancelledError:
            logger.info("Application cancelled")
        except Exception as e:
            logger.error(f"Application error: {e}", exc_info=True)
            raise
        finally:
            await self.stop()

    async def _poll_loop(self) -> None:
        """Main polling loop.

        Periodically requests status from IntelliChem and
        publishes the results to MQTT.
        """
        poll_interval = self.config.intellichem.poll_interval
        timeout = self.config.intellichem.timeout
        address = self.config.intellichem.address

        was_comms_lost = False

        while self.running and not self._shutdown_event.is_set():
            self._stats["polls"] += 1

            try:
                # Build and send status request
                request = StatusRequestMessage(address)
                await self.serial.send(request.to_bytes())

                logger.debug(f"Sent status request to address {address}")

                # Wait for response
                response = await self.serial.receive_packet(timeout=timeout)

                if response:
                    # Parse the response
                    state = self.parser.parse(response)

                    if state:
                        self._stats["successful_polls"] += 1
                        self._stats["last_success"] = datetime.now()

                        # Cache state for command handler
                        self._last_state = state
                        if self.command_handler:
                            self.command_handler.set_current_state(state)

                        # Log compact status at INFO level
                        logger.info(
                            f"pH={state.ph.level:.2f} ORP={state.orp.level}mV "
                            f"T={state.temperature}°F LSI={state.lsi:.2f} "
                            f"flow={'Y' if state.flow_detected else 'N'}"
                        )

                        if self._mqtt_enabled:
                            # Publish to MQTT
                            await self.publisher.publish_state(state)

                            # If we were in comms lost state, notify recovery
                            if was_comms_lost:
                                await self.publisher.publish_comms_restored()
                                was_comms_lost = False
                        else:
                            # Log-only mode - print detailed values
                            self._log_state(state)
                    else:
                        logger.warning("Failed to parse status response")
                        self._stats["failed_polls"] += 1
                else:
                    # Timeout - no response
                    logger.warning(
                        f"No response from IntelliChem at address {address}"
                    )
                    self._stats["failed_polls"] += 1

                    if self._mqtt_enabled and not was_comms_lost:
                        await self.publisher.publish_comms_error()
                        was_comms_lost = True

            except Exception as e:
                logger.error(f"Poll error: {e}")
                self._stats["failed_polls"] += 1

            # Wait for next poll interval (or shutdown)
            try:
                await asyncio.wait_for(
                    self._shutdown_event.wait(),
                    timeout=poll_interval,
                )
                # If we get here, shutdown was requested
                break
            except asyncio.TimeoutError:
                # Normal timeout, continue polling
                continue

    async def _message_loop(self) -> None:
        """MQTT message processing loop for command handling.

        Runs concurrently with the poll loop to process incoming commands.
        """
        logger.debug("Starting MQTT message loop")

        try:
            command_prefix = self.mqtt.topic("intellichem", "set")
            await self.mqtt.message_loop(
                callback=self._handle_mqtt_message,
                filter_prefix=command_prefix,
            )
        except asyncio.CancelledError:
            logger.debug("Message loop cancelled")
        except Exception as e:
            logger.error(f"Message loop error: {e}")

    async def _handle_mqtt_message(self, topic: str, payload: bytes) -> None:
        """Handle an incoming MQTT message.

        Args:
            topic: MQTT topic
            payload: Message payload
        """
        if not self.command_handler:
            return

        try:
            result = await self.command_handler.handle_message(topic, payload)
            if result:
                logger.debug(f"Command result: {result}")
        except Exception as e:
            logger.error(f"Error handling message: {e}")

    async def _send_command(self, command: ConfigurationCommand) -> bool:
        """Send a configuration command to IntelliChem.

        Args:
            command: Configuration command to send

        Returns:
            True if command was sent successfully
        """
        if not self.serial:
            logger.error("Serial connection not available")
            return False

        try:
            await self.serial.send(command.to_bytes())
            self._stats["commands_sent"] += 1
            logger.info(f"Sent command: {command}")
            return True
        except Exception as e:
            logger.error(f"Failed to send command: {e}")
            self._stats["commands_failed"] += 1
            return False

    async def _publish_command_result(self, result: CommandResult) -> None:
        """Publish a command result to MQTT.

        Args:
            result: Command result to publish
        """
        if not self.publisher:
            return

        try:
            await self.publisher.publish_command_result(result)
        except Exception as e:
            logger.error(f"Failed to publish command result: {e}")

    async def stop(self) -> None:
        """Stop the application gracefully."""
        logger.info("Stopping Intellichem2MQTT")
        self.running = False
        self._shutdown_event.set()

        # Publish offline status and disconnect MQTT
        if self.mqtt:
            try:
                await self.mqtt.publish_availability("offline")
                await self.mqtt.disconnect()
            except Exception as e:
                logger.error(f"Error disconnecting MQTT: {e}")
            self.mqtt = None

        # Disconnect serial
        if self.serial:
            try:
                await self.serial.disconnect()
            except Exception as e:
                logger.error(f"Error disconnecting serial: {e}")
            self.serial = None

        # Log statistics
        logger.info(
            f"Statistics: polls={self._stats['polls']}, "
            f"success={self._stats['successful_polls']}, "
            f"failed={self._stats['failed_polls']}"
        )
        logger.info("Intellichem2MQTT stopped")

    def _setup_signal_handlers(self) -> None:
        """Set up signal handlers for graceful shutdown."""
        loop = asyncio.get_event_loop()

        def signal_handler(sig):
            logger.info(f"Received signal {sig.name}, initiating shutdown")
            self._shutdown_event.set()

        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, lambda s=sig: signal_handler(s))

    def _log_state(self, state) -> None:
        """Log IntelliChem state to console (log-only mode)."""
        logger.info("=" * 60)
        logger.info("IntelliChem Status (LOG-ONLY MODE)")
        logger.info("=" * 60)
        logger.info(f"  pH Level:      {state.ph.level:.2f} (setpoint: {state.ph.setpoint:.1f})")
        logger.info(f"  ORP Level:     {state.orp.level} mV (setpoint: {state.orp.setpoint})")
        logger.info(f"  Temperature:   {state.temperature}°F")
        logger.info(f"  LSI:           {state.lsi:.2f}")
        logger.info(f"  Salt Level:    {state.salt_level} ppm")
        logger.info(f"  Alkalinity:    {state.alkalinity} ppm")
        logger.info(f"  Calcium:       {state.calcium_hardness} ppm")
        logger.info(f"  Cyanuric Acid: {state.cyanuric_acid} ppm")
        logger.info(f"  Flow Detected: {state.flow_detected}")
        logger.info(f"  pH Tank:       {state.ph.tank_level}% ({state.ph.dosing_status})")
        logger.info(f"  ORP Tank:      {state.orp.tank_level}% ({state.orp.dosing_status})")
        if state.alarms.any_active:
            logger.warning(f"  ALARMS:        Flow={state.alarms.flow}, "
                          f"pH Tank={state.alarms.ph_tank_empty}, "
                          f"ORP Tank={state.alarms.orp_tank_empty}")
        logger.info("=" * 60)

    @property
    def stats(self) -> dict:
        """Get application statistics."""
        return {
            **self._stats,
            "uptime": (
                str(datetime.now() - self._stats["start_time"])
                if self._stats["start_time"]
                else None
            ),
            "serial": self.serial.stats if self.serial else None,
        }


async def run_app(config: Union[AppConfig, str, None] = None) -> None:
    """Run the application.

    Args:
        config: AppConfig instance, path to config file, or None for env/defaults
    """
    app = IntelliChem2MQTT(config)
    await app.start()
