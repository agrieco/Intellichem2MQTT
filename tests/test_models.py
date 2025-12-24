"""Tests for Pydantic data models."""

import pytest
from intellichem2mqtt.models.intellichem import (
    ChemicalState,
    Alarms,
    Warnings,
    IntelliChemState,
    DosingStatus,
    WaterChemistry,
)


class TestChemicalState:
    """Tests for ChemicalState model."""

    def test_default_values(self):
        """Test default values are set correctly."""
        state = ChemicalState()

        assert state.level == 0.0
        assert state.setpoint == 0.0
        assert state.dose_time == 0
        assert state.dose_volume == 0
        assert state.tank_level == 0
        assert state.dosing_status == DosingStatus.MONITORING
        assert not state.is_dosing

    def test_tank_level_percent(self):
        """Test tank level percentage calculation."""
        state = ChemicalState(tank_level=6)
        assert state.tank_level_percent == 100.0

        state = ChemicalState(tank_level=3)
        assert state.tank_level_percent == 50.0

        state = ChemicalState(tank_level=0)
        assert state.tank_level_percent == 0.0

    def test_dosing_status_str(self):
        """Test dosing status string representation."""
        assert str(DosingStatus.DOSING) == "Dosing"
        assert str(DosingStatus.MONITORING) == "Monitoring"
        assert str(DosingStatus.MIXING) == "Mixing"


class TestAlarms:
    """Tests for Alarms model."""

    def test_any_active_false(self):
        """Test any_active when no alarms."""
        alarms = Alarms()
        assert not alarms.any_active

    def test_any_active_true(self):
        """Test any_active when alarm present."""
        alarms = Alarms(flow=True)
        assert alarms.any_active

        alarms = Alarms(probe_fault=True)
        assert alarms.any_active


class TestWarnings:
    """Tests for Warnings model."""

    def test_any_active_false(self):
        """Test any_active when no warnings."""
        warnings = Warnings()
        assert not warnings.any_active

    def test_any_active_with_water_chemistry(self):
        """Test any_active with water chemistry warning."""
        warnings = Warnings(water_chemistry=WaterChemistry.CORROSIVE)
        assert warnings.any_active

        warnings = Warnings(water_chemistry=WaterChemistry.OK)
        assert not warnings.any_active


class TestWaterChemistry:
    """Tests for WaterChemistry enum."""

    def test_string_representation(self):
        """Test string representation."""
        assert str(WaterChemistry.OK) == "Ok"
        assert str(WaterChemistry.CORROSIVE) == "Corrosive"
        assert str(WaterChemistry.SCALING) == "Scaling"


class TestIntelliChemState:
    """Tests for IntelliChemState model."""

    def test_default_values(self):
        """Test default values are set correctly."""
        state = IntelliChemState()

        assert state.address == 144
        assert isinstance(state.ph, ChemicalState)
        assert isinstance(state.orp, ChemicalState)
        assert state.lsi == 0.0
        assert state.firmware == ""

    def test_to_mqtt_dict(self):
        """Test MQTT dictionary conversion."""
        state = IntelliChemState(
            address=144,
            ph=ChemicalState(level=7.5, setpoint=7.6, tank_level=5),
            orp=ChemicalState(level=700, setpoint=700, tank_level=4),
            lsi=0.03,
            calcium_hardness=250,
            alkalinity=100,
            temperature=82,
            firmware="1.080",
        )

        mqtt_dict = state.to_mqtt_dict()

        assert mqtt_dict["address"] == 144
        assert mqtt_dict["ph"]["level"] == 7.5
        assert mqtt_dict["ph"]["setpoint"] == 7.6
        assert mqtt_dict["orp"]["level"] == 700
        assert mqtt_dict["lsi"] == 0.03
        assert mqtt_dict["temperature"] == 82
        assert mqtt_dict["firmware"] == "1.080"

    def test_to_mqtt_dict_alarms(self):
        """Test MQTT dictionary includes alarms."""
        state = IntelliChemState(
            alarms=Alarms(flow=True, probe_fault=True),
        )

        mqtt_dict = state.to_mqtt_dict()

        assert mqtt_dict["alarms"]["flow"] is True
        assert mqtt_dict["alarms"]["probe_fault"] is True
        assert mqtt_dict["alarms"]["any_active"] is True

    def test_address_validation(self):
        """Test address validation."""
        # Valid addresses
        IntelliChemState(address=144)
        IntelliChemState(address=158)

        # Invalid addresses should raise error
        with pytest.raises(ValueError):
            IntelliChemState(address=143)

        with pytest.raises(ValueError):
            IntelliChemState(address=159)
