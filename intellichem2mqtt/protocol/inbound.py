"""Inbound message parser for IntelliChem status responses."""

import logging
from typing import Optional

from .message import Message
from .constants import (
    ACTION_STATUS_RESPONSE,
    STATUS_PAYLOAD_LENGTH,
    INTELLICHEM_ADDRESS_MIN,
    INTELLICHEM_ADDRESS_MAX,
    ALARM_FLOW,
    ALARM_PH_TANK_EMPTY,
    ALARM_ORP_TANK_EMPTY,
    ALARM_PROBE_FAULT,
    WARNING_PH_LOCKOUT,
    WARNING_PH_DAILY_LIMIT,
    WARNING_ORP_DAILY_LIMIT,
    WARNING_INVALID_SETUP,
    WARNING_CHLORINATOR_COMM,
    STATUS_COMMS_LOST,
)
from ..models.intellichem import (
    IntelliChemState,
    ChemicalState,
    Alarms,
    Warnings,
    DosingStatus,
    WaterChemistry,
)

logger = logging.getLogger(__name__)

# Enable VERY verbose debugging
DEBUG_PARSER = True


class StatusResponseParser:
    """Parser for IntelliChem Action 18 status response messages.

    The status message contains 41 bytes of payload with pH, ORP,
    tank levels, alarms, warnings, and configuration data.

    Payload byte mapping (from IntelliChemStateMessage.ts):
        Bytes 0-1:   pH level (Big Endian) / 100
        Bytes 2-3:   ORP level (Big Endian) mV
        Bytes 4-5:   pH setpoint / 100
        Bytes 6-7:   ORP setpoint mV
        Bytes 10-11: pH dose time (seconds)
        Bytes 14-15: ORP dose time (seconds)
        Bytes 16-17: pH dose volume (mL)
        Bytes 18-19: ORP dose volume (mL)
        Byte 20:     pH tank level (1-7, 0=no tank)
        Byte 21:     ORP tank level (1-7, 0=no tank)
        Byte 22:     LSI (signed, bit 7 = negative)
        Bytes 23-24: Calcium Hardness (ppm)
        Byte 26:     Cyanuric Acid (ppm)
        Bytes 27-28: Alkalinity (ppm)
        Byte 29:     Salt level * 50 (ppm)
        Byte 31:     Temperature
        Byte 32:     Alarms (bitfield)
        Byte 33:     Warnings (bitfield)
        Byte 34:     Dosing status (bitfield)
        Byte 35:     Status flags (bitfield)
        Bytes 36-37: Firmware version
        Byte 38:     Water chemistry (0=OK, 1=Corrosive, 2=Scaling)
    """

    def parse(self, packet: bytes) -> Optional[IntelliChemState]:
        """Parse a complete status response packet.

        Args:
            packet: Complete packet bytes including preamble and checksum

        Returns:
            IntelliChemState if valid, None if invalid or not a status response
        """
        if DEBUG_PARSER:
            logger.info("")
            logger.info("[PARSER] ========== PARSING PACKET ==========")
            logger.info(f"[PARSER] Packet length: {len(packet)} bytes")
            logger.info(f"[PARSER] Packet hex: {packet.hex()}")
            logger.info(f"[PARSER] Packet raw: {list(packet)}")

        # Validate checksum
        if not Message.validate_checksum(packet):
            if DEBUG_PARSER:
                data = packet[3:-2] if len(packet) >= 11 else b''
                calculated = sum(data) if data else 0
                received = (packet[-2] << 8) | packet[-1] if len(packet) >= 2 else 0
                logger.warning(f"[PARSER] !!! CHECKSUM VALIDATION FAILED !!!")
                logger.warning(f"[PARSER]   Calculated: {calculated} (0x{calculated:04x})")
                logger.warning(f"[PARSER]   Received:   {received} (0x{received:04x})")
            else:
                logger.warning("Invalid checksum in status response")
            return None

        if DEBUG_PARSER:
            logger.info("[PARSER] Checksum valid ✓")

        # Check action code
        action = Message.get_action(packet)
        if DEBUG_PARSER:
            logger.info(f"[PARSER] Action code: {action} (expected {ACTION_STATUS_RESPONSE} for status response)")

        if action != ACTION_STATUS_RESPONSE:
            if DEBUG_PARSER:
                logger.info(f"[PARSER] Not a status response, action={action}")
                logger.info(f"[PARSER]   Known actions: 18=status_response, 210=status_request, 146=config, 147=ocp_broadcast")
            else:
                logger.debug(f"Not a status response (action={action})")
            return None

        if DEBUG_PARSER:
            logger.info("[PARSER] Action code valid ✓")

        # Validate source is IntelliChem
        source = Message.get_source(packet)
        dest = Message.get_dest(packet)
        if DEBUG_PARSER:
            logger.info(f"[PARSER] Source: {source} (expected {INTELLICHEM_ADDRESS_MIN}-{INTELLICHEM_ADDRESS_MAX})")
            logger.info(f"[PARSER] Dest:   {dest}")

        if not (INTELLICHEM_ADDRESS_MIN <= source <= INTELLICHEM_ADDRESS_MAX):
            if DEBUG_PARSER:
                logger.warning(f"[PARSER] !!! INVALID SOURCE ADDRESS !!!")
                logger.warning(f"[PARSER]   Got: {source}")
                logger.warning(f"[PARSER]   Expected: {INTELLICHEM_ADDRESS_MIN}-{INTELLICHEM_ADDRESS_MAX}")
            else:
                logger.warning(f"Invalid source address: {source}")
            return None

        if DEBUG_PARSER:
            logger.info("[PARSER] Source address valid ✓")

        # Extract payload
        payload = Message.extract_payload(packet)
        if DEBUG_PARSER:
            logger.info(f"[PARSER] Payload length: {len(payload)} (expected >= {STATUS_PAYLOAD_LENGTH})")
            logger.info(f"[PARSER] Payload hex: {payload.hex()}")
            logger.info(f"[PARSER] Payload raw: {list(payload)}")

        if len(payload) < STATUS_PAYLOAD_LENGTH:
            if DEBUG_PARSER:
                logger.warning(f"[PARSER] !!! PAYLOAD TOO SHORT !!!")
            else:
                logger.warning(
                    f"Payload too short: {len(payload)} < {STATUS_PAYLOAD_LENGTH}"
                )
            return None

        if DEBUG_PARSER:
            logger.info("[PARSER] Payload length valid ✓")
            logger.info("[PARSER] All validation passed, parsing payload...")

        result = self._parse_payload(payload, source)

        if DEBUG_PARSER:
            logger.info("[PARSER] ========== PARSE COMPLETE ==========")
            logger.info(f"[PARSER] pH: {result.ph.level:.2f} (setpoint: {result.ph.setpoint:.2f})")
            logger.info(f"[PARSER] ORP: {result.orp.level} mV (setpoint: {result.orp.setpoint})")
            logger.info(f"[PARSER] Temp: {result.temperature}°F, LSI: {result.lsi:.2f}")
            logger.info(f"[PARSER] Flow: {result.flow_detected}, Comms Lost: {result.comms_lost}")

        return result

    def _parse_payload(self, payload: bytes, address: int) -> IntelliChemState:
        """Parse the 41-byte status payload.

        Args:
            payload: 41-byte payload
            address: IntelliChem address (144-158)

        Returns:
            Parsed IntelliChemState
        """
        # Helper for big-endian 16-bit values
        def be16(offset: int) -> int:
            return (payload[offset] << 8) | payload[offset + 1]

        # Parse pH data
        ph_level = be16(0) / 100.0
        ph_setpoint = be16(4) / 100.0
        ph_dose_time = be16(10)
        ph_dose_volume = be16(16)
        ph_tank_level = max(payload[20] - 1, 0) if payload[20] > 0 else 0

        # Parse ORP data
        orp_level = be16(2)
        orp_setpoint = be16(6)
        orp_dose_time = be16(14)
        orp_dose_volume = be16(18)
        orp_tank_level = max(payload[21] - 1, 0) if payload[21] > 0 else 0

        # Parse dosing status (byte 34)
        dosing_byte = payload[34]
        ph_doser_type = dosing_byte & 0x03
        orp_doser_type = (dosing_byte & 0x0C) >> 2
        ph_dosing_raw = (dosing_byte & 0x30) >> 4
        orp_dosing_raw = (dosing_byte & 0xC0) >> 6

        # Map dosing status values
        ph_dosing_status = DosingStatus(min(ph_dosing_raw, 2))
        orp_dosing_status = DosingStatus(min(orp_dosing_raw, 2))

        ph_state = ChemicalState(
            level=ph_level,
            setpoint=ph_setpoint,
            dose_time=ph_dose_time,
            dose_volume=ph_dose_volume,
            tank_level=ph_tank_level,
            dosing_status=ph_dosing_status,
            is_dosing=(ph_dosing_status == DosingStatus.DOSING and ph_doser_type != 0),
        )

        orp_state = ChemicalState(
            level=float(orp_level),
            setpoint=float(orp_setpoint),
            dose_time=orp_dose_time,
            dose_volume=orp_dose_volume,
            tank_level=orp_tank_level,
            dosing_status=orp_dosing_status,
            is_dosing=(orp_dosing_status == DosingStatus.DOSING and orp_doser_type != 0),
        )

        # Parse LSI (byte 22) - signed value
        lsi_byte = payload[22]
        if lsi_byte & 0x80:  # High bit set = negative
            lsi = (256 - lsi_byte) / -100.0
        else:
            lsi = lsi_byte / 100.0

        # Parse water chemistry values
        calcium_hardness = be16(23)
        cyanuric_acid = payload[26]
        alkalinity = be16(27)
        salt_level = payload[29] * 50
        temperature = payload[31]

        # Parse alarms (byte 32)
        alarm_byte = payload[32]
        alarms = Alarms(
            flow=(alarm_byte & ALARM_FLOW) != 0,
            ph_tank_empty=(alarm_byte & ALARM_PH_TANK_EMPTY) != 0,
            orp_tank_empty=(alarm_byte & ALARM_ORP_TANK_EMPTY) != 0,
            probe_fault=(alarm_byte & ALARM_PROBE_FAULT) != 0,
        )

        # Parse warnings (byte 33)
        warning_byte = payload[33]
        warnings = Warnings(
            ph_lockout=(warning_byte & WARNING_PH_LOCKOUT) != 0,
            ph_daily_limit=(warning_byte & WARNING_PH_DAILY_LIMIT) != 0,
            orp_daily_limit=(warning_byte & WARNING_ORP_DAILY_LIMIT) != 0,
            invalid_setup=(warning_byte & WARNING_INVALID_SETUP) != 0,
            chlorinator_comm_error=(warning_byte & WARNING_CHLORINATOR_COMM) != 0,
        )

        # Parse water chemistry warning (byte 38)
        water_chem_byte = payload[38]
        water_chemistry = WaterChemistry(min(water_chem_byte, 2))
        warnings.water_chemistry = water_chemistry

        # Parse firmware (bytes 36-37)
        firmware = f"{payload[37]}.{payload[36]:03d}"

        # Parse status flags (byte 35)
        status_byte = payload[35]
        comms_lost = (status_byte & STATUS_COMMS_LOST) != 0

        # Flow detected is inverse of flow alarm
        flow_detected = not alarms.flow

        return IntelliChemState(
            address=address,
            ph=ph_state,
            orp=orp_state,
            lsi=lsi,
            calcium_hardness=calcium_hardness,
            cyanuric_acid=cyanuric_acid,
            alkalinity=alkalinity,
            salt_level=salt_level,
            temperature=temperature,
            firmware=firmware,
            alarms=alarms,
            warnings=warnings,
            flow_detected=flow_detected,
            comms_lost=comms_lost,
        )
