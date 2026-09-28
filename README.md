# Satellite Tracker

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
- **SDR radio control** - Tunes Gqrx / SDR++ / rigctld to a satellite downlink with live Doppler correction

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
- **RadioController** - Doppler-corrected tuning over the rigctl protocol

## SDR Radio Control

The tracker can keep a software defined radio tuned to a satellite's downlink,
correcting for Doppler shift once per second (up to about ±3.5 kHz at 145 MHz
and ±10 kHz at 435 MHz for a LEO pass). It does not talk to the SDR dongle
directly: an SDR application does the demodulation and audio, and the tracker
steers it over the Hamlib **rigctl** TCP protocol, the same way Gpredict does.

The receiver range defaults to the NooElec NESDR Smart v5 (RTL-SDR, 100 kHz–1.75 GHz).
That covers the 2 m / 70 cm amateur satellites, the ISS and 137 MHz weather
satellites; downlinks outside it (e.g. 2.4 GHz, 10 GHz) are shown greyed out.

### Setup (Linux, RTL-SDR)

```bash
sudo apt install rtl-sdr gqrx-sdr
# Stop the DVB-TV kernel driver from claiming the dongle, then replug it
echo 'blacklist dvb_usb_rtl28xxu' | sudo tee /etc/modprobe.d/blacklist-rtlsdr.conf
rtl_test   # should find the "Generic RTL2832U" device
```

Then start the SDR application and enable its rigctl server:

| Application | How to enable | Port |
|-------------|---------------|------|
| Gqrx | Tools → Remote control | 7356 |
| SDR++ | Module Manager → add *Rigctl Server*, then Start | 4532 |
| Hamlib `rigctld` (hardware radio) | `rigctld -m <model> -r <device>` | 4532 |

### Use

1. In the **Radio (rigctl)** panel set host/port and click **Connect**.
2. Select a satellite in the table or sky map; its downlinks appear in the
   transponder list (the first tunable one is preselected).
3. Click **Tune** (or double-click the table row). The radio's mode and filter
   are set once (FM, USB, CW, …) and the frequency is updated every second.
   The tracked satellite is shown in bold.
4. Tuning also works before AOS: the radio is pre-tuned so you hear the
   satellite as soon as it clears the horizon.

To work a station inside a linear (SSB) transponder, just tune the SDR
application by hand: the tracker reads the frequency back each second and keeps
your change as a manual offset on top of the Doppler correction.

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
| GET  | `/radio` | Radio link status, tuned frequency, Doppler and manual offset (Hz) |
| POST | `/radio/connect` | Connect to rigctl (optional `{"host": "127.0.0.1", "port": 7356}`) |
| POST | `/radio/tune` | Doppler-track a downlink: `{"satellite": "ISS (ZARYA)", "transponder": 0}` (`transponder` is optional) |
| POST | `/radio/stop` | Stop tracking (radio stays on its last frequency) |

### MCP server

`mcp_server/satellite_tracker_mcp.py` wraps this API as MCP tools
(`get_status`, `get_observer_location`, `set_observer_location`,
`get_visible_satellites`, `list_all_satellites`, `get_satellite`, `refresh_tle_data`,
`get_radio_status`, `connect_radio`, `tune_radio`, `stop_radio`).
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
- [x] Add Doppler shift calculations for radio frequencies
- [x] Control an SDR via rigctl (Gqrx, SDR++, rigctld)
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
