# Cosmic Trajectory GUI

A tool for calculating cosmic trajectories during the QCD phase transition epoch of the early universe.

This application solves the evolution of chemical potentials ($\mu_B$, $\mu_Q$, $\mu_{\nu e}$, etc.) and particle densities as a function of Temperature ($T$), enforcing lepton asymmetry conservation, baryon number conservation, and charge neutrality under beta equilibrium.

## Features

- **Interactive GUI**: Real-time plotting and parameter control built with Qt6.
- **Simulation Control**:
  - **Stop Button**: Interrupt long-running simulations safely at any time.
  - **Custom Solver Initialization**: Manually configure initial guesses for the chemical potential solver ($\mu_B, \mu_Q, \mu_\nu$) to improve convergence in challenging parameter spaces.
- **Dual Simulation Modes**:
  - **Single Run**: Detailed control over a single trajectory simulation.
  - **Compare Runs**: Run and overlay up to 5 independent trajectories simultaneously with distinct color-coding.
- **Physics Models (EoS)**:
  - Free Quark Gluon Plasma (QGP, 2-flavor, 3-flavor, or 4-flavor with Charm).
  - Lattice QCD-based EoS (3-flavor or 4-flavor with Charm).
  - Entropy Contour-based EoS (two variants; see *Entropy Contour EoS* below).
  - External Tabulated EoS (Import CSV/TXT tables). Features:
    - **Smart RAM Caching**: Heavy tables are loaded once and kept in memory for performance.
    - **Intelligent Temperature Clamping**: User-defined temperature ranges are respected if they fall within the table's bounds. If the user input exceeds the table range, the simulation automatically clamps to the available extremes and provides a clear warning in the console.
- **Visualization**:
  - **Critical Point Markers**: Specify and visualize the QCD Critical Point on all chemical potential plots with stable star markers.
  - High-quality, synchronized Log-Log charts.
  - Interactive legends with styled line indicators (dashed, dotted, solid).
  - Dark/Light mode theme support.
  - Dynamic Axis Toggling (Temperature on Vertical or Horizontal axis).
- **Exporting**:
  - **PDF Export**: Vector-scaled plots for publication.
  - **TXT Export**: Raw numerical data extracted into structured, parallel columns.

## Entropy Contour EoS

The Entropy Contour equation of state is the lattice-QCD-anchored constant
entropy density contour expansion of Shah et al.
[arXiv:2410.16206, arXiv:2601.08823], extended to the full three-dimensional
chemical potential space. Two variants ship here:

Both variants run the **same contour algebra** -- `src/ContourEoSCore.cpp`, a
port of `scontours::EquationOfState` from **s_contours_c-dev4-tristan** -- and
both anchor on the same QvdW-HRG seam. They differ in exactly one thing, the
mu = 0 lattice input:

| GUI selection | Source | mu = 0 lattice input |
| --- | --- | --- |
| **Entropy Contour** (`eos = 3`) | `src/EntrCont.cpp` | cubic splines through the tabulated Wuppertal-Budapest susceptibilities in `EntroContourEoS/chis/` and the entropy density in `entro_2013_hrg+extrap.spln` |
| **Entropy Contour Param** (`eos = 4`) | `src/EntrContParam.cpp` | the reference's closed-form Wuppertal-Budapest fits, with symbolically generated exact T-derivatives; reproduces s_contours_c-dev4-tristan exactly |

So comparing the two in the GUI isolates the effect of the lattice input
representation, which is the only reason to keep both.

**Which one to use.** The parametrizations behind `eos = 4` are fits to the
crossover region and are good **up to about 200 MeV**; the tabulated input of
`eos = 3` follows the lattice data to 800 MeV. Within the fits' range the two
agree to a few per cent, and past it they part company as expected -- the
`tanh` entropy fit saturates at s/T^3 = 12.09 while the data keeps rising
(14.7 at 300 MeV, 17.4 at 800 MeV, heading for Stefan-Boltzmann):

