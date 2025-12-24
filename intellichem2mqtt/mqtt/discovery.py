"""Home Assistant MQTT Discovery configuration."""

import logging
from typing import Any

from ..config import MQTTConfig
from .client import MQTTClient

logger = logging.getLogger(__name__)


class DiscoveryManager:
    """Manager for Home Assistant MQTT Discovery.

    Generates and publishes discovery configs for all IntelliChem
    entities so they appear automatically in Home Assistant.
    """

    def __init__(self, mqtt_client: MQTTClient, config: MQTTConfig):
        """Initialize the discovery manager.

        Args:
            mqtt_client: Connected MQTT client
            config: MQTT configuration
        """
        self.client = mqtt_client
        self.config = config
        self._device_info = {
            "identifiers": ["intellichem_144"],
            "name": "IntelliChem",
            "manufacturer": "Pentair",
            "model": "IntelliChem",
            "suggested_area": "Pool",
        }

    def _discovery_topic(self, component: str, entity_id: str) -> str:
        """Build a discovery topic.

        Args:
            component: HA component type (sensor, binary_sensor, etc.)
            entity_id: Unique entity identifier

        Returns:
            Discovery topic string
        """
        return f"{self.config.discovery_prefix}/{component}/intellichem/{entity_id}/config"

    def _state_topic(self, *path: str) -> str:
        """Build a state topic.

        Args:
            path: Topic path components

        Returns:
            State topic string
        """
        return f"{self.config.topic_prefix}/intellichem/{'/'.join(path)}"

    def _base_config(self, name: str, entity_id: str) -> dict[str, Any]:
        """Build base discovery config.

        Args:
            name: Entity display name
            entity_id: Unique entity identifier

        Returns:
            Base config dictionary
        """
        return {
            "name": name,
            "unique_id": f"intellichem_144_{entity_id}",
            "availability_topic": self.client.availability_topic,
            "payload_available": "online",
            "payload_not_available": "offline",
            "device": self._device_info,
        }

    async def publish_discovery_configs(self) -> None:
        """Publish all discovery configs to Home Assistant."""
        logger.info("Publishing Home Assistant discovery configs")

        # Sensors
        await self._publish_sensors()

        # Binary sensors
        await self._publish_binary_sensors()

        # Text sensors for status displays
        await self._publish_text_sensors()

        logger.info("Discovery configs published")

    async def _publish_sensors(self) -> None:
        """Publish sensor discovery configs."""
        sensors = [
            # pH sensors
            {
                "name": "pH Level",
                "entity_id": "ph_level",
                "state_topic": self._state_topic("ph", "level"),
                "unit_of_measurement": "pH",
                "state_class": "measurement",
                "icon": "mdi:ph",
            },
            {
                "name": "pH Setpoint",
                "entity_id": "ph_setpoint",
                "state_topic": self._state_topic("ph", "setpoint"),
                "unit_of_measurement": "pH",
                "icon": "mdi:target",
            },
            {
                "name": "pH Tank Level",
                "entity_id": "ph_tank_level",
                "state_topic": self._state_topic("ph", "tank_level_percent"),
                "unit_of_measurement": "%",
                "icon": "mdi:car-coolant-level",
            },
            {
                "name": "pH Dose Time",
                "entity_id": "ph_dose_time",
                "state_topic": self._state_topic("ph", "dose_time"),
                "unit_of_measurement": "s",
                "device_class": "duration",
                "icon": "mdi:timer",
            },
            {
                "name": "pH Dose Volume",
                "entity_id": "ph_dose_volume",
                "state_topic": self._state_topic("ph", "dose_volume"),
                "unit_of_measurement": "mL",
                "icon": "mdi:beaker",
            },
            # ORP sensors
            {
                "name": "ORP Level",
                "entity_id": "orp_level",
                "state_topic": self._state_topic("orp", "level"),
                "unit_of_measurement": "mV",
                "device_class": "voltage",
                "state_class": "measurement",
                "icon": "mdi:flash",
            },
            {
                "name": "ORP Setpoint",
                "entity_id": "orp_setpoint",
                "state_topic": self._state_topic("orp", "setpoint"),
                "unit_of_measurement": "mV",
                "device_class": "voltage",
                "icon": "mdi:target",
            },
            {
                "name": "ORP Tank Level",
                "entity_id": "orp_tank_level",
                "state_topic": self._state_topic("orp", "tank_level_percent"),
                "unit_of_measurement": "%",
                "icon": "mdi:car-coolant-level",
            },
            {
                "name": "ORP Dose Time",
                "entity_id": "orp_dose_time",
                "state_topic": self._state_topic("orp", "dose_time"),
                "unit_of_measurement": "s",
                "device_class": "duration",
                "icon": "mdi:timer",
            },
            {
                "name": "ORP Dose Volume",
                "entity_id": "orp_dose_volume",
                "state_topic": self._state_topic("orp", "dose_volume"),
                "unit_of_measurement": "mL",
                "icon": "mdi:beaker",
            },
            # Water chemistry sensors
            {
                "name": "Temperature",
                "entity_id": "temperature",
                "state_topic": self._state_topic("temperature"),
                "unit_of_measurement": "°F",
                "device_class": "temperature",
                "state_class": "measurement",
            },
            {
                "name": "Saturation Index (LSI)",
                "entity_id": "lsi",
                "state_topic": self._state_topic("lsi"),
                "state_class": "measurement",
                "icon": "mdi:water-percent",
            },
            {
                "name": "Calcium Hardness",
                "entity_id": "calcium_hardness",
                "state_topic": self._state_topic("calcium_hardness"),
                "unit_of_measurement": "ppm",
                "state_class": "measurement",
                "icon": "mdi:flask",
            },
            {
                "name": "Cyanuric Acid",
                "entity_id": "cyanuric_acid",
                "state_topic": self._state_topic("cyanuric_acid"),
                "unit_of_measurement": "ppm",
                "state_class": "measurement",
                "icon": "mdi:flask",
            },
            {
                "name": "Alkalinity",
                "entity_id": "alkalinity",
                "state_topic": self._state_topic("alkalinity"),
                "unit_of_measurement": "ppm",
                "state_class": "measurement",
                "icon": "mdi:flask",
            },
            {
                "name": "Salt Level",
                "entity_id": "salt_level",
                "state_topic": self._state_topic("salt_level"),
                "unit_of_measurement": "ppm",
                "state_class": "measurement",
                "icon": "mdi:shaker",
            },
            {
                "name": "Firmware",
                "entity_id": "firmware",
                "state_topic": self._state_topic("firmware"),
                "icon": "mdi:chip",
            },
        ]

        for sensor in sensors:
            config = self._base_config(sensor["name"], sensor["entity_id"])
            config["state_topic"] = sensor["state_topic"]

            for key in ["unit_of_measurement", "device_class", "state_class", "icon"]:
                if key in sensor:
                    config[key] = sensor[key]

            topic = self._discovery_topic("sensor", sensor["entity_id"])
            await self.client.publish_json(topic, config, retain=True)

    async def _publish_binary_sensors(self) -> None:
        """Publish binary sensor discovery configs."""
        binary_sensors = [
            {
                "name": "Flow Detected",
                "entity_id": "flow_detected",
                "state_topic": self._state_topic("flow_detected"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "running",
                "icon": "mdi:water",
            },
            {
                "name": "Flow Alarm",
                "entity_id": "flow_alarm",
                "state_topic": self._state_topic("alarms", "flow"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
            },
            {
                "name": "pH Tank Empty",
                "entity_id": "ph_tank_empty",
                "state_topic": self._state_topic("alarms", "ph_tank_empty"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
                "icon": "mdi:car-coolant-level",
            },
            {
                "name": "ORP Tank Empty",
                "entity_id": "orp_tank_empty",
                "state_topic": self._state_topic("alarms", "orp_tank_empty"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
                "icon": "mdi:car-coolant-level",
            },
            {
                "name": "Probe Fault",
                "entity_id": "probe_fault",
                "state_topic": self._state_topic("alarms", "probe_fault"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
            },
            {
                "name": "Communication Lost",
                "entity_id": "comms_lost",
                "state_topic": self._state_topic("comms_lost"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "connectivity",
            },
            {
                "name": "pH Lockout",
                "entity_id": "ph_lockout",
                "state_topic": self._state_topic("warnings", "ph_lockout"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
            },
            {
                "name": "pH Daily Limit",
                "entity_id": "ph_daily_limit",
                "state_topic": self._state_topic("warnings", "ph_daily_limit"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
            },
            {
                "name": "ORP Daily Limit",
                "entity_id": "orp_daily_limit",
                "state_topic": self._state_topic("warnings", "orp_daily_limit"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "problem",
            },
            {
                "name": "pH Dosing",
                "entity_id": "ph_dosing",
                "state_topic": self._state_topic("ph", "is_dosing"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "running",
                "icon": "mdi:water-pump",
            },
            {
                "name": "ORP Dosing",
                "entity_id": "orp_dosing",
                "state_topic": self._state_topic("orp", "is_dosing"),
                "payload_on": "true",
                "payload_off": "false",
                "device_class": "running",
                "icon": "mdi:water-pump",
            },
        ]

        for sensor in binary_sensors:
            config = self._base_config(sensor["name"], sensor["entity_id"])
            config["state_topic"] = sensor["state_topic"]
            config["payload_on"] = sensor["payload_on"]
            config["payload_off"] = sensor["payload_off"]

            for key in ["device_class", "icon"]:
                if key in sensor:
                    config[key] = sensor[key]

            topic = self._discovery_topic("binary_sensor", sensor["entity_id"])
            await self.client.publish_json(topic, config, retain=True)

    async def _publish_text_sensors(self) -> None:
        """Publish text sensor discovery configs for status displays."""
        text_sensors = [
            {
                "name": "pH Dosing Status",
                "entity_id": "ph_dosing_status",
                "state_topic": self._state_topic("ph", "dosing_status"),
                "icon": "mdi:information",
            },
            {
                "name": "ORP Dosing Status",
                "entity_id": "orp_dosing_status",
                "state_topic": self._state_topic("orp", "dosing_status"),
                "icon": "mdi:information",
            },
            {
                "name": "Water Chemistry",
                "entity_id": "water_chemistry",
                "state_topic": self._state_topic("warnings", "water_chemistry"),
                "icon": "mdi:water-alert",
            },
        ]

        for sensor in text_sensors:
            config = self._base_config(sensor["name"], sensor["entity_id"])
            config["state_topic"] = sensor["state_topic"]
            if "icon" in sensor:
                config["icon"] = sensor["icon"]

            topic = self._discovery_topic("sensor", sensor["entity_id"])
            await self.client.publish_json(topic, config, retain=True)

    async def remove_discovery_configs(self) -> None:
        """Remove all discovery configs from Home Assistant."""
        logger.info("Removing Home Assistant discovery configs")

        entity_ids = [
            # Sensors
            "ph_level", "ph_setpoint", "ph_tank_level", "ph_dose_time", "ph_dose_volume",
            "orp_level", "orp_setpoint", "orp_tank_level", "orp_dose_time", "orp_dose_volume",
            "temperature", "lsi", "calcium_hardness", "cyanuric_acid", "alkalinity",
            "salt_level", "firmware",
            "ph_dosing_status", "orp_dosing_status", "water_chemistry",
        ]

        binary_ids = [
            "flow_detected", "flow_alarm", "ph_tank_empty", "orp_tank_empty",
            "probe_fault", "comms_lost", "ph_lockout", "ph_daily_limit",
            "orp_daily_limit", "ph_dosing", "orp_dosing",
        ]

        for entity_id in entity_ids:
            topic = self._discovery_topic("sensor", entity_id)
            await self.client.publish(topic, "", retain=True)

        for entity_id in binary_ids:
            topic = self._discovery_topic("binary_sensor", entity_id)
            await self.client.publish(topic, "", retain=True)

        logger.info("Discovery configs removed")
