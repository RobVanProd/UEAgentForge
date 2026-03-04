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
from pathlib import Path
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

try:
    import requests
except ImportError:
    raise ImportError("Install requests: pip install requests")


# â”€â”€â”€ Configuration â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 30010
OBJECT_PATH  = "/Script/UEAgentForge.Default__AgentForgeLibrary"
FUNCTION     = "ExecuteCommandJson"

log = logging.getLogger("ueagentforge")


# â”€â”€â”€ Data types â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
            status = "âœ“" if d.get("passed") else "âœ—"
            lines.append(f"  {status} [{d.get('phase','?')}] {d.get('detail','')} "
                         f"({d.get('duration_ms', 0):.1f}ms)")
        return "\n".join(lines)


# â”€â”€â”€ Client â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
        max_retries: int = 3,
        retry_backoff_sec: float = 0.5,
        verbose: bool = False,
    ):
        self.base_url = f"http://{host}:{port}/remote/object/call"
        self.timeout  = timeout
        self.verify   = verify
        self.max_retries = max(1, int(max_retries))
        self.retry_backoff_sec = max(0.0, float(retry_backoff_sec))
        self._session = requests.Session()
        self._session.headers.update({"Content-Type": "application/json"})

        if verbose:
            logging.basicConfig(level=logging.DEBUG)

    # â”€â”€ Core transport â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def _send(self, cmd: str, args: Optional[Dict] = None) -> Dict[str, Any]:
        """Send a raw command JSON to the plugin and return the parsed response."""
        request_json = json.dumps({"cmd": cmd, "args": args or {}})
        payload = {
            "objectPath":   OBJECT_PATH,
            "functionName": FUNCTION,
            "parameters":   {"RequestJson": request_json},
        }

        log.debug(f"â†’ {cmd} {args}")
        last_exc: Optional[Exception] = None
        for attempt in range(1, self.max_retries + 1):
            try:
                resp = self._session.put(self.base_url, json=payload, timeout=self.timeout)
                resp.raise_for_status()
                break
            except (requests.exceptions.ConnectionError, requests.exceptions.Timeout) as exc:
                last_exc = exc
                if attempt >= self.max_retries:
                    raise RuntimeError(
                        f"Cannot reach Unreal Editor at {self.base_url} for cmd '{cmd}': {exc}"
                    )
                time.sleep(self.retry_backoff_sec * attempt)
            except requests.exceptions.HTTPError as exc:
                last_exc = exc
                if attempt >= self.max_retries:
                    raise RuntimeError(
                        f"HTTP error for cmd '{cmd}' after {self.max_retries} attempts: {exc}"
                    )
                time.sleep(self.retry_backoff_sec * attempt)
        else:
            raise RuntimeError(
                f"Cannot connect to Unreal Editor at {self.base_url} for cmd '{cmd}'. "
                f"Last error: {last_exc}"
            )

        body = resp.json()
        # Remote Control wraps the return value in {"ReturnValue": "..."}
        return_val = body.get("ReturnValue", body)
        if isinstance(return_val, str):
            try:
                return_val = json.loads(return_val)
            except json.JSONDecodeError:
                return_val = {"raw": return_val}

        log.debug(f"â† {return_val}")
        return return_val

    def execute(self, cmd: str, args: Optional[Dict] = None) -> ForgeResult:
        """Execute any command and return a ForgeResult."""
        data = self._send(cmd, args)
        return ForgeResult.from_response(data)

    def wait_for_editor(self, timeout_sec: float = 30.0, poll_sec: float = 1.0) -> bool:
        """Poll ping() until the editor responds or timeout is reached."""
        deadline = time.time() + max(0.1, timeout_sec)
        while time.time() < deadline:
            try:
                self.ping()
                return True
            except Exception:
                time.sleep(max(0.05, poll_sec))
        return False

    def wait_for_current_level(
        self,
        expected_package_path: str,
        timeout_sec: float = 30.0,
        poll_sec: float = 0.5,
    ) -> bool:
        """Poll get_current_level() until package_path matches expected value."""
        expected = (expected_package_path or "").strip()
        if not expected:
            return False
        deadline = time.time() + max(0.1, timeout_sec)
        while time.time() < deadline:
            try:
                current = self.get_current_level()
                if current.get("package_path") == expected:
                    return True
            except Exception:
                pass
            time.sleep(max(0.05, poll_sec))
        return False

    def execute_many(self, commands: List[Dict[str, Any]]) -> List[ForgeResult]:
        """Run a list of commands sequentially."""
        results: List[ForgeResult] = []
        for item in commands:
            cmd = str(item.get("cmd", "")).strip()
            if not cmd:
                results.append(ForgeResult(raw={"error": "Missing cmd"}, ok=False, error="Missing cmd"))
                continue
            results.append(self.execute(cmd, item.get("args", {}) or {}))
        return results

    # â”€â”€ Forge meta â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    # â”€â”€ Observation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def get_all_level_actors(self) -> List[Dict]:
        data = self._send("get_all_level_actors")
        return data.get("actors", [])

    def get_actor_components(self, label: str) -> List[Dict]:
        data = self._send("get_actor_components", {"label": label})
        return data.get("components", [])

    def get_current_level(self) -> Dict:
        return self._send("get_current_level")

    def get_world_context(
        self,
        max_actors: int = 120,
        max_relationships: int = 48,
        include_components: bool = False,
        include_screenshot: bool = True,
        screenshot_label: str = "world_context",
    ) -> Dict:
        return self._send("get_world_context", {
            "max_actors": int(max_actors),
            "max_relationships": int(max_relationships),
            "include_components": bool(include_components),
            "include_screenshot": bool(include_screenshot),
            "screenshot_label": screenshot_label,
        })

    def assert_current_level(self, expected_level: str) -> Dict:
        return self._send("assert_current_level", {"expected_level": expected_level})

    def get_actor_bounds(self, label: str) -> Dict:
        return self._send("get_actor_bounds", {"label": label})

    def analyze_level_composition(self) -> Dict:
        return self._send("analyze_level_composition")

    def get_actors_in_radius(self, x: float, y: float, z: float, radius: float = 1000.0) -> Dict:
        return self._send("get_actors_in_radius", {"x": x, "y": y, "z": z, "radius": radius})

    # Deterministic procedural operator stack
    def get_procedural_capabilities(self, include_repo_urls: bool = True) -> Dict:
        return self._send("get_procedural_capabilities", {"include_repo_urls": include_repo_urls})

    def get_operator_policy(self) -> Dict:
        return self._send("get_operator_policy")

    def set_operator_policy(
        self,
        operator_only: Optional[bool] = None,
        allow_atomic_placement: Optional[bool] = None,
        max_poi_per_call: Optional[int] = None,
        max_actor_delta_per_pipeline: Optional[int] = None,
        max_memory_used_mb: Optional[float] = None,
    ) -> Dict:
        args: Dict[str, Any] = {}
        if operator_only is not None:
            args["operator_only"] = operator_only
        if allow_atomic_placement is not None:
            args["allow_atomic_placement"] = allow_atomic_placement
        if max_poi_per_call is not None:
            args["max_poi_per_call"] = int(max_poi_per_call)
        if max_actor_delta_per_pipeline is not None:
            args["max_actor_delta_per_pipeline"] = int(max_actor_delta_per_pipeline)
        if max_memory_used_mb is not None:
            args["max_memory_used_mb"] = float(max_memory_used_mb)
        return self._send("set_operator_policy", args)

    def op_terrain_generate(
        self,
        seed: int = 48293,
        width: int = 257,
        height: int = 257,
        frequency: float = 0.01,
        amplitude: float = 1.0,
        ridge_strength: float = 0.35,
        erosion_iterations: int = 16,
        erosion_strength: float = 0.35,
        spawn_landscape: bool = False,
    ) -> Dict:
        return self._send("op_terrain_generate", {
            "seed": int(seed),
            "width": int(width),
            "height": int(height),
            "frequency": float(frequency),
            "amplitude": float(amplitude),
            "ridge_strength": float(ridge_strength),
            "erosion_iterations": int(erosion_iterations),
            "erosion_strength": float(erosion_strength),
            "spawn_landscape": bool(spawn_landscape),
        })

    def op_surface_scatter(
        self,
        target_label: str,
        parameters: Optional[Dict[str, Any]] = None,
        generate: bool = True,
        distribution_mode: Optional[str] = None,
        density: Optional[float] = None,
        cluster_radius: Optional[float] = None,
        min_spacing: Optional[float] = None,
        height_range: Optional[List[float]] = None,
        slope_range: Optional[List[float]] = None,
        distance_mask: Optional[Dict[str, Any]] = None,
        seed: Optional[int] = None,
        palette_id: Optional[str] = None,
    ) -> Dict:
        args: Dict[str, Any] = {
            "target_label": target_label,
            "parameters": parameters or {},
            "generate": generate,
        }
        if distribution_mode is not None:
            args["distribution_mode"] = distribution_mode
        if density is not None:
            args["density"] = float(density)
        if cluster_radius is not None:
            args["cluster_radius"] = float(cluster_radius)
        if min_spacing is not None:
            args["min_spacing"] = float(min_spacing)
        if height_range is not None:
            args["height_range"] = height_range
        if slope_range is not None:
            args["slope_range"] = slope_range
        if distance_mask is not None:
            args["distance_mask"] = distance_mask
        if seed is not None:
            args["seed"] = int(seed)
        if palette_id is not None:
            args["palette_id"] = palette_id
        return self._send("op_surface_scatter", args)

    def op_spline_scatter(
        self,
        spline_actor_label: str,
        control_points: Optional[List[Dict[str, float]]] = None,
        parameters: Optional[Dict[str, Any]] = None,
        closed_loop: bool = False,
        generate: bool = True,
        distribution_mode: Optional[str] = None,
        density: Optional[float] = None,
        cluster_radius: Optional[float] = None,
        min_spacing: Optional[float] = None,
        height_range: Optional[List[float]] = None,
        slope_range: Optional[List[float]] = None,
        distance_mask: Optional[Dict[str, Any]] = None,
        seed: Optional[int] = None,
        palette_id: Optional[str] = None,
    ) -> Dict:
        args: Dict[str, Any] = {
            "spline_actor_label": spline_actor_label,
            "parameters": parameters or {},
            "closed_loop": closed_loop,
            "generate": generate,
        }
        if control_points:
            args["control_points"] = control_points
        if distribution_mode is not None:
            args["distribution_mode"] = distribution_mode
        if density is not None:
            args["density"] = float(density)
        if cluster_radius is not None:
            args["cluster_radius"] = float(cluster_radius)
        if min_spacing is not None:
            args["min_spacing"] = float(min_spacing)
        if height_range is not None:
            args["height_range"] = height_range
        if slope_range is not None:
            args["slope_range"] = slope_range
        if distance_mask is not None:
            args["distance_mask"] = distance_mask
        if seed is not None:
            args["seed"] = int(seed)
        if palette_id is not None:
            args["palette_id"] = palette_id
        return self._send("op_spline_scatter", args)

    def op_road_layout(
        self,
        road_actor_label: Optional[str] = None,
        road_class_path: Optional[str] = None,
        centerline_points: Optional[List[Dict[str, float]]] = None,
        parameters: Optional[Dict[str, Any]] = None,
        generate: bool = True,
    ) -> Dict:
        args: Dict[str, Any] = {"generate": generate}
        if road_actor_label:
            args["road_actor_label"] = road_actor_label
        if road_class_path:
            args["road_class_path"] = road_class_path
        if centerline_points:
            args["centerline_points"] = centerline_points
        if parameters:
            args["parameters"] = parameters
        return self._send("op_road_layout", args)

    def op_biome_layers(
        self,
        target_label: str,
        layers: Optional[Dict[str, Any]] = None,
        parameters: Optional[Dict[str, Any]] = None,
        generate: bool = True,
        distribution_mode: Optional[str] = None,
        density: Optional[float] = None,
        cluster_radius: Optional[float] = None,
        min_spacing: Optional[float] = None,
        height_range: Optional[List[float]] = None,
        slope_range: Optional[List[float]] = None,
        distance_mask: Optional[Dict[str, Any]] = None,
        seed: Optional[int] = None,
        palette_id: Optional[str] = None,
    ) -> Dict:
        args: Dict[str, Any] = {"target_label": target_label, "generate": generate}
        if layers:
            args["layers"] = layers
        if parameters:
            args["parameters"] = parameters
        if distribution_mode is not None:
            args["distribution_mode"] = distribution_mode
        if density is not None:
            args["density"] = float(density)
        if cluster_radius is not None:
            args["cluster_radius"] = float(cluster_radius)
        if min_spacing is not None:
            args["min_spacing"] = float(min_spacing)
        if height_range is not None:
            args["height_range"] = height_range
        if slope_range is not None:
            args["slope_range"] = slope_range
        if distance_mask is not None:
            args["distance_mask"] = distance_mask
        if seed is not None:
            args["seed"] = int(seed)
        if palette_id is not None:
            args["palette_id"] = palette_id
        return self._send("op_biome_layers", args)

    def op_stamp_poi(
        self,
        poi_class_paths: List[str],
        anchors: List[Dict[str, float]],
        seed: int = 1337,
        max_count: Optional[int] = None,
        align_to_surface: bool = True,
        align_to_normal: bool = False,
        label_prefix: str = "POI",
    ) -> Dict:
        args: Dict[str, Any] = {
            "poi_class_paths": poi_class_paths,
            "anchors": anchors,
            "seed": int(seed),
            "align_to_surface": align_to_surface,
            "align_to_normal": align_to_normal,
            "label_prefix": label_prefix,
        }
        if max_count is not None:
            args["max_count"] = int(max_count)
        return self._send("op_stamp_poi", args)

    def run_operator_pipeline(
        self,
        terrain: Optional[Dict[str, Any]] = None,
        surface: Optional[Dict[str, Any]] = None,
        spline: Optional[Dict[str, Any]] = None,
        roads: Optional[Dict[str, Any]] = None,
        biomes: Optional[Dict[str, Any]] = None,
        poi: Optional[Dict[str, Any]] = None,
        seed: Optional[int] = None,
        palette_id: Optional[str] = None,
        distribution_mode: Optional[str] = None,
        density: Optional[float] = None,
        cluster_radius: Optional[float] = None,
        min_spacing: Optional[float] = None,
        height_range: Optional[List[float]] = None,
        slope_range: Optional[List[float]] = None,
        distance_mask: Optional[Dict[str, Any]] = None,
        stop_on_error: bool = True,
        max_actor_delta: Optional[int] = None,
        max_memory_used_mb: Optional[float] = None,
    ) -> Dict:
        args: Dict[str, Any] = {"stop_on_error": stop_on_error}
        if terrain is not None:
            args["terrain"] = terrain
        if surface is not None:
            args["surface"] = surface
        if spline is not None:
            args["spline"] = spline
        if roads is not None:
            args["roads"] = roads
        if biomes is not None:
            args["biomes"] = biomes
        if poi is not None:
            args["poi"] = poi
        if seed is not None:
            args["seed"] = int(seed)
        if palette_id is not None:
            args["palette_id"] = palette_id
        if distribution_mode is not None:
            args["distribution_mode"] = distribution_mode
        if density is not None:
            args["density"] = float(density)
        if cluster_radius is not None:
            args["cluster_radius"] = float(cluster_radius)
        if min_spacing is not None:
            args["min_spacing"] = float(min_spacing)
        if height_range is not None:
            args["height_range"] = height_range
        if slope_range is not None:
            args["slope_range"] = slope_range
        if distance_mask is not None:
            args["distance_mask"] = distance_mask
        if max_actor_delta is not None:
            args["max_actor_delta"] = int(max_actor_delta)
        if max_memory_used_mb is not None:
            args["max_memory_used_mb"] = float(max_memory_used_mb)
        return self._send("run_operator_pipeline", args)

    # â”€â”€ Actor control â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    def redraw_viewports(self) -> ForgeResult:
        """Force editor viewport redraw before screenshot capture."""
        return self.execute("redraw_viewports")

    def set_viewport_camera(
        self,
        x: float = 0.0, y: float = 0.0, z: float = 170.0,
        pitch: float = 0.0, yaw: float = 0.0, roll: float = 0.0,
    ) -> ForgeResult:
        """Move the first perspective viewport camera."""
        return self.execute("set_viewport_camera", {
            "x": x, "y": y, "z": z,
            "pitch": pitch, "yaw": yaw, "roll": roll,
        })

    def take_focused_screenshot(
        self,
        filename: str = "forge_screenshot",
        x: Optional[float] = None, y: Optional[float] = None, z: Optional[float] = None,
        pitch: Optional[float] = None, yaw: Optional[float] = None, roll: Optional[float] = None,
    ) -> ForgeResult:
        """Optionally frame camera, redraw, then queue screenshot."""
        if x is not None:
            self.set_viewport_camera(
                x=x, y=y or 0.0, z=z or 170.0,
                pitch=pitch or 0.0, yaw=yaw or 0.0, roll=roll or 0.0,
            )
        self.redraw_viewports()
        return self.execute("take_screenshot", {"filename": filename})

    # â”€â”€ Spatial queries â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    def spawn_actor_at_surface(
        self,
        class_path: str,
        label: str,
        origin: Dict[str, float],
        direction: Optional[Dict[str, float]] = None,
        max_distance: float = 5000.0,
        align_to_normal: bool = True,
    ) -> Dict:
        args: Dict[str, Any] = {
            "class_path": class_path,
            "label": label,
            "origin": origin,
            "max_distance": max_distance,
            "align_to_normal": align_to_normal,
        }
        if direction:
            args["direction"] = direction
        return self._send("spawn_actor_at_surface", args)

    # â”€â”€ Blueprint manipulation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def create_blueprint(
        self,
        name: str,
        parent_class: str,
        output_path: str,
        use_command_path: bool = False,
    ) -> ForgeResult:
        # Safe default path: use editor Python idempotent creation to avoid double-exec
        # rollback collisions in create_blueprint command transactions.
        if not use_command_path:
            asset_path = f"{output_path.rstrip('/')}/{name}"
            script = f"""
import unreal
asset_path = {json.dumps(asset_path)}
parent_path = {json.dumps(parent_class)}
parent_cls = unreal.load_class(None, parent_path)
if not parent_cls:
    raise RuntimeError("Parent class not found: " + parent_path)

bp = unreal.EditorAssetLibrary.load_asset(asset_path)
created = False
if not bp:
    if not hasattr(unreal, "BlueprintEditorLibrary") or not hasattr(unreal.BlueprintEditorLibrary, "create_blueprint_asset_with_parent"):
        raise RuntimeError("BlueprintEditorLibrary.create_blueprint_asset_with_parent not available")
    bp = unreal.BlueprintEditorLibrary.create_blueprint_asset_with_parent(asset_path, parent_cls)
    created = True
if not bp:
    raise RuntimeError("Failed to create blueprint: " + asset_path)

try:
    unreal.KismetEditorUtilities.compile_blueprint(bp)
except Exception:
    pass
unreal.EditorAssetLibrary.save_asset(asset_path)
"""
            run = self.execute_python_multiline(script)
            if not run.ok:
                return run
            return ForgeResult.from_response({
                "ok": True,
                "package": asset_path,
                "generated_class_path": f"{asset_path}.{name}_C",
            })

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

    def get_multi_view_capture(
        self,
        angle: str = "top",
        orbit_radius: float = 3000.0,
        center_x: Optional[float] = None,
        center_y: Optional[float] = None,
        center_z: Optional[float] = None,
    ) -> Dict:
        args: Dict[str, Any] = {"angle": angle, "orbit_radius": orbit_radius}
        if center_x is not None:
            args["center_x"] = center_x
        if center_y is not None:
            args["center_y"] = center_y
        if center_z is not None:
            args["center_z"] = center_z
        return self._send("get_multi_view_capture", args)

    def get_semantic_env_snapshot(self) -> Dict:
        return self._send("get_semantic_env_snapshot")

    def place_asset_thematically(
        self,
        class_path: str,
        count: int = 3,
        label_prefix: str = "Themed",
        reference_area: Optional[Dict[str, float]] = None,
        theme_rules: Optional[Dict[str, Any]] = None,
    ) -> Dict:
        args: Dict[str, Any] = {
            "class_path": class_path,
            "count": int(count),
            "label_prefix": label_prefix,
        }
        if reference_area:
            args["reference_area"] = reference_area
        if theme_rules:
            args["theme_rules"] = theme_rules
        return self._send("place_asset_thematically", args)

    def observe_analyze_plan_act(
        self,
        description: str,
        max_iterations: int = 1,
        score_target: float = 60.0,
    ) -> Dict:
        return self._send("observe_analyze_plan_act", {
            "description": description,
            "max_iterations": int(max_iterations),
            "score_target": float(score_target),
        })

    # â”€â”€ Material instancing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

    # â”€â”€ Content management â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def rename_asset(self, asset_path: str, new_name: str) -> ForgeResult:
        return self.execute("rename_asset", {"asset_path": asset_path, "new_name": new_name})

    def move_asset(self, asset_path: str, destination_path: str) -> ForgeResult:
        return self.execute("move_asset", {
            "asset_path": asset_path, "destination_path": destination_path
        })

    def delete_asset(self, asset_path: str) -> ForgeResult:
        return self.execute("delete_asset", {"asset_path": asset_path})

    # â”€â”€ Transaction safety â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def begin_transaction(self, label: str = "AgentForge") -> ForgeResult:
        return self.execute("begin_transaction", {"label": label})

    def end_transaction(self) -> ForgeResult:
        return self.execute("end_transaction")

    def undo_transaction(self) -> ForgeResult:
        return self.execute("undo_transaction")

    def create_snapshot(self, snapshot_name: str = "") -> ForgeResult:
        return self.execute("create_snapshot", {"snapshot_name": snapshot_name})

    # â”€â”€ Python scripting â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def execute_python(self, script: str) -> ForgeResult:
        """Execute arbitrary Python code inside the Unreal Editor process."""
        return self.execute("execute_python", {"script": script})

    def execute_python_multiline(self, script: str) -> ForgeResult:
        """Run multi-line Python in editors where ExecuteStatement is single-line only."""
        wrapped = f"exec({json.dumps(script)})"
        return self.execute_python(wrapped)

    def execute_python_file(self, script_path: str) -> ForgeResult:
        """Load a local .py file and execute it inside the Unreal Editor process."""
        path = Path(script_path)
        if not path.exists():
            return ForgeResult(raw={"error": f"Python file not found: {path}"}, ok=False, error="Python file not found")
        script = path.read_text(encoding="utf-8")
        return self.execute_python_multiline(script)

    # Convenience wrappers for common editor automation tasks.
    def load_level(self, level_path: str) -> ForgeResult:
        code = f"import unreal; unreal.EditorLevelLibrary.load_level({json.dumps(level_path)})"
        return self.execute_python(code)

    def new_level(self, level_path: str) -> ForgeResult:
        code = f"import unreal; unreal.EditorLevelLibrary.new_level({json.dumps(level_path)})"
        return self.execute_python(code)

    def duplicate_asset(self, source_asset_path: str, destination_asset_path: str) -> ForgeResult:
        code = (
            "import unreal; "
            f"unreal.EditorAssetLibrary.duplicate_asset({json.dumps(source_asset_path)}, "
            f"{json.dumps(destination_asset_path)})"
        )
        return self.execute_python(code)

    def save_all_dirty_levels(self) -> ForgeResult:
        code = "import unreal; unreal.EditorLevelLibrary.save_all_dirty_levels()"
        return self.execute_python(code)

    def spawn_static_mesh_actor(
        self,
        mesh_asset_path: str,
        label: str,
        x: float,
        y: float,
        z: float,
        pitch: float = 0.0,
        yaw: float = 0.0,
        roll: float = 0.0,
        sx: float = 1.0,
        sy: float = 1.0,
        sz: float = 1.0,
    ) -> ForgeResult:
        """Spawn a StaticMeshActor and assign a mesh in one call."""
        code = (
            "import unreal; "
            f"mesh=unreal.EditorAssetLibrary.load_asset({json.dumps(mesh_asset_path)}); "
            "assert mesh, 'Static mesh not found'; "
            f"a=unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector({x},{y},{z}), "
            f"unreal.Rotator({pitch},{yaw},{roll})); "
            f"a.set_actor_label({json.dumps(label)}); "
            f"a.set_actor_scale3d(unreal.Vector({sx},{sy},{sz})); "
            "smc=a.get_component_by_class(unreal.StaticMeshComponent); "
            "assert smc, 'StaticMeshComponent missing'; "
            "smc.set_static_mesh(mesh)"
        )
        return self.execute_python(code)

    def spawn_blueprint_actor(
        self,
        blueprint_asset_path: str,
        label: str,
        x: float,
        y: float,
        z: float,
        pitch: float = 0.0,
        yaw: float = 0.0,
        roll: float = 0.0,
    ) -> ForgeResult:
        """Spawn a Blueprint actor via execute_python.

        This path is useful when rollback-based spawn_actor validation fails on
        very large or heavily modified levels.
        """
        code = (
            "import unreal; "
            f"cls=unreal.EditorAssetLibrary.load_blueprint_class({json.dumps(blueprint_asset_path)}); "
            "assert cls, 'Blueprint class not found'; "
            f"a=unreal.EditorLevelLibrary.spawn_actor_from_class(cls, unreal.Vector({x},{y},{z}), "
            f"unreal.Rotator({pitch},{yaw},{roll})); "
            f"a.set_actor_label({json.dumps(label)})"
        )
        return self.execute_python(code)

    # â”€â”€ Performance profiling â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def get_perf_stats(self) -> Dict:
        return self._send("get_perf_stats")

    # â”€â”€ Scene setup â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    def setup_test_level(self, floor_size: float = 10000.0) -> ForgeResult:
        return self.execute("setup_test_level", {"floor_size": floor_size})

    # â”€â”€ Context manager â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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


# â”€â”€â”€ CLI quick-test â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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

