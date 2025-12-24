"""Async MQTT client wrapper."""

import asyncio
import logging
import json
from typing import Optional, Any

import aiomqtt

from ..config import MQTTConfig

logger = logging.getLogger(__name__)


class MQTTClient:
    """Async MQTT client for Home Assistant integration.

    Wraps aiomqtt with connection management, reconnection,
    and helper methods for JSON publishing.
    """

    def __init__(self, config: MQTTConfig):
        """Initialize the MQTT client.

        Args:
            config: MQTT configuration
        """
        self.config = config
        self._client: Optional[aiomqtt.Client] = None
        self._connected = False
        self._reconnect_interval = 5.0

    @property
    def connected(self) -> bool:
        """Check if connected to MQTT broker."""
        return self._connected

    @property
    def availability_topic(self) -> str:
        """Get the availability topic."""
        return f"{self.config.topic_prefix}/intellichem/availability"

    async def connect(self) -> None:
        """Connect to the MQTT broker.

        Raises:
            MqttError: If connection fails
        """
        logger.info(f"Connecting to MQTT broker at {self.config.host}:{self.config.port}")

        try:
            self._client = aiomqtt.Client(
                hostname=self.config.host,
                port=self.config.port,
                username=self.config.username,
                password=self.config.password,
                identifier=self.config.client_id,
                # Last Will and Testament for availability
                will=aiomqtt.Will(
                    topic=self.availability_topic,
                    payload="offline",
                    qos=self.config.qos,
                    retain=True,
                ),
            )
            await self._client.__aenter__()
            self._connected = True
            logger.info("Connected to MQTT broker")

        except Exception as e:
            logger.error(f"Failed to connect to MQTT broker: {e}")
            raise

    async def disconnect(self) -> None:
        """Disconnect from the MQTT broker."""
        if self._client:
            try:
                # Publish offline status before disconnecting
                await self.publish(self.availability_topic, "offline", retain=True)
            except Exception:
                pass

            try:
                await self._client.__aexit__(None, None, None)
            except Exception:
                pass

            self._client = None
            self._connected = False
            logger.info("Disconnected from MQTT broker")

    async def publish(
        self,
        topic: str,
        payload: Any,
        retain: Optional[bool] = None,
        qos: Optional[int] = None,
    ) -> None:
        """Publish a message to a topic.

        Args:
            topic: MQTT topic
            payload: Message payload (will be JSON-encoded if dict/list)
            retain: Whether to retain the message (default from config)
            qos: QoS level (default from config)
        """
        if not self._client or not self._connected:
            raise ConnectionError("Not connected to MQTT broker")

        # Use config defaults if not specified
        if retain is None:
            retain = self.config.retain
        if qos is None:
            qos = self.config.qos

        # Convert payload to string
        if isinstance(payload, (dict, list)):
            payload_str = json.dumps(payload)
        elif isinstance(payload, bool):
            payload_str = "true" if payload else "false"
        elif payload is None:
            payload_str = ""
        else:
            payload_str = str(payload)

        await self._client.publish(
            topic,
            payload=payload_str,
            qos=qos,
            retain=retain,
        )
        logger.debug(f"Published to {topic}: {payload_str[:100]}")

    async def publish_json(
        self,
        topic: str,
        data: dict,
        retain: Optional[bool] = None,
    ) -> None:
        """Publish a JSON message.

        Args:
            topic: MQTT topic
            data: Dictionary to publish as JSON
            retain: Whether to retain the message
        """
        await self.publish(topic, data, retain=retain)

    async def publish_availability(self, status: str) -> None:
        """Publish availability status.

        Args:
            status: "online" or "offline"
        """
        await self.publish(
            self.availability_topic,
            status,
            retain=True,
        )
        logger.info(f"Published availability: {status}")

    async def subscribe(self, topic: str) -> None:
        """Subscribe to a topic.

        Args:
            topic: MQTT topic pattern
        """
        if not self._client or not self._connected:
            raise ConnectionError("Not connected to MQTT broker")

        await self._client.subscribe(topic, qos=self.config.qos)
        logger.debug(f"Subscribed to {topic}")

    def topic(self, *parts: str) -> str:
        """Build a topic with the configured prefix.

        Args:
            parts: Topic path components

        Returns:
            Full topic string
        """
        return "/".join([self.config.topic_prefix, *parts])
