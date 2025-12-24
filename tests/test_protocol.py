"""Tests for protocol encoding and decoding."""

import pytest
from intellichem2mqtt.protocol.constants import (
    PREAMBLE,
    ACTION_STATUS_REQUEST,
    ACTION_STATUS_RESPONSE,
    DEFAULT_INTELLICHEM_ADDRESS,
    CONTROLLER_ADDRESS,
)
from intellichem2mqtt.protocol.message import Message
from intellichem2mqtt.protocol.outbound import StatusRequestMessage
from intellichem2mqtt.protocol.inbound import StatusResponseParser
from intellichem2mqtt.models.intellichem import DosingStatus, WaterChemistry


class TestMessage:
    """Tests for base Message class."""

    def test_checksum_calculation(self):
        """Test checksum is calculated correctly."""
        msg = Message(
            dest=144,
            source=16,
            action=210,
            payload=bytes([210]),
        )

        # Header: [165, 0, 144, 16, 210, 1]
        # Payload: [210]
        # Sum: 165 + 0 + 144 + 16 + 210 + 1 + 210 = 746
        assert msg.checksum == 746
        assert msg.checksum_bytes == bytes([2, 234])  # 746 = 0x02EA

    def test_to_bytes(self):
        """Test packet serialization."""
        msg = Message(
            dest=144,
            source=16,
            action=210,
            payload=bytes([210]),
        )

        packet = msg.to_bytes()

        # Preamble
        assert packet[:3] == PREAMBLE

        # Header
        assert packet[3] == 165  # Start byte
        assert packet[4] == 0    # Sub byte
        assert packet[5] == 144  # Dest
        assert packet[6] == 16   # Source
        assert packet[7] == 210  # Action
        assert packet[8] == 1    # Payload length

        # Payload
        assert packet[9] == 210

        # Checksum
        assert packet[10:12] == bytes([2, 234])

    def test_validate_checksum_valid(self):
        """Test checksum validation with valid packet."""
        # Valid packet from IntelliChem
        packet = bytes([
            255, 0, 255,          # Preamble
            165, 0, 16, 144, 18, 41,  # Header
        ] + [0] * 41 + [          # 41-byte payload (all zeros)
            2, 97,                # Checksum (sum of header + zeros = 344 + 0 = 609 = 0x0261)
        ])

        # Recalculate expected checksum
        data_sum = sum(packet[3:-2])  # 165+0+16+144+18+41 = 384, plus 41 zeros
        expected = (data_sum >> 8, data_sum & 0xFF)

        # Fix checksum for test
        packet = packet[:-2] + bytes(expected)

        assert Message.validate_checksum(packet)

    def test_validate_checksum_invalid(self):
        """Test checksum validation with invalid packet."""
        packet = bytes([
            255, 0, 255,
            165, 0, 16, 144, 18, 2,
            0, 0,
            0, 0,  # Wrong checksum
        ])
        assert not Message.validate_checksum(packet)

    def test_extract_payload(self):
        """Test payload extraction."""
        packet = bytes([
            255, 0, 255,          # Preamble
            165, 0, 16, 144, 18, 3,  # Header (payload len = 3)
            1, 2, 3,              # Payload
            0, 0,                 # Checksum
        ])

        payload = Message.extract_payload(packet)
        assert payload == bytes([1, 2, 3])

    def test_get_action(self):
        """Test action extraction."""
        packet = bytes([255, 0, 255, 165, 0, 16, 144, 18, 0])
        assert Message.get_action(packet) == 18

    def test_get_source(self):
        """Test source extraction."""
        packet = bytes([255, 0, 255, 165, 0, 16, 144, 18, 0])
        assert Message.get_source(packet) == 144

    def test_get_dest(self):
        """Test destination extraction."""
        packet = bytes([255, 0, 255, 165, 0, 16, 144, 18, 0])
        assert Message.get_dest(packet) == 16


class TestStatusRequestMessage:
    """Tests for status request message builder."""

    def test_default_address(self):
        """Test request with default address."""
        msg = StatusRequestMessage()

        assert msg.dest == DEFAULT_INTELLICHEM_ADDRESS
        assert msg.source == CONTROLLER_ADDRESS
        assert msg.action == ACTION_STATUS_REQUEST
        assert msg.payload == bytes([210])

    def test_custom_address(self):
        """Test request with custom address."""
        msg = StatusRequestMessage(intellichem_address=145)
        assert msg.dest == 145

    def test_packet_format(self):
        """Test complete packet format."""
        msg = StatusRequestMessage()
        packet = msg.to_bytes()

        # Should be 12 bytes: 3 preamble + 6 header + 1 payload + 2 checksum
        assert len(packet) == 12

        # Verify structure
        assert packet[:3] == PREAMBLE
        assert packet[3] == 165
        assert packet[5] == 144  # Dest = IntelliChem
        assert packet[6] == 16   # Source = Controller
        assert packet[7] == 210  # Action
        assert packet[9] == 210  # Payload = action echo


