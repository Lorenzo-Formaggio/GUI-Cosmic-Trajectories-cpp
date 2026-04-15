# Cosmic Trajectory GUI

A tool for calculating cosmic trajectories during the QCD phase transition epoch of the early universe.

This application solves the evolution of chemical potentials ($\mu_B$, $\mu_Q$, $\mu_{\nu e}$, etc.) and particle densities as a function of Temperature ($T$), enforcing lepton asymmetry conservation, baryon number conservation, and charge neutrality under beta equilibrium.

## Features

- **Interactive GUI**: Real-time plotting and parameter control built with Qt6.
- **Dual Simulation Modes**:
  - **Single Run**: Detailed control over a single trajectory simulation.
  - **Compare Runs**: Run and overlay up to 5 independent trajectories simultaneously with distinct color-coding.
- **Physics Models (EoS)**:
  - Free Quark Gluon Plasma (QGP, 3-flavor or 4-flavor with Charm).
  - Lattice QCD-based EoS (3-flavor or 4-flavor with Charm).
  - External Tabulated EoS (Import CSV/TXT tables).
- **Visualization**:
  - High-quality, synchronized Log-Log charts.
  - Interactive legends with styled line indicators (dashed, dotted, solid).
  - Dark/Light mode theme support.
  - Dynamic Axis Toggling (Temperature on Vertical or Horizontal axis).
- **Exporting**:
  - **PDF Export**: Vector-scaled plots for publication.
  - **TXT Export**: Raw numerical data extracted into structured, parallel columns.

## Prerequisites & Installation

Follow the detailed instructions below to configure your environment for **macOS**, **Linux**, or **Windows**. 

The application requires:
1. A C++17 compatible compiler (`g++`, `clang++`, or `MSVC`)
2. CMake (version 3.16 or higher)
3. Qt 6 (Base and Charts modules)
4. GSL (GNU Scientific Library)

### 🍏 macOS

**1. Install Dependencies (via Homebrew)**
Open your terminal and run the following command to install CMake, Qt6, and GSL:
```bash
brew install cmake qt@6 gsl
```

**2. Configure Qt Path**
By default, Homebrew doesn't add Qt6 to your system path to avoid conflicts. You will typically need to run the compilation script, which handles this automatically. If you want to build manually, ensure CMake can find Qt6 by updating your path:
```bash
export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"
export LDFLAGS="-L/opt/homebrew/opt/qt@6/lib"
export CPPFLAGS="-I/opt/homebrew/opt/qt@6/include"
```

### 🐧 Linux (Ubuntu / Debian)

**1. Install Dependencies**
Open your terminal and use `apt` to install the required dev packages:
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev libqt6charts6-dev libgsl-dev
```

*(For Fedora/RHEL derivatives, use `sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qtcharts-devel gsl-devel`)*

### 🪟 Windows

**1. Install Dependencies (via MSYS2 / MinGW-w64)**
The easiest way to compile on Windows is using MSYS2.
1. Download and install [MSYS2](https://www.msys2.org/).
2. Open the **"MSYS2 MinGW x64"** terminal from your Start Menu.
3. Install the compilation toolchain, Qt6, and GSL by running:
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-charts mingw-w64-x86_64-gsl
```

## Compilation and Running

### Building the GUI

A convenient shell script is provided to automate the CMake configuration and compilation.

**On macOS and Linux** (or Windows MSYS2):
```bash
# Navigate to the project root directory
cd path/to/Gui-Cosmic-trajectories-cpp

# Execute the build script
bash gui/build_and_run.sh
```

*(If you get a permission denied error, run `chmod +x gui/build_and_run.sh` first).*

> [!TIP]
> This script automatically detects your QT path, builds the binary inside the `gui/build/` folder, and launches the application. It works whether run from the project root or from inside the `/gui` folder.

**Manual Build Process** (Fallback):
If you prefer not to use the script, you can build manually using standard CMake commands:
```bash
cd gui
mkdir build && cd build
cmake ..
cmake --build .
```
Then run the generated executable (e.g., `./CosmicTrajectoryGUI` on Mac/Linux or `CosmicTrajectoryGUI.exe` on Windows).

## Usage

### Single Run Tab
- Configure physics parameters (Baryon asymmetry $b$, Lepton asymmetries $l_e$, $l_\mu$, $l_\tau$).
- Select the **Equation of State (EoS)** and **Guess Method**.
- Click **"▶ Run Simulation"** to see the trajectory traced in real-time.
- Use the checkboxes below the charts to toggle specific variables (e.g., $|n_Q|$, $\mu_B$).

### Compare Runs Tab
- Features 5 **Collapsible Slots**.
- Each slot can be configured independently (e.g., testing different $dT$ or $b$ parameters).
- Click **"▶ Run Slot"** to add a trajectory to the comparison chart.
- Traces are color-coded to their respective slot borders.

### Exporting Results
Click the **"📤 Export Active Plot"** button in the bottom-right corner:
- **Export as PDF**: Saves a high-fidelity image of the current chart.
- **Export as TXT**: Saves the raw numerical data. In **Comparison mode**, data for all active slots is saved side-by-side in parallel columns for easy processing.

## Project Structure

-   [**gui/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/gui): Source code for the Qt6 interface and simulation workers.
-   [**src/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/src): Core physics library (Solver, EOS logic, Equation definitions).
-   [**include/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/include): Shared headers.
-   [**LatticeEoS/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/LatticeEoS): Mandatory thermodynamic data tables for the Lattice QCD model.
-   **EoS_Table.txt**: Input file used when selecting "Interpolated Table" EoS.

---
*Developed for research into the Early Universe QCD Phase Transition.*