| T [MeV] | 90 | 150 | 200 | 300 | 600 |
| --- | --- | --- | --- | --- | --- |
| s/T^3 rel. difference | 3.5% | 0.2% | 0.4% | 17% | 28% |

So use `eos = 4` at and below ~200 MeV, where it reproduces
s_contours_c-dev4-tristan exactly, and `eos = 3` above it -- which is where
cosmic trajectories spend most of their range.

The shared contour algebra is:

    T_s(T0, mu) = T0 + mu^2/2 alpha2(T0),  alpha2 = -d(T0^2 X2)/dT0 / ds0/dT0
    P(T, mu)    = P_ref(mu) + [ p0(T0) + mu^2/2 (s0 alpha2 + T0^2 X2) ]
    s(T, mu)    = s0(T0)                       (exact along a constant-s contour)
    n_X(T, mu)  = n_X,ref + [ 1/2 T0^2 W_X ],  W_X = 2 sum_j mu_j chi_Xj(T0)

with s = s0(T0) exact along the contour, the pressure from the closed-form
antiderivative plus a Gauss-Legendre p0, and the anchor T0 resolved by a
safeguarded Newton solve rather than read off the grid. For `eos = 4` the
lattice input is `include/LatticeDerivatives.hpp`, the reference's symbolically
generated closed forms copied verbatim (no `chis/` files are read). Cross
susceptibilities default to the isospin-derived
scheme (chi11BS = 2 chi11QS - chi2S, chi11BQ = (chi2B + chi11BS)/2), matching
the paper's production setup; `EntropyContoursParam::setCrossMode` selects the
all-fitted scheme instead.

### QvdW-HRG low-temperature boundary

