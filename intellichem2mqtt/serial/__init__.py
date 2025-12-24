"""Serial communication module for RS-485 IntelliChem protocol."""

from .connection import RS485Connection
from .buffer import PacketBuffer

__all__ = ["RS485Connection", "PacketBuffer"]
