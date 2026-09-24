# Satellite Tracker

<img width="958" height="738" alt="image" src="https://github.com/user-attachments/assets/e4f72f50-74a9-44a0-9e86-b0984b589cb8" />


A C++ GUI application for tracking satellites in real-time, showing which satellites are currently visible from your location.
Valid radio frequencies provided, per satellite, when available.

## Features

- **Real-time satellite tracking** - Updates every 2 seconds
- **Sky map visualization** - Polar projection showing satellites above the horizon
- **Satellite table** - Detailed information about visible satellites
- **Color-coded status** - Blue for approaching, red for departing
- **Auto-location** - Automatically detect your location via IP geolocation
- **Manual location** - Set custom observer coordinates
- **TLE data fetching** - Downloads latest satellite orbital data from CelesTrak

## Requirements

- Qt 6.x (Core, Widgets, Network)
- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)

## Building

### Linux/macOS

```bash
# Install Qt 6 (example for Ubuntu)
sudo apt-get install qt6-base-dev

# Build the project
mkdir build
cd build
cmake ..
make

# Run
./SatelliteTracker
```

### macOS (with Homebrew)

```bash
brew install qt@6
export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"

mkdir build
cd build
cmake ..
make

./SatelliteTracker
```

### Windows

Install Qt 6 from https://www.qt.io/download

```cmd
mkdir build
cd build
cmake -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2019_64 ..
cmake --build . --config Release

.\Release\SatelliteTracker.exe
```

## Usage

1. **Set Your Location**
   - Click "Auto-Locate" to use IP geolocation
   - Or manually enter latitude, longitude, and altitude

2. **Fetch Satellite Data**
   - Click "Fetch Satellite Data" to download TLE data
   - Default: Amateur radio satellites from CelesTrak

3. **View Satellites**
   - Sky map shows satellites above the horizon
   - Table lists visible satellites with details
   - Blue = approaching (Doppler shift negative)
   - Red = departing (Doppler shift positive)

## Technical Details

### Components

- **SatelliteTracker** - Core tracking logic, TLE fetching, position calculations
- **SkyMapWidget** - Polar sky map visualization
- **MainWindow** - GUI coordination
- **SGP4Wrapper** - SGP4/SDP4 orbital propagation (via libsgp4)
- **TLEParser** - Parse Two-Line Element sets

## Orbital Accuracy

Positions are computed with the full SGP4/SDP4 model using
[libsgp4 by dnwrnr](https://github.com/dnwrnr/sgp4) (Apache 2.0), vendored in
`external/sgp4` so no separate install is needed. Results agree with
[Skyfield](https://rhodesmill.org/skyfield/) to within ~0.01° in azimuth/elevation
and well under 1 km in range. Overall accuracy is limited by TLE age (typically
~1 km at epoch, growing a few km per day), so refresh TLE data regularly.

### Data Sources

- **TLE Data**: CelesTrak (https://celestrak.org)
- **Geolocation**: ipapi.co (free tier, no API key required)

## MCP / Local JSON API

While running, the app serves a read/write JSON API on `http://127.0.0.1:8765`
(localhost only; change the port with the `SAT_TRACKER_API_PORT` environment variable).

| Method | Path | Description |
|--------|------|-------------|
| GET  | `/health` | Status and satellite counts |
| GET  | `/observer` | Observer location |
| POST | `/observer` | Set location: `{"latitude": 39.7, "longitude": -104.9, "altitude": 1609}` |
| GET  | `/satellites` | All satellites (`?visible=true` to filter, `?groundTrack=true` to include tracks) |
| GET  | `/satellites/visible` | Satellites above the horizon |
| GET  | `/satellites/{id}` | One satellite by NORAD catalog number or exact name |
| POST | `/tle/refresh` | Re-download TLE data (optional `{"url": "..."}`) |

### MCP server

`mcp_server/satellite_tracker_mcp.py` wraps this API as MCP tools
(`get_status`, `get_observer_location`, `set_observer_location`,
`get_visible_satellites`, `list_all_satellites`, `get_satellite`, `refresh_tle_data`).
The Qt app must be running for the tools to return data.

```bash
pip install -r mcp_server/requirements.txt
```

Register it with an MCP client, e.g. Claude Code:

```bash
claude mcp add satellite-tracker -- python3 /path/to/mcp_server/satellite_tracker_mcp.py
```

or in a JSON MCP config:

```json
{
  "mcpServers": {
    "satellite-tracker": {
      "command": "python3",
      "args": ["/path/to/mcp_server/satellite_tracker_mcp.py"]
    }
  }
}
```

## Customization

### Change Satellite Categories

Edit the TLE URL in `satellitetracker.cpp`:

```cpp
// Amateur satellites (default)
fetchTLEData("https://celestrak.org/NORAD/elements/gp.php?GROUP=amateur&FORMAT=tle");

// ISS and crew vehicles
fetchTLEData("https://celestrak.org/NORAD/elements/gp.php?GROUP=stations&FORMAT=tle");

// Weather satellites
fetchTLEData("https://celestrak.org/NORAD/elements/gp.php?GROUP=weather&FORMAT=tle");
```

### Update Frequency

Change update interval in `mainwindow.cpp`:

```cpp
// Update every 2 seconds (2000 ms)
m_updateTimer->start(2000);
```

## Future Enhancements

- [x] Integrate full SGP4 library for accurate propagation
- [ ] Add satellite pass predictions
- [ ] Show satellite ground tracks on a map
- [ ] Add Doppler shift calculations for radio frequencies
- [ ] Save/load observer locations
- [ ] Export pass predictions to calendar
- [ ] Add 3D visualization option
- [ ] Support for multiple observer locations
- [ ] Satellite filtering by type/category

## License

This project is provided as-is for educational purposes.

## Contributing

Contributions welcome! Key areas:

1. **Pass Predictions** - Calculate future satellite passes
2. **Radio Features** - Doppler calculations, transponder support
3. **Visualization** - Ground track maps, 3D view

## Credits

- TLE data from [CelesTrak](https://celestrak.org)
- Geolocation via [ipapi.co](https://ipapi.co)
- Built with Qt 6

## Contact

For questions about amateur radio satellite tracking, check out:
- [AMSAT](https://www.amsat.org)
- [SatNOGS](https://satnogs.org)