`src/HRG.cpp` is the seam model both variants anchor on at Tlow = 80 MeV. It is
the **quantum van der Waals HRG** (Vovchenko, Gorenstein, Stoecker,
PRL 118, 182301) evaluated through [Thermal-FIST](https://github.com/vlvovch/Thermal-FIST),
mirroring `scontours::QvdWHRG`: baryon-baryon and antibaryon-antibaryon van der
Waals interactions with `a = 329 MeV fm^3`, `b = 3.42 fm^3`, quantum statistics
and finite resonance widths on, and pions/kaons described by the ChPT-matched
effective mass model (which regularizes pion condensation at large |mu_Q|
instead of letting the ideal Bose integrals diverge). It replaces the previous
hand-rolled ideal / excluded-volume HRG.

One QvdW solve yields the pressure, all three charge densities and the entropy
density together, and results are memoized on the exact (T, mu) point -- the
solve dominates the cost of the EoS, so ask `evalContour` once per chemical
potential and reuse the returned `ContourValues`.

`EntroContourEoS/HRG/` holds the particle list and **`decays.dat`**. The decay
table is not optional: it sets the dynamical thresholds of the Breit-Wigner
width integration, and without it n_B at the seam is off by several percent.
The hypothetical sexaquark (pdg 9000001) is commented out of the list; it is not
part of the PDG2020 list the reference uses, and at mu_B ~ 600 MeV it shifts the
seam by a few percent in n_B and n_S.

### Cross-checking against the reference

`gui/eos_check.cpp` prints the same observables, in the same units and layout,
as the reference's `eos_line`, so the two can be diffed column by column:

```sh
# this repository
./build/eos_check 300 30 0 90 200 10
# s_contours_c-dev4-tristan  (mu = |(300,30,0)| = 301.4963 MeV,
#                             theta = acos(300/mu), phi = 0)
./build/eos_line 300 90 200 10 --dir 5.710593 0 --qvdw
```

## Prerequisites & Installation

Follow the detailed instructions below to configure your environment for **macOS**, **Linux**, or **Windows**. 

The application requires:
1. A C++17 compatible compiler (`g++`, `clang++`, or `MSVC`)
2. CMake (version 3.16 or higher)
3. Qt 6 (Base and Charts modules)
4. GSL (GNU Scientific Library)
5. Thermal-FIST — supplies the QvdW-HRG low-temperature boundary of the
   Entropy Contour EoS. **Nothing to install and no network needed**: its
   source tree is committed under `external/Thermal-FIST`, so a clone builds
   as-is. CMake resolves it in this order:

   1. `-DTHERMALFIST_ROOT=<path>` — an existing checkout you point at
   2. `external/Thermal-FIST` — the copy committed here (the normal case)
   3. downloaded at configure time (FetchContent) if neither is present

   Pass `-DCTG_FETCH_THERMALFIST=OFF` to forbid the download entirely. Only
   Thermal-FIST's library target is built; its own GUI and command-line tools
   are excluded. Without Thermal-FIST the Entropy Contour equations of state
   (`eos = 3` and `eos = 4`) report the missing dependency at start-up; the
   other models are unaffected.

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

## Usagecd

### Single Run Tab
- Configure physics parameters (Baryon asymmetry $b$, Lepton asymmetries $l_e$, $l_\mu$, $l_\tau$).
- **Advanced Configuration**:
  - **📍 Configure Critical Point**: Set $(T, \mu_B, \mu_Q)$ coordinates for the QCD Critical Point to mark it on your plots (located below the charts).
  - **⚙ Solver Settings**: Access a unified dialog to configure convergence tolerance, maximum iterations, and initial guess strategies (Standard vs Custom).
- Select the **Equation of State (EoS)** and **Scan Direction**.
- Click **"▶ Run Simulation"** to start, or **"⏹ Stop"** to abort a running process.
- Use the checkboxes below the charts to toggle specific variables (e.g., $|n_Q|$, $\mu_B$).

### Compare Runs Tab
- Features 5 **Collapsible Slots**.
- Each slot can be configured independently (e.g., testing different $dT$ or $b$ parameters).
- **Slot Identification**: Traces are color-coded to their respective slot borders.
- **Comparison Console**: A real-time console below the slots tracks logs and warnings for all active simulations, with each entry prefixed by its slot number and color.
- **Global Tools**: Use the buttons at the top of the left panel to configure the **Solver Settings** (Tolerance, Max Iterations, Initial Guess vectors) globally for all comparison slots.
- **Per-Slot Guess Method**: Select the propagation strategy (Simple vs Linear Extrap) individually for each slot within its collapsible box.
- Click **"▶ Run Slot"** to add a trajectory to the comparison chart, or the **"Stop"** button in the slot to abort it.
  
### EoS Explorer Tab
- Scan and visualize thermodynamic quantities ($n_B$, $n_Q$, $s$) for a given EoS across a temperature range.
- Supports **Normalization by $T^3$** to visualize scaled densities.
- Features the same **Intelligent Temperature Clamping** as the trajectory simulation when using external tables.

### Exporting Results
Click the **"📤 Export Active Plot"** button in the bottom-right corner:
- **Export as PDF**: Saves a high-fidelity image of the current chart.
- **Export as TXT**: Saves the raw numerical data. In **Comparison mode**, data for all active slots is saved side-by-side in parallel columns for easy processing.

## Project Structure

-   [**gui/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/gui): Source code for the Qt6 interface and simulation workers.
-   [**src/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/src): Core physics library (Solver, EOS logic, Equation definitions).
-   [**include/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/include): Shared headers.
-   [**LatticeEoS/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/LatticeEoS): Mandatory thermodynamic data tables for the Lattice QCD model.
-   [**EntroContourEoS/**](file:///Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp/EntroContourEoS): Data files for the Entropy Contour EoS model.
-   **EoS_Table.txt**: Input file used when selecting "Interpolated Table" EoS.

---
*Developed for research into the Early Universe QCD Phase Transition.*
