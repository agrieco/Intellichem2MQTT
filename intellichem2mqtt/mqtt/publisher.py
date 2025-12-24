"""State publisher for MQTT."""

import logging
from datetime import datetime
from typing import Optional

from ..config import MQTTConfig
from ..models.intellichem import IntelliChemState
from .client import MQTTClient

logger = logging.getLogger(__name__)


class StatePublisher:
    """Publisher for IntelliChem state to MQTT.

    Publishes both individual sensor values and a complete
    JSON state object for flexible consumption.
    """

    def __init__(self, mqtt_client: MQTTClient, config: MQTTConfig):
        """Initialize the state publisher.

        Args:
            mqtt_client: Connected MQTT client
            config: MQTT configuration
        """
        self.client = mqtt_client
        self.config = config
        self._last_state: Optional[IntelliChemState] = None

    def _topic(self, *path: str) -> str:
        """Build a state topic.

        Args:
            path: Topic path components

        Returns:
            Full topic string
        """
        return f"{self.config.topic_prefix}/intellichem/{'/'.join(path)}"

    async def publish_state(self, state: IntelliChemState) -> None:
        """Publish complete IntelliChem state.

        Publishes both individual sensor topics and a complete
        JSON state topic.

        Args:
            state: Current IntelliChem state
        """
        # Update timestamp
        state.last_update = datetime.now()

        # Publish complete JSON state
        await self.client.publish_json(
            self._topic("status"),
            state.to_mqtt_dict(),
        )

        # Publish individual sensor values
        await self._publish_ph_state(state)
        await self._publish_orp_state(state)
        await self._publish_chemistry_state(state)
        await self._publish_alarms(state)
        await self._publish_warnings(state)

        self._last_state = state
        logger.debug("Published IntelliChem state")

    async def _publish_ph_state(self, state: IntelliChemState) -> None:
        """Publish pH-related state."""
        ph = state.ph

        await self.client.publish(self._topic("ph", "level"), round(ph.level, 2))
        await self.client.publish(self._topic("ph", "setpoint"), round(ph.setpoint, 2))
        await self.client.publish(self._topic("ph", "tank_level"), ph.tank_level)
        await self.client.publish(
            self._topic("ph", "tank_level_percent"),
            round(ph.tank_level_percent, 1),
        )
        await self.client.publish(self._topic("ph", "dose_time"), ph.dose_time)
        await self.client.publish(self._topic("ph", "dose_volume"), ph.dose_volume)
        await self.client.publish(self._topic("ph", "dosing_status"), str(ph.dosing_status))
        await self.client.publish(self._topic("ph", "is_dosing"), ph.is_dosing)

    async def _publish_orp_state(self, state: IntelliChemState) -> None:
        """Publish ORP-related state."""
        orp = state.orp

        await self.client.publish(self._topic("orp", "level"), int(orp.level))
        await self.client.publish(self._topic("orp", "setpoint"), int(orp.setpoint))
        await self.client.publish(self._topic("orp", "tank_level"), orp.tank_level)
        await self.client.publish(
            self._topic("orp", "tank_level_percent"),
            round(orp.tank_level_percent, 1),
        )
        await self.client.publish(self._topic("orp", "dose_time"), orp.dose_time)
        await self.client.publish(self._topic("orp", "dose_volume"), orp.dose_volume)
        await self.client.publish(self._topic("orp", "dosing_status"), str(orp.dosing_status))
        await self.client.publish(self._topic("orp", "is_dosing"), orp.is_dosing)

    async def _publish_chemistry_state(self, state: IntelliChemState) -> None:
        """Publish water chemistry state."""
        await self.client.publish(self._topic("lsi"), round(state.lsi, 2))
        await self.client.publish(self._topic("calcium_hardness"), state.calcium_hardness)
        await self.client.publish(self._topic("cyanuric_acid"), state.cyanuric_acid)
        await self.client.publish(self._topic("alkalinity"), state.alkalinity)
        await self.client.publish(self._topic("salt_level"), state.salt_level)
        await self.client.publish(self._topic("temperature"), state.temperature)
        await self.client.publish(self._topic("firmware"), state.firmware)
        await self.client.publish(self._topic("flow_detected"), state.flow_detected)
        await self.client.publish(self._topic("comms_lost"), state.comms_lost)

    async def _publish_alarms(self, state: IntelliChemState) -> None:
        """Publish alarm states."""
        alarms = state.alarms

        await self.client.publish(self._topic("alarms", "flow"), alarms.flow)
        await self.client.publish(self._topic("alarms", "ph_tank_empty"), alarms.ph_tank_empty)
        await self.client.publish(self._topic("alarms", "orp_tank_empty"), alarms.orp_tank_empty)
        await self.client.publish(self._topic("alarms", "probe_fault"), alarms.probe_fault)
        await self.client.publish(self._topic("alarms", "any_active"), alarms.any_active)

    async def _publish_warnings(self, state: IntelliChemState) -> None:
        """Publish warning states."""
        warnings = state.warnings

        await self.client.publish(self._topic("warnings", "ph_lockout"), warnings.ph_lockout)
        await self.client.publish(self._topic("warnings", "ph_daily_limit"), warnings.ph_daily_limit)
        await self.client.publish(self._topic("warnings", "orp_daily_limit"), warnings.orp_daily_limit)
        await self.client.publish(self._topic("warnings", "invalid_setup"), warnings.invalid_setup)
        await self.client.publish(
            self._topic("warnings", "chlorinator_comm_error"),
            warnings.chlorinator_comm_error,
        )
        await self.client.publish(
            self._topic("warnings", "water_chemistry"),
            str(warnings.water_chemistry),
        )
        await self.client.publish(self._topic("warnings", "any_active"), warnings.any_active)

    async def publish_comms_error(self) -> None:
        """Publish communication error state.

        Called when IntelliChem doesn't respond within timeout.
        """
        logger.warning("Publishing communication error state")

        await self.client.publish(self._topic("comms_lost"), True)
        await self.client.publish(self._topic("alarms", "comms"), True)

        # If we have a previous state, update it to show comms lost
        if self._last_state:
            self._last_state.comms_lost = True
            self._last_state.alarms.comms = True
            await self.client.publish_json(
                self._topic("status"),
                self._last_state.to_mqtt_dict(),
            )

    async def publish_comms_restored(self) -> None:
        """Publish communication restored state."""
        logger.info("Communication restored")

        await self.client.publish(self._topic("comms_lost"), False)
        await self.client.publish(self._topic("alarms", "comms"), False)

    @property
    def last_state(self) -> Optional[IntelliChemState]:
        """Get the last published state."""
        return self._last_state
