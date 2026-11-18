"""
CeeBase: Embedded Relational Database Python Client
"""

from .client import Connection, Cursor, Error, connect

__version__ = "1.0.0"
__all__ = ["Connection", "Cursor", "Error", "connect"]
