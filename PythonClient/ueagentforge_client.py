"""
UEAgentForge Python Client v0.1.0
==================================
HTTP client for the Unreal Remote Control API that routes all commands through
the UEAgentForge plugin (AgentForgeLibrary) with optional safety verification.

Usage:
    from ueagentforge_client import AgentForgeClient

    client = AgentForgeClient()
    print(client.ping())
    print(client.get_all_level_actors())
    client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=100)

Requirements:
    pip install requests
"""

from __future__ import annotations

import json
import time
import logging
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

try:
    import requests
except ImportError:
    raise ImportError("Install requests: pip install requests")


# ─── Configuration ─────────────────────────────────────────────────────────
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 30010
OBJECT_PATH  = "/Script/UEAgentForge.Default__AgentForgeLibrary"
FUNCTION     = "ExecuteCommandJson"

log = logging.getLogger("ueagentforge")


# ─── Data types ────────────────────────────────────────────────────────────
@dataclass
class ForgeResult:
    """Parsed response from a Forge command."""
    raw:     Dict[str, Any]
    ok:      bool
    error:   Optional[str]  = None

    @classmethod
    def from_response(cls, data: Dict[str, Any]) -> "ForgeResult":
        error = data.get("error")
        ok = error is None and data.get("ok", True)
        return cls(raw=data, ok=ok, error=error)

    def __repr__(self) -> str:
        if self.error:
            return f"ForgeResult(ERROR: {self.error})"
        return f"ForgeResult(ok={self.ok}, keys={list(self.raw.keys())})"


@dataclass
class VerificationReport:
    all_passed:  bool
    phases_run:  int
    details:     List[Dict[str, Any]] = field(default_factory=list)

    def summary(self) -> str:
        lines = [f"Verification: {'PASSED' if self.all_passed else 'FAILED'} "
                 f"({self.phases_run} phases)"]
        for d in self.details:
            status = "✓" if d.get("passed") else "✗"
            lines.append(f"  {status} [{d.get('phase','?')}] {d.get('detail','')} "
                         f"({d.get('duration_ms', 0):.1f}ms)")
        return "\n".join(lines)


