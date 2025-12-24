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
from .mqtt.client import MQTTClient
from .mqtt.discovery import DiscoveryManager
from .mqtt.publisher import StatePublisher
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
        self.parser = StatusResponseParser()

        # Statistics
        self._stats = {
            "polls": 0,
            "successful_polls": 0,
            "failed_polls": 0,
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

        # Set up signal handlers
        self._setup_signal_handlers()

        try:
            # Initialize MQTT connection
            self.mqtt = MQTTClient(self.config.mqtt)
            await self.mqtt.connect()
            await self.mqtt.publish_availability("online")

            # Initialize discovery and publish configs
            self.discovery = DiscoveryManager(self.mqtt, self.config.mqtt)
            await self.discovery.publish_discovery_configs()

            # Initialize state publisher
            self.publisher = StatePublisher(self.mqtt, self.config.mqtt)

            # Initialize serial connection
            self.serial = RS485Connection(self.config.serial)
            await self.serial.connect()

            # Start polling loop
            logger.info(
                f"Starting poll loop (interval={self.config.intellichem.poll_interval}s)"
            )
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
                        # Publish to MQTT
                        await self.publisher.publish_state(state)
                        self._stats["successful_polls"] += 1
                        self._stats["last_success"] = datetime.now()

                        # If we were in comms lost state, notify recovery
                        if was_comms_lost:
                            await self.publisher.publish_comms_restored()
                            was_comms_lost = False

                        logger.debug(
                            f"pH={state.ph.level:.2f}, "
                            f"ORP={state.orp.level}mV, "
                            f"temp={state.temperature}°F"
                        )
                    else:
                        logger.warning("Failed to parse status response")
                        self._stats["failed_polls"] += 1
                else:
                    # Timeout - no response
                    logger.warning(
                        f"No response from IntelliChem at address {address}"
                    )
                    self._stats["failed_polls"] += 1

                    if not was_comms_lost:
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
