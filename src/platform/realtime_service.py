"""
CeeBase Platform Realtime Event Service
Provides WebSocket & Server-Sent Events (SSE) changefeed subscriptions for
committed database operations (INSERT, UPDATE, DELETE).
"""

import asyncio
import json
import queue
import threading
import time
import uuid
from typing import Any, Callable, Dict, List, Optional, Set


class RealtimeSubscription:
    def __init__(
        self,
        sub_id: str,
        project_id: str,
        channel: str,
        table: str,
        event_filter: str = "*",  # INSERT, UPDATE, DELETE, *
        user_context: Optional[Dict[str, Any]] = None
    ):
        self.id = sub_id
        self.project_id = project_id
        self.channel = channel
        self.table = table.lower()
        self.event_filter = event_filter.upper()
        self.user_context = user_context or {"role": "anon"}
        self.queue: queue.Queue = queue.Queue(maxsize=1000)
        self.created_at = time.time()
        self.active = True


class RealtimeService:
    """Manages active realtime subscriptions and dispatches committed database mutations."""

    def __init__(self):
        self.lock = threading.Lock()
        self.subscriptions: Dict[str, RealtimeSubscription] = {}
        self.channel_map: Dict[str, Set[str]] = {}  # channel -> set of sub_ids
        self.table_map: Dict[str, Set[str]] = {}    # table -> set of sub_ids
        self.total_events_published = 0
        self.total_events_delivered = 0

    def subscribe(
        self,
        project_id: str,
        channel: str,
        table: str,
        event_filter: str = "*",
        user_context: Optional[Dict[str, Any]] = None
    ) -> RealtimeSubscription:
        sub_id = str(uuid.uuid4())
        sub = RealtimeSubscription(sub_id, project_id, channel, table, event_filter, user_context)

        with self.lock:
            self.subscriptions[sub_id] = sub
            if channel not in self.channel_map:
                self.channel_map[channel] = set()
            self.channel_map[channel].add(sub_id)

            tbl = table.lower()
            if tbl not in self.table_map:
                self.table_map[tbl] = set()
            self.table_map[tbl].add(sub_id)

        return sub

    def unsubscribe(self, sub_id: str):
        with self.lock:
            sub = self.subscriptions.pop(sub_id, None)
            if sub:
                sub.active = False
                if sub.channel in self.channel_map:
                    self.channel_map[sub.channel].discard(sub_id)
                if sub.table in self.table_map:
                    self.table_map[sub.table].discard(sub_id)

    def publish_event(
        self,
        project_id: str,
        table: str,
        event_type: str,  # INSERT, UPDATE, DELETE
        new_record: Optional[Dict[str, Any]] = None,
        old_record: Optional[Dict[str, Any]] = None
    ) -> int:
        """
        Dispatches an event to all matching active subscribers.
        Returns the number of subscribers the event was delivered to.
        """
        tbl = table.lower()
        ev_type = event_type.upper()
        now = time.time()

        payload = {
            "id": str(uuid.uuid4()),
            "project_id": project_id,
            "table": tbl,
            "type": ev_type,
            "commit_timestamp": now,
            "record": new_record or {},
            "old_record": old_record or {}
        }

        with self.lock:
            self.total_events_published += 1
            # Candidates are subscribers to this specific table or wildcard '*'
            candidates: Set[str] = set()
            if tbl in self.table_map:
                candidates.update(self.table_map[tbl])
            if "*" in self.table_map:
                candidates.update(self.table_map["*"])

            delivered = 0
            for sub_id in candidates:
                sub = self.subscriptions.get(sub_id)
                if not sub or not sub.active:
                    continue
                if sub.project_id != project_id and sub.project_id != "*":
                    continue
                if sub.event_filter != "*" and sub.event_filter != ev_type:
                    continue

                try:
                    sub.queue.put_nowait(payload)
                    delivered += 1
                    self.total_events_delivered += 1
                except queue.Full:
                    # Subscriber fell behind, drop event to prevent memory leak
                    pass

        return delivered

    def get_metrics(self) -> Dict[str, Any]:
        with self.lock:
            return {
                "active_connections": len(self.subscriptions),
                "active_channels": len(self.channel_map),
                "total_events_published": self.total_events_published,
                "total_events_delivered": self.total_events_delivered
            }