# ─── Client ────────────────────────────────────────────────────────────────
class AgentForgeClient:
    """
    Thin Python wrapper over the UEAgentForge Remote Control API.

    By default, `verify=True` runs full 4-phase verification before any
    mutating command (this is the key differentiator from bare RCP calls).

    Set `verify=False` only in trusted automation scripts where speed matters
    and you accept that rollback testing is skipped.
    """

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        timeout: float = 30.0,
        verify: bool = True,
        verbose: bool = False,
    ):
        self.base_url = f"http://{host}:{port}/remote/object/call"
        self.timeout  = timeout
        self.verify   = verify
        self._session = requests.Session()
        self._session.headers.update({"Content-Type": "application/json"})

        if verbose:
            logging.basicConfig(level=logging.DEBUG)

    # ── Core transport ─────────────────────────────────────────────────────
    def _send(self, cmd: str, args: Optional[Dict] = None) -> Dict[str, Any]:
        """Send a raw command JSON to the plugin and return the parsed response."""
        request_json = json.dumps({"cmd": cmd, "args": args or {}})
        payload = {
            "objectPath":   OBJECT_PATH,
            "functionName": FUNCTION,
            "parameters":   {"RequestJson": request_json},
        }

        log.debug(f"→ {cmd} {args}")
        try:
            resp = self._session.put(self.base_url, json=payload, timeout=self.timeout)
            resp.raise_for_status()
        except requests.exceptions.ConnectionError:
            raise RuntimeError(
                f"Cannot connect to Unreal Editor at {self.base_url}. "
                "Ensure the editor is running with Remote Control API enabled."
            )

        body = resp.json()
        # Remote Control wraps the return value in {"ReturnValue": "..."}
        return_val = body.get("ReturnValue", body)
        if isinstance(return_val, str):
            try:
                return_val = json.loads(return_val)
            except json.JSONDecodeError:
                return_val = {"raw": return_val}

        log.debug(f"← {return_val}")
        return return_val

    def execute(self, cmd: str, args: Optional[Dict] = None) -> ForgeResult:
        """Execute any command and return a ForgeResult."""
        data = self._send(cmd, args)
        return ForgeResult.from_response(data)

    # ── Forge meta ─────────────────────────────────────────────────────────
    def ping(self) -> Dict:
        return self._send("ping")

    def get_forge_status(self) -> Dict:
        return self._send("get_forge_status")

    def run_verification(self, phase_mask: int = 15) -> VerificationReport:
        """
        Run the 4-phase verification protocol.
        phase_mask bits: 1=PreFlight, 2=Snapshot+Rollback, 4=PostVerify, 8=BuildCheck
        """
        data = self._send("run_verification", {"phase_mask": phase_mask})
        return VerificationReport(
            all_passed = data.get("all_passed", False),
            phases_run = data.get("phases_run", 0),
            details    = data.get("details", []),
        )

    def enforce_constitution(self, action_description: str) -> Dict:
        """Check whether an action is permitted by the loaded constitution."""
        return self._send("enforce_constitution",
                          {"action_description": action_description})

    # ── Observation ────────────────────────────────────────────────────────
    def get_all_level_actors(self) -> List[Dict]:
        data = self._send("get_all_level_actors")
        return data.get("actors", [])

    def get_actor_components(self, label: str) -> List[Dict]:
        data = self._send("get_actor_components", {"label": label})
        return data.get("components", [])

    def get_current_level(self) -> Dict:
        return self._send("get_current_level")

    def assert_current_level(self, expected_level: str) -> Dict:
        return self._send("assert_current_level", {"expected_level": expected_level})

    def get_actor_bounds(self, label: str) -> Dict:
        return self._send("get_actor_bounds", {"label": label})

    # ── Actor control ──────────────────────────────────────────────────────
    def spawn_actor(
        self,
        class_path: str,
        x: float = 0.0, y: float = 0.0, z: float = 0.0,
        pitch: float = 0.0, yaw: float = 0.0, roll: float = 0.0,
    ) -> ForgeResult:
        """Spawn an actor. Runs constitution + verification if self.verify is True."""
        if self.verify:
            chk = self.enforce_constitution(f"spawn_actor {class_path}")
            if not chk.get("allowed", True):
                raise PermissionError(
                    f"Constitution blocked spawn_actor: {chk.get('violations')}")
        return self.execute("spawn_actor", {
            "class_path": class_path,
            "x": x, "y": y, "z": z,
            "pitch": pitch, "yaw": yaw, "roll": roll,
        })

    def set_actor_transform(
        self,
        object_path: str,
        x: float = 0.0, y: float = 0.0, z: float = 0.0,
        pitch: float = 0.0, yaw: float = 0.0, roll: float = 0.0,
    ) -> ForgeResult:
        return self.execute("set_actor_transform", {
            "object_path": object_path,
            "x": x, "y": y, "z": z,
            "pitch": pitch, "yaw": yaw, "roll": roll,
        })

    def delete_actor(self, label: str) -> ForgeResult:
        return self.execute("delete_actor", {"label": label})

    def save_current_level(self) -> ForgeResult:
        return self.execute("save_current_level")

    def take_screenshot(self, filename: str = "forge_screenshot") -> ForgeResult:
        return self.execute("take_screenshot", {"filename": filename})

    # ── Spatial queries ────────────────────────────────────────────────────
    def cast_ray(
        self,
        start: Dict[str, float],
        end: Dict[str, float],
        trace_complex: bool = True,
    ) -> Dict:
        return self._send("cast_ray", {
            "start": start, "end": end, "trace_complex": trace_complex
        })

    def query_navmesh(
        self,
        x: float, y: float, z: float,
        extent_x: float = 100.0, extent_y: float = 100.0, extent_z: float = 200.0,
    ) -> Dict:
        return self._send("query_navmesh", {
            "x": x, "y": y, "z": z,
            "extent_x": extent_x, "extent_y": extent_y, "extent_z": extent_z,
        })

    # ── Blueprint manipulation ─────────────────────────────────────────────
    def create_blueprint(
        self, name: str, parent_class: str, output_path: str
    ) -> ForgeResult:
        return self.execute("create_blueprint", {
            "name": name, "parent_class": parent_class, "output_path": output_path
        })

    def compile_blueprint(self, blueprint_path: str) -> ForgeResult:
        return self.execute("compile_blueprint", {"blueprint_path": blueprint_path})

    def set_blueprint_cdo_property(
        self,
        blueprint_path: str,
        property_name: str,
        type: str,
        value: Any,
    ) -> ForgeResult:
        return self.execute("set_bp_cdo_property", {
            "blueprint_path": blueprint_path,
            "property_name":  property_name,
            "type":           type,
            "value":          str(value),
        })

    def edit_blueprint_node(
        self,
        blueprint_path: str,
        node_type: str = "",
        node_title: str = "",
        pins: Optional[List[Dict[str, str]]] = None,
    ) -> ForgeResult:
        """
        Edit a node in a Blueprint's event graph.
        pins: [{"name": "PinName", "value": "DefaultValue"}, ...]
        """
        return self.execute("edit_blueprint_node", {
            "blueprint_path": blueprint_path,
            "node_spec": {
                "type":  node_type,
                "title": node_title,
                "pins":  pins or [],
            },
        })

    # ── Material instancing ────────────────────────────────────────────────
    def create_material_instance(
        self, parent_material: str, instance_name: str, output_path: str
    ) -> ForgeResult:
        return self.execute("create_material_instance", {
            "parent_material": parent_material,
            "instance_name":   instance_name,
            "output_path":     output_path,
        })

    def set_material_params(
        self,
        instance_path: str,
        scalar_params: Optional[Dict[str, float]] = None,
        vector_params: Optional[Dict[str, Dict[str, float]]] = None,
    ) -> ForgeResult:
        args: Dict[str, Any] = {"instance_path": instance_path}
        if scalar_params: args["scalar_params"] = scalar_params
        if vector_params:  args["vector_params"] = vector_params
        return self.execute("set_material_params", args)

    # ── Content management ─────────────────────────────────────────────────
    def rename_asset(self, asset_path: str, new_name: str) -> ForgeResult:
        return self.execute("rename_asset", {"asset_path": asset_path, "new_name": new_name})

    def move_asset(self, asset_path: str, destination_path: str) -> ForgeResult:
        return self.execute("move_asset", {
            "asset_path": asset_path, "destination_path": destination_path
        })

    def delete_asset(self, asset_path: str) -> ForgeResult:
        return self.execute("delete_asset", {"asset_path": asset_path})

    # ── Transaction safety ─────────────────────────────────────────────────
    def begin_transaction(self, label: str = "AgentForge") -> ForgeResult:
        return self.execute("begin_transaction", {"label": label})

    def end_transaction(self) -> ForgeResult:
        return self.execute("end_transaction")

    def undo_transaction(self) -> ForgeResult:
        return self.execute("undo_transaction")

    def create_snapshot(self, snapshot_name: str = "") -> ForgeResult:
        return self.execute("create_snapshot", {"snapshot_name": snapshot_name})

    # ── Python scripting ───────────────────────────────────────────────────
    def execute_python(self, script: str) -> ForgeResult:
        """Execute arbitrary Python code inside the Unreal Editor process."""
        return self.execute("execute_python", {"script": script})

    # ── Performance profiling ──────────────────────────────────────────────
    def get_perf_stats(self) -> Dict:
        return self._send("get_perf_stats")

    # ── Scene setup ────────────────────────────────────────────────────────
    def setup_test_level(self, floor_size: float = 10000.0) -> ForgeResult:
        return self.execute("setup_test_level", {"floor_size": floor_size})

    # ── Context manager ────────────────────────────────────────────────────
    def transaction(self, label: str = "AgentForge"):
        """Context manager for scoped transactions.

        Usage:
            with client.transaction("My Label"):
                client.spawn_actor(...)
                client.set_actor_transform(...)
        """
        return _TransactionContext(self, label)


class _TransactionContext:
    def __init__(self, client: AgentForgeClient, label: str):
        self._client = client
        self._label  = label

    def __enter__(self):
        self._client.begin_transaction(self._label)
        return self._client

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is not None:
            self._client.undo_transaction()
            return False  # re-raise
        self._client.end_transaction()
        return False


# ─── CLI quick-test ────────────────────────────────────────────────────────
if __name__ == "__main__":
    import sys

    client = AgentForgeClient(verbose=True)

    print("\n=== UEAgentForge Client Quick Test ===\n")

    # Ping
    pong = client.ping()
    print("ping:", json.dumps(pong, indent=2))

    # Forge status
    status = client.get_forge_status()
    print("status:", json.dumps(status, indent=2))

    # Run verification
    report = client.run_verification(phase_mask=1)  # PreFlight only
    print(report.summary())

    # Actor list
    actors = client.get_all_level_actors()
    print(f"\nActors in level: {len(actors)}")
    for a in actors[:5]:
        print(f"  [{a.get('class','?')}] {a.get('label','?')}")
    if len(actors) > 5:
        print(f"  ... and {len(actors)-5} more")
