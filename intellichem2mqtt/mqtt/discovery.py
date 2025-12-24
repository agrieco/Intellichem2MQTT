"""Home Assistant MQTT Discovery configuration."""

import logging
from typing import Any

from ..config import MQTTConfig
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
from .client import MQTTClient

logger = logging.getLogger(__name__)


class DiscoveryManager:
    """Manager for Home Assistant MQTT Discovery.

    Generates and publishes discovery configs for all IntelliChem
    entities so they appear automatically in Home Assistant.
    """

    def __init__(
        self,
        mqtt_client: MQTTClient,
        config: MQTTConfig,
        control_enabled: bool = False,
    ):
        """Initialize the discovery manager.

        Args:
            mqtt_client: Connected MQTT client
            config: MQTT configuration
            control_enabled: Whether control features are enabled
        """
        self.client = mqtt_client
        self.config = config
        self.control_enabled = control_enabled
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

    def _command_topic(self, command: str) -> str:
        """Build a command topic.

        Args:
            command: Command name (e.g., 'ph_setpoint')

        Returns:
            Command topic string
        """
        return f"{self.config.topic_prefix}/intellichem/set/{command}"

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

        # Control entities (if enabled)
        if self.control_enabled:
            await self._publish_number_entities()
            await self._publish_switch_entities()
            logger.info("Control entities published (control enabled)")

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

    async def _publish_number_entities(self) -> None:
        """Publish number entity discovery configs for setpoint control."""
        numbers = [
            # pH setpoint control
            {
                "name": "pH Setpoint Control",
                "entity_id": "ph_setpoint_control",
                "command_topic": self._command_topic("ph_setpoint"),
                "state_topic": self._state_topic("ph", "setpoint"),
                "min": PH_SETPOINT_MIN,
                "max": PH_SETPOINT_MAX,
                "step": 0.1,
                "unit_of_measurement": "pH",
                "icon": "mdi:target",
                "mode": "slider",
            },
            # ORP setpoint control
            {
                "name": "ORP Setpoint Control",
                "entity_id": "orp_setpoint_control",
                "command_topic": self._command_topic("orp_setpoint"),
                "state_topic": self._state_topic("orp", "setpoint"),
                "min": ORP_SETPOINT_MIN,
                "max": ORP_SETPOINT_MAX,
                "step": 10,
                "unit_of_measurement": "mV",
                "icon": "mdi:target",
                "mode": "slider",
            },
            # Calcium hardness
            {
                "name": "Calcium Hardness Setting",
                "entity_id": "calcium_hardness_control",
                "command_topic": self._command_topic("calcium_hardness"),
                "state_topic": self._state_topic("calcium_hardness"),
                "min": CALCIUM_HARDNESS_MIN,
                "max": CALCIUM_HARDNESS_MAX,
                "step": 25,
                "unit_of_measurement": "ppm",
                "icon": "mdi:flask",
                "mode": "box",
            },
            # Cyanuric acid
            {
                "name": "Cyanuric Acid Setting",
                "entity_id": "cyanuric_acid_control",
                "command_topic": self._command_topic("cyanuric_acid"),
                "state_topic": self._state_topic("cyanuric_acid"),
                "min": CYANURIC_ACID_MIN,
                "max": CYANURIC_ACID_MAX,
                "step": 10,
                "unit_of_measurement": "ppm",
                "icon": "mdi:flask",
                "mode": "box",
            },
            # Alkalinity
            {
                "name": "Alkalinity Setting",
                "entity_id": "alkalinity_control",
                "command_topic": self._command_topic("alkalinity"),
                "state_topic": self._state_topic("alkalinity"),
                "min": ALKALINITY_MIN,
                "max": ALKALINITY_MAX,
                "step": 10,
                "unit_of_measurement": "ppm",
                "icon": "mdi:flask",
                "mode": "box",
            },
        ]

        for number in numbers:
            config = self._base_config(number["name"], number["entity_id"])
            config["command_topic"] = number["command_topic"]
            config["state_topic"] = number["state_topic"]
            config["min"] = number["min"]
            config["max"] = number["max"]
            config["step"] = number["step"]

            for key in ["unit_of_measurement", "icon", "mode"]:
                if key in number:
                    config[key] = number[key]

            topic = self._discovery_topic("number", number["entity_id"])
            await self.client.publish_json(topic, config, retain=True)

        logger.debug(f"Published {len(numbers)} number entities")

    async def _publish_switch_entities(self) -> None:
        """Publish switch entity discovery configs for dosing control."""
        switches = [
            # pH dosing enable/disable
            {
                "name": "pH Dosing Enable",
                "entity_id": "ph_dosing_enable",
                "command_topic": self._command_topic("ph_dosing"),
                "state_topic": self._state_topic("ph", "dosing_enabled"),
                "payload_on": "ON",
                "payload_off": "OFF",
                "state_on": "true",
                "state_off": "false",
                "icon": "mdi:flask-outline",
            },
            # ORP dosing enable/disable
            {
                "name": "ORP Dosing Enable",
                "entity_id": "orp_dosing_enable",
                "command_topic": self._command_topic("orp_dosing"),
                "state_topic": self._state_topic("orp", "dosing_enabled"),
                "payload_on": "ON",
                "payload_off": "OFF",
                "state_on": "true",
                "state_off": "false",
                "icon": "mdi:flask-outline",
            },
        ]

        for switch in switches:
            config = self._base_config(switch["name"], switch["entity_id"])
            config["command_topic"] = switch["command_topic"]
            config["state_topic"] = switch["state_topic"]
            config["payload_on"] = switch["payload_on"]
            config["payload_off"] = switch["payload_off"]
            config["state_on"] = switch["state_on"]
            config["state_off"] = switch["state_off"]

            if "icon" in switch:
                config["icon"] = switch["icon"]

            topic = self._discovery_topic("switch", switch["entity_id"])
            await self.client.publish_json(topic, config, retain=True)

        logger.debug(f"Published {len(switches)} switch entities")

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

        number_ids = [
            "ph_setpoint_control", "orp_setpoint_control",
            "calcium_hardness_control", "cyanuric_acid_control", "alkalinity_control",
        ]

        switch_ids = [
            "ph_dosing_enable", "orp_dosing_enable",
        ]

        for entity_id in entity_ids:
            topic = self._discovery_topic("sensor", entity_id)
            await self.client.publish(topic, "", retain=True)

        for entity_id in binary_ids:
            topic = self._discovery_topic("binary_sensor", entity_id)
            await self.client.publish(topic, "", retain=True)

        for entity_id in number_ids:
            topic = self._discovery_topic("number", entity_id)
            await self.client.publish(topic, "", retain=True)

        for entity_id in switch_ids:
            topic = self._discovery_topic("switch", entity_id)
            await self.client.publish(topic, "", retain=True)

        logger.info("Discovery configs removed")
