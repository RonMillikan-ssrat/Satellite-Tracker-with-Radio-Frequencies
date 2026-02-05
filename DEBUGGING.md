# VSCode Debugging Guide for Satellite Tracker

## Prerequisites

1. **Install VSCode Extensions:**
   - C/C++ (by Microsoft)
   - CMake Tools (by Microsoft)
   - CMake (by twxs)

2. **Install Debug Tools:**
   ```bash
   sudo apt-get install gdb build-essential
   ```

## Quick Start Debugging

### Method 1: Using VSCode UI (Easiest)

1. **Open the project in VSCode:**
   ```bash
   cd satellite-tracker
   code .
   ```

2. **First-time setup:**
   - Press `Ctrl+Shift+P` and type "CMake: Configure"
   - Select your compiler (usually GCC)

3. **Set breakpoints:**
   - Click in the left margin (gutter) next to line numbers
   - Red dots will appear where breakpoints are set

4. **Start debugging:**
   - Press `F5` or click "Run and Debug" in the sidebar
   - Select "Debug SatelliteTracker"
   - The app will build and launch with the debugger attached

### Method 2: Using Tasks

1. **Configure and Build for Debug:**
   - Press `Ctrl+Shift+B`
   - Select "configure and build"

2. **Start Debugging:**
   - Press `F5`

## Setting Breakpoints

### Common Breakpoint Locations

**For Satellite Tracking Logic:**
```cpp
// In satellitetracker.cpp
void SatelliteTracker::updatePositions() {
    QDateTime now = QDateTime::currentDateTimeUtc();  // ← Set breakpoint here
    
    for (int i = 0; i < m_satellites.size(); ++i) {
        Satellite& sat = m_satellites[i];
        sat.position = SGP4Wrapper::calculatePosition(sat, now, m_observer);  // ← Or here
    }
}
```

**For TLE Parsing:**
```cpp
// In tleparser.cpp
Satellite TLEParser::parseSingleTLE(...) {
    Satellite sat;  // ← Set breakpoint here
    sat.name = name;
    sat.line1 = line1;
    sat.line2 = line2;
    return sat;
}
```

**For Network Requests:**
```cpp
// In satellitetracker.cpp
void SatelliteTracker::onTLEReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {  // ← Set breakpoint here
        QString tleData = QString::fromUtf8(reply->readAll());
    }
}
```

**For GUI Updates:**
```cpp
// In mainwindow.cpp
void MainWindow::updateSatelliteTable() {
    QList<Satellite> visible = m_tracker->getVisibleSatellites();  // ← Set breakpoint here
    
    m_satelliteTable->setRowCount(visible.size());
}
```

**For Rendering:**
```cpp
// In skymapwidget.cpp
void SkyMapWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);  // ← Set breakpoint here
    painter.setRenderHint(QPainter::Antialiasing);
}
```

## Debugging Commands (When Stopped at Breakpoint)

### VSCode Debug Panel

- **Continue** (`F5`): Resume execution
- **Step Over** (`F10`): Execute current line, skip function calls
- **Step Into** (`F11`): Enter function calls
- **Step Out** (`Shift+F11`): Exit current function
- **Restart** (`Ctrl+Shift+F5`): Restart debugging session
- **Stop** (`Shift+F5`): Stop debugging

### Inspect Variables

When stopped at a breakpoint:

1. **VARIABLES pane** (left sidebar):
   - See all local variables
   - Expand Qt objects (QString, QList, etc.)
   - Hover over variables in code to see values

2. **WATCH pane**:
   - Add expressions to monitor: `sat.position.elevation`
   - Right-click variable → "Add to Watch"

3. **Debug Console**:
   - Type GDB commands directly
   - Examples:
     ```
     p sat.name           # Print satellite name
     p m_satellites.size() # Print number of satellites
     p sat.position       # Print entire position structure
     ```

## Common Debugging Scenarios

### 1. Debug Satellite Position Calculation

```cpp
// Set breakpoint in sgp4wrapper.cpp
SatellitePosition SGP4Wrapper::calculatePosition(...) {
    SatellitePosition pos;  // ← Breakpoint here
    
    // Step through to see calculations
    double meanMotion = 14.0;  // Watch this variable
    // ...
    return pos;  // ← Breakpoint here to see final result
}
```

