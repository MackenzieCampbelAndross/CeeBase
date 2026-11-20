"""
CeeBase Platform Row-Level Security (RLS) Engine
Enforces table-level and row-level access control policies at trusted backend boundaries.
Translates security context (auth.uid(), auth.role(), auth.email()) into enforceable row filters.
"""

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple


class RLSPolicy:
    def __init__(
        self,
        name: str,
        table: str,
        operation: str = "ALL",  # SELECT, INSERT, UPDATE, DELETE, ALL
        roles: Optional[List[str]] = None,  # ["anon", "authenticated"] or ["*"]
        using_expr: str = "true",
        check_expr: Optional[str] = None
    ):
        self.name = name
        self.table = table.lower()
        self.operation = operation.upper()
        self.roles = [r.lower() for r in (roles or ["*"])]
        self.using_expr = using_expr.strip()
        self.check_expr = (check_expr or using_expr).strip()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "table": self.table,
            "operation": self.operation,
            "roles": self.roles,
            "using_expr": self.using_expr,
            "check_expr": self.check_expr
        }

    @classmethod
    def from_dict(cls, d: Dict[str, Any]) -> "RLSPolicy":
        return cls(
            name=d["name"],
            table=d["table"],
            operation=d.get("operation", "ALL"),
            roles=d.get("roles", ["*"]),
            using_expr=d.get("using_expr", "true"),
            check_expr=d.get("check_expr")
        )