class TestStatusResponseParser:
    """Tests for status response parser."""

    def create_test_packet(self, payload: bytes) -> bytes:
        """Create a valid test packet with correct checksum."""
        header = bytes([165, 0, 16, 144, 18, len(payload)])
        data = header + payload
        checksum = sum(data)
        chk_bytes = bytes([(checksum >> 8) & 0xFF, checksum & 0xFF])
        return PREAMBLE + data + chk_bytes

    def test_parse_valid_response(self):
        """Test parsing a valid status response."""
        # Sample payload with realistic values
        payload = bytes([
            2, 238,    # pH = 750 / 100 = 7.50
            2, 188,    # ORP = 700 mV
            2, 248,    # pH setpoint = 760 / 100 = 7.60
            2, 188,    # ORP setpoint = 700 mV
            0, 0,      # Reserved
            0, 2,      # pH dose time = 2 seconds
            0, 0,      # Reserved
            0, 5,      # ORP dose time = 5 seconds
            0, 10,     # pH dose volume = 10 mL
            0, 20,     # ORP dose volume = 20 mL
            6,         # pH tank level = 5 (6-1)
            5,         # ORP tank level = 4 (5-1)
            3,         # LSI = 0.03
            0, 250,    # Calcium hardness = 250 ppm
            0,         # Reserved
            50,        # CYA = 50 ppm
            0, 100,    # Alkalinity = 100 ppm
            60,        # Salt = 3000 ppm (60 * 50)
            0,         # Reserved
            82,        # Temperature = 82°F
            0,         # Alarms = 0 (none)
            0,         # Warnings = 0 (none)
            0x51,      # Dosing: pH=monitoring(1), ORP=monitoring(1), types set
            0,         # Status flags
            80, 1,     # Firmware = 1.080
            0,         # Water chemistry = OK
            0, 0,      # Reserved
        ])

        packet = self.create_test_packet(payload)
        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is not None
        assert state.address == 144

        # pH values
        assert abs(state.ph.level - 7.50) < 0.01
        assert abs(state.ph.setpoint - 7.60) < 0.01
        assert state.ph.tank_level == 5
        assert state.ph.dose_time == 2
        assert state.ph.dose_volume == 10

        # ORP values
        assert state.orp.level == 700
        assert state.orp.setpoint == 700
        assert state.orp.tank_level == 4
        assert state.orp.dose_time == 5
        assert state.orp.dose_volume == 20

        # Water chemistry
        assert abs(state.lsi - 0.03) < 0.001
        assert state.calcium_hardness == 250
        assert state.cyanuric_acid == 50
        assert state.alkalinity == 100
        assert state.salt_level == 3000
        assert state.temperature == 82

        # Status
        assert state.firmware == "1.080"
        assert not state.alarms.any_active
        assert not state.warnings.any_active

    def test_parse_negative_lsi(self):
        """Test parsing negative LSI value."""
        # Create payload with LSI = -0.30 (encoded as 256-30 = 226 with high bit set)
        payload = bytes([0] * 22 + [0x9E] + [0] * 18)  # 0x9E = 158, with bit 7 set
        # Actually: 0x9E = 158, bit 7 is set (158 & 0x80 = 0x80)
        # So LSI = (256 - 158) / -100 = 98 / -100 = -0.98

        packet = self.create_test_packet(payload)
        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is not None
        assert state.lsi < 0

    def test_parse_alarms(self):
        """Test parsing alarm bits."""
        # Payload with flow alarm (0x01) and pH tank empty (0x20)
        payload = bytes([0] * 32 + [0x21] + [0] * 8)

        packet = self.create_test_packet(payload)
        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is not None
        assert state.alarms.flow
        assert state.alarms.ph_tank_empty
        assert not state.alarms.orp_tank_empty
        assert state.alarms.any_active

    def test_parse_warnings(self):
        """Test parsing warning bits."""
        # Payload with pH lockout (0x01) and invalid setup (0x08)
        payload = bytes([0] * 33 + [0x09] + [0] * 7)

        packet = self.create_test_packet(payload)
        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is not None
        assert state.warnings.ph_lockout
        assert state.warnings.invalid_setup
        assert state.warnings.any_active

    def test_parse_dosing_status(self):
        """Test parsing dosing status."""
        # Byte 34: pH dosing (bits 4-5), ORP dosing (bits 6-7)
        # 0x51 = 0101 0001:
        #   - bits 0-3: doser types
        #   - bits 4-5: pH status = 01 (monitoring)
        #   - bits 6-7: ORP status = 01 (monitoring)
        payload = bytes([0] * 34 + [0x51] + [0] * 6)

        packet = self.create_test_packet(payload)
        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is not None
        assert state.ph.dosing_status == DosingStatus.MONITORING
        assert state.orp.dosing_status == DosingStatus.MONITORING

    def test_parse_invalid_checksum(self):
        """Test that invalid checksum returns None."""
        payload = bytes([0] * 41)
        packet = self.create_test_packet(payload)

        # Corrupt the checksum
        packet = packet[:-1] + bytes([0xFF])

        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is None

    def test_parse_wrong_action(self):
        """Test that wrong action returns None."""
        # Create packet with action 210 instead of 18
        header = bytes([165, 0, 16, 144, 210, 1])  # Action 210
        payload = bytes([0])
        data = header + payload
        checksum = sum(data)
        packet = PREAMBLE + data + bytes([(checksum >> 8), checksum & 0xFF])

        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is None

    def test_parse_short_payload(self):
        """Test that short payload returns None."""
        # Create packet with only 20 bytes of payload
        payload = bytes([0] * 20)
        packet = self.create_test_packet(payload)

        parser = StatusResponseParser()
        state = parser.parse(packet)

        assert state is None
