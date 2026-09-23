"""MCP server exposing the Satellite Tracker's local JSON API as tools.

The Qt app must be running; it serves the API on 127.0.0.1:8765 by default
(override with SAT_TRACKER_API_PORT in both the app and this server's env).
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request

try:  # mcp >= 2.0
    from mcp.server.mcpserver import MCPServer
except ImportError:  # mcp 1.x
    from mcp.server.fastmcp import FastMCP as MCPServer

API_BASE = f"http://127.0.0.1:{os.environ.get('SAT_TRACKER_API_PORT', '8765')}"

mcp = MCPServer("satellite-tracker")


def _request(method: str, path: str, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(
        API_BASE + path,
        data=data,
        method=method,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return json.loads(e.read() or b'{"error": "HTTP error"}')
    except urllib.error.URLError as e:
        return {
            "error": f"Satellite Tracker app is not reachable at {API_BASE} ({e.reason}). "
            "Start the SatelliteTracker app first."
        }


@mcp.tool()
def get_status() -> dict:
    """Check that the tracker is running and how many satellites are loaded/visible."""
    return _request("GET", "/health")


@mcp.tool()
def get_observer_location() -> dict:
    """Get the observer location (latitude/longitude in degrees, altitude in meters)."""
    return _request("GET", "/observer")


@mcp.tool()
def set_observer_location(latitude: float, longitude: float, altitude: float = 0.0) -> dict:
    """Set the observer location. Latitude/longitude in degrees, altitude in meters."""
    return _request(
        "POST",
        "/observer",
        {"latitude": latitude, "longitude": longitude, "altitude": altitude},
    )


@mcp.tool()
def get_visible_satellites(with_frequencies_only: bool = False) -> dict:
    """List satellites currently above the observer's horizon.

    Each entry has azimuth/elevation (degrees), range (km), range rate
    (km/s, negative = approaching) and any known radio transponders (MHz).
    Set with_frequencies_only to return only satellites with known transponders.
    """
    result = _request("GET", "/satellites/visible")
    if with_frequencies_only and "satellites" in result:
        result["satellites"] = [s for s in result["satellites"] if s["transponders"]]
        result["count"] = len(result["satellites"])
    return result


@mcp.tool()
def list_all_satellites() -> dict:
    """List every loaded satellite (name, catalog number, visibility, elevation)."""
    result = _request("GET", "/satellites")
    if "satellites" in result:
        result["satellites"] = [
            {
                "name": s["name"],
                "catalogNumber": s["catalogNumber"],
                "isVisible": s["isVisible"],
                "elevation": round(s["position"]["elevation"], 1),
            }
            for s in result["satellites"]
        ]
    return result


@mcp.tool()
def get_satellite(name_or_catalog_number: str) -> dict:
    """Get full details for one satellite by exact name (e.g. "ISS (ZARYA)") or
    NORAD catalog number (e.g. "25544"), including position, transponder
    frequencies, TLE lines and the predicted sky track for the next 20 minutes."""
    return _request("GET", "/satellites/" + urllib.parse.quote(name_or_catalog_number))


@mcp.tool()
def refresh_tle_data(url: str | None = None) -> dict:
    """Re-download orbital (TLE) data. Defaults to CelesTrak amateur satellites;
    pass a CelesTrak URL such as
    https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle
    to track a different group."""
    return _request("POST", "/tle/refresh", {"url": url} if url else {})


if __name__ == "__main__":
    mcp.run()