class RLSEngine:
    """Manages table RLS flags and evaluates policies against security contexts."""

    def __init__(self, state_dir: Path):
        self.state_dir = Path(state_dir)
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.policies_file = self.state_dir / "rls_policies.json"
        self.tables_rls_enabled: Dict[str, bool] = {}
        self.policies: Dict[str, List[RLSPolicy]] = {}
        self._load_state()

    def _load_state(self):
        if self.policies_file.exists():
            try:
                with open(self.policies_file, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    self.tables_rls_enabled = data.get("tables_enabled", {})
                    self.policies = {}
                    for tbl, pol_list in data.get("policies", {}).items():
                        self.policies[tbl] = [RLSPolicy.from_dict(p) for p in pol_list]
            except Exception:
                self.tables_rls_enabled = {}
                self.policies = {}

    def _save_state(self):
        payload = {
            "tables_enabled": self.tables_rls_enabled,
            "policies": {tbl: [p.to_dict() for p in pol_list] for tbl, pol_list in self.policies.items()}
        }
        with open(self.policies_file, "w", encoding="utf-8") as f:
            json.dump(payload, f, indent=2)

    def enable_rls(self, table: str):
        self.tables_rls_enabled[table.lower()] = True
        self._save_state()

    def disable_rls(self, table: str):
        self.tables_rls_enabled[table.lower()] = False
        self._save_state()

    def is_rls_enabled(self, table: str) -> bool:
        return self.tables_rls_enabled.get(table.lower(), False)

    def add_policy(self, policy: RLSPolicy):
        tbl = policy.table
        if tbl not in self.policies:
            self.policies[tbl] = []
        # Replace if name matches, else append
        self.policies[tbl] = [p for p in self.policies[tbl] if p.name != policy.name]
        self.policies[tbl].append(policy)
        self._save_state()

    def remove_policy(self, table: str, policy_name: str):
        tbl = table.lower()
        if tbl in self.policies:
            self.policies[tbl] = [p for p in self.policies[tbl] if p.name != policy_name]
            self._save_state()

    def get_policies(self, table: str) -> List[RLSPolicy]:
        return self.policies.get(table.lower(), [])

    def authorize_and_filter_query(
        self,
        table: str,
        operation: str,
        security_context: Dict[str, Any],
        incoming_where: Optional[str] = None
    ) -> Tuple[bool, Optional[str], Optional[RLSPolicy]]:
        """
        Authorize access and construct effective SQL WHERE clause.
        Returns: (is_allowed, effective_where_clause, matched_policy).
        """
        tbl = table.lower()
        op = operation.upper()
        role = security_context.get("role", "anon").lower()

        # service_role always bypasses RLS
        if role == "service_role":
            return True, incoming_where, None

        if not self.is_rls_enabled(tbl):
            # If RLS is not enabled on table, default allow
            return True, incoming_where, None

        # Find applicable policies
        tbl_policies = self.policies.get(tbl, [])
        applicable = []
        for p in tbl_policies:
            if (p.operation == "ALL" or p.operation == op) and ("*" in p.roles or role in p.roles):
                applicable.append(p)

        if not applicable:
            # RLS enabled and no matching policy -> access denied
            return False, None, None

        # Resolve policy expressions using security context
        policy_clauses = []
        for p in applicable:
            clause = self._resolve_expression(p.using_expr, security_context)
            if clause == "true":
                # At least one policy is unrestricted
                return True, incoming_where, p
            elif clause != "false":
                policy_clauses.append(clause)

        if not policy_clauses:
            return False, None, None

        # Combine policies with OR
        combined_policy = " OR ".join(f"({c})" for c in policy_clauses)
        if incoming_where and incoming_where.strip():
            effective_where = f"({incoming_where}) AND ({combined_policy})"
        else:
            effective_where = combined_policy

        return True, effective_where, applicable[0]

    def authorize_row_data(
        self,
        table: str,
        operation: str,
        security_context: Dict[str, Any],
        row_dict: Dict[str, Any]
    ) -> bool:
        """
        Check whether a specific row payload satisfies CHECK expressions for INSERT or UPDATE.
        """
        tbl = table.lower()
        role = security_context.get("role", "anon").lower()
        if role == "service_role":
            return True

        if not self.is_rls_enabled(tbl):
            return True

        tbl_policies = self.policies.get(tbl, [])
        applicable = [
            p for p in tbl_policies
            if (p.operation == "ALL" or p.operation == operation.upper()) and ("*" in p.roles or role in p.roles)
        ]
        if not applicable:
            return False

        for p in applicable:
            if self._eval_row_in_memory(p.check_expr, security_context, row_dict):
                return True
        return False

    def _resolve_expression(self, expr: str, ctx: Dict[str, Any]) -> str:
        """Replace auth functions with concrete SQL literals."""
        uid = ctx.get("id") or ctx.get("sub") or ""
        role = ctx.get("role", "anon")
        email = ctx.get("email") or ""

        res = expr
        # auth.uid() -> 'uuid'
        res = re.sub(r'auth\.uid\(\)', f"'{uid}'" if uid and uid != "anon" else "NULL", res, flags=re.IGNORECASE)
        # auth.role() -> 'role'
        res = re.sub(r'auth\.role\(\)', f"'{role}'", res, flags=re.IGNORECASE)
        # auth.email() -> 'email'
        res = re.sub(r'auth\.email\(\)', f"'{email}'", res, flags=re.IGNORECASE)
        return res

    def _eval_row_in_memory(self, expr: str, ctx: Dict[str, Any], row: Dict[str, Any]) -> bool:
        """Evaluate expression in memory for a single row dictionary."""
        resolved = self._resolve_expression(expr, ctx)
        if resolved.strip().lower() == "true":
            return True
        if resolved.strip().lower() == "false":
            return False

        # Simple condition: col = 'val' or 'val' = col
        match = re.match(r"^([\w\.]+)\s*(=|!=|>|<)\s*(.+)$", resolved.strip())
        if match:
            left, op, right = match.groups()
            left_val = row.get(left.strip(), left.strip().strip("'\""))
            right_val = row.get(right.strip(), right.strip().strip("'\""))

            # Type conversion
            if str(left_val).isdigit() and str(right_val).isdigit():
                left_val, right_val = int(left_val), int(right_val)

            if op == "=":
                return str(left_val) == str(right_val)
            elif op == "!=":
                return str(left_val) != str(right_val)
        return True
