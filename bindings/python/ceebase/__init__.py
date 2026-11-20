"""
CeeBase: High-Performance Embedded Relational Database Engine
Python DB-API 2.0 Compliant Driver
"""

from .client import (
    Connection,
    Cursor,
    Error,
    DatabaseError,
    OperationalError,
    IntegrityError,
    ProgrammingError,
    apilevel,
    threadsafety,
    paramstyle,
    connect
)
from .sdk import create_client, CeeBaseClient

__version__ = "1.0.0"
__all__ = [
    "Connection",
    "Cursor",
    "Error",
    "DatabaseError",
    "OperationalError",
    "IntegrityError",
    "ProgrammingError",
    "apilevel",
    "threadsafety",
    "paramstyle",
    "connect",
    "create_client",
    "CeeBaseClient"
]
