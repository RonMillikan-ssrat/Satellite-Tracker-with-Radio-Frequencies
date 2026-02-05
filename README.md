# Satellite Tracker

A C++ GUI application for tracking satellites in real-time, showing which satellites are currently visible from your location.

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
- **SGP4Wrapper** - Simplified orbital propagation (placeholder for full SGP4)
- **TLEParser** - Parse Two-Line Element sets

## Current Limitations

⚠️ **Important**: The current SGP4 implementation is SIMPLIFIED for demonstration. For production use, you should integrate a full SGP4 library:

- [sgp4 by dnwrnr](https://github.com/dnwrnr/sgp4) - C++ implementation
- [Vallado's SGP4](https://celestrak.org/software/vallado-sw.php) - Reference implementation

**Current Accuracy:**
- ✅ Satellites spread around the sky based on orbital elements
- ✅ Uses real TLE data (inclination, mean motion, RAAN)
- ✅ Accounts for Earth's rotation
- ⚠️ Simplified circular orbit (no eccentricity)
- ⚠️ No perturbations (atmospheric drag, solar pressure)
- ⚠️ Position errors can be 10-100+ km

For accurate satellite tracking (especially for antenna pointing or pass predictions), see `SGP4_INTEGRATION.md` for full implementation instructions.

### Data Sources

- **TLE Data**: CelesTrak (https://celestrak.org)
- **Geolocation**: ipapi.co (free tier, no API key required)

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

- [ ] Integrate full SGP4 library for accurate propagation
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

1. **SGP4 Integration** - Replace simplified propagator with full SGP4
2. **Pass Predictions** - Calculate future satellite passes
3. **Radio Features** - Doppler calculations, transponder support
4. **Visualization** - Ground track maps, 3D view

## Credits

- TLE data from [CelesTrak](https://celestrak.org)
- Geolocation via [ipapi.co](https://ipapi.co)
- Built with Qt 6

## Contact

For questions about amateur radio satellite tracking, check out:
- [AMSAT](https://www.amsat.org)
- [SatNOGS](https://satnogs.org)