**What to watch:**
- `meanMotion` - Should be > 0
- `inclination` - Should be between 0-180°
- `pos.azimuth` - Should be 0-360°
- `pos.elevation` - Should be -90 to 90°

### 2. Debug TLE Parsing

```cpp
// Set conditional breakpoint
// Right-click breakpoint → Edit Breakpoint → Condition
// Example: sat.name.contains("ISS")

void SatelliteTracker::onTLEReplyFinished() {
    QString tleData = QString::fromUtf8(reply->readAll());
    m_satellites = TLEParser::parseTLEData(tleData);  // ← Breakpoint
    
    // Check m_satellites in VARIABLES pane
}
```

### 3. Debug Network Issues

```cpp
void SatelliteTracker::onTLEReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        // ← Breakpoint: Check if this branch is hit
    } else {
        // ← Breakpoint: Check error message
        QString error = reply->errorString();
    }
}
```

### 4. Debug Ground Track Calculation

```cpp
QList<QPointF> SatelliteTracker::calculateGroundTrack(...) {
    QList<QPointF> track;
    
    for (int i = 0; i < totalSteps; ++i) {  // ← Breakpoint in loop
        QDateTime futureTime = now.addSecs(i * stepSeconds);
        SatellitePosition futurePos = SGP4Wrapper::calculatePosition(...);
        
        if (futurePos.elevation > 0.0) {
            track.append(QPointF(futurePos.azimuth, futurePos.elevation));
        }
    }
    return track;  // ← Check track.size()
}
```

## Advanced: Conditional Breakpoints

Right-click on a breakpoint → "Edit Breakpoint"

**Example 1: Break only for specific satellite**
```cpp
sat.name.contains("ISS")
```

**Example 2: Break when elevation is high**
```cpp
sat.position.elevation > 45.0
```

**Example 3: Break on error conditions**
```cpp
reply->error() != QNetworkReply::NoError
```

## Memory Debugging with Valgrind

For memory leaks and access issues:

```bash
# Build with debug symbols
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Run with valgrind
valgrind --leak-check=full --track-origins=yes ./SatelliteTracker
```

## Qt-Specific Debugging Tips

### Pretty Printers for Qt Types

GDB should automatically load Qt pretty printers. If not:

```bash
# In VSCode Debug Console
source /usr/share/qt6/gdb/qtprinters.py
```

### Inspect Qt Objects

```
(gdb) p sat.name              # QString
(gdb) p m_satellites          # QList<Satellite>
(gdb) p sat.transponders[0]   # Transponder struct
```

## Performance Profiling

### Using gprof

```bash
# Add profiling flags to CMakeLists.txt
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -pg")

# Rebuild and run
./SatelliteTracker

# Generate profile
gprof ./SatelliteTracker gmon.out > analysis.txt
```

### Using perf

```bash
# Record
perf record -g ./SatelliteTracker

# Analyze
perf report
```

## Troubleshooting

### Breakpoints Not Hit

1. **Check build type:**
   ```bash
   cd build
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make clean && make
   ```

2. **Verify debug symbols:**
   ```bash
   file ./SatelliteTracker
   # Should say "not stripped"
   ```

3. **Check breakpoint location:**
   - Make sure it's on an executable line (not blank/comment)
   - Set breakpoint AFTER opening brace `{`

### Variables Show "<optimized out>"

This means optimization is enabled. Force debug mode:
```bash
cd build
rm -rf *
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Can't See Qt String Contents

Install Qt debug helpers:
```bash
sudo apt-get install qt6-tools-dev-tools
```

## Keyboard Shortcuts Summary

| Action | Shortcut |
|--------|----------|
| Start Debugging | `F5` |
| Toggle Breakpoint | `F9` |
| Step Over | `F10` |
| Step Into | `F11` |
| Step Out | `Shift+F11` |
| Continue | `F5` |
| Stop | `Shift+F5` |
| Build | `Ctrl+Shift+B` |

## Example Debugging Session

1. **Open project:** `code satellite-tracker`
2. **Set breakpoint:** Click left of line 70 in `satellitetracker.cpp` (in `updatePositions`)
3. **Press F5** to start debugging
4. **Wait** for app to launch and satellites to load
5. **Breakpoint hits** every 2 seconds (update timer)
6. **Inspect** `m_satellites` in VARIABLES pane
7. **Press F10** to step through position calculations
8. **Press F5** to continue to next update

Happy debugging! 🐛
