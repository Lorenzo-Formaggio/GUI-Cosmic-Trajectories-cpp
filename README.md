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
  - Entropy Contour-based EoS (five variants: two lattice inputs, each with or without the Gibbs mixed phase in the first-order region, plus the parametrized input with all six susceptibility fits; see *Entropy Contour EoS* below).
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
chemical potential space. Five variants ship here:

All variants run the **same contour algebra** -- `src/ContourEoSCore.cpp`, a
port of `scontours::EquationOfState` from **s_contours_c-dev4-tristan** -- and
all anchor on the same QvdW-HRG seam at Tlow = 80 MeV. They differ in the
mu = 0 lattice input, in how the cross susceptibilities are obtained, and in
how the first-order region beyond the critical point is treated:

| GUI selection | Source | mu = 0 lattice input | first-order region |
| --- | --- | --- | --- |
| **Entropy Contour** (`eos = 3`) | `src/EntrCont.cpp` | cubic splines through the tabulated Wuppertal-Budapest susceptibilities in `EntroContourEoS/chis/` and the entropy density in `entro_2013_hrg+extrap.spln` | one phase at a time (Maxwell) |
| **Entropy Contour Param** (`eos = 4`) | `src/EntrContParam.cpp` | the reference's closed-form Wuppertal-Budapest fits, with symbolically generated exact T-derivatives; chi11BS and chi11BQ derived from the isospin relations; reproduces s_contours_c-dev4-tristan exactly | one phase at a time (Maxwell) |
| **Entropy Contour Gibbs** (`eos = 5`) | `src/EntrCont.cpp` + `src/GibbsMixedPhase.cpp` | as `eos = 3` | Gibbs mixed phase |
| **Entropy Contour Param Gibbs** (`eos = 6`) | `src/EntrContParam.cpp` + `src/GibbsMixedPhase.cpp` | as `eos = 4` | Gibbs mixed phase |
| **Entropy Contour Param-Legacy Gibbs** (`eos = 7`) | `src/EntrContParam.cpp` + `src/GibbsMixedPhase.cpp` | the same closed-form fits, but all six cross-susceptibility fits used as they are (no isospin relations), as in the pre-August code | Gibbs mixed phase |

So comparing 3 with 4 (or 5 with 6) in the GUI isolates the effect of the
lattice input representation, comparing 3 with 5 (or 4 with 6) isolates the
treatment of the first-order region, and comparing 6 with 7 isolates the
cross-susceptibility scheme. Outside the first-order region 5 and 6 are
identical to 3 and 4.

**Why `eos = 7` exists.** In 4 and 6, chi11BS = 2 chi11QS - chi2S and
chi11BQ = (chi2B + chi11BS)/2 are derived from four fits, which is the paper's
production setup. The relations are exact for isospin-symmetric quark
susceptibilities, but chi2B was fitted to a different dataset from the other
five, so the cancellation does not close: the derived chi11BQ keeps a residual
of ~0.016 above 250 MeV where the tables (from which the other fits were made)
give ~0.005, and it turns negative below ~95 MeV. Along a cosmic trajectory
this shows up as "waves" in mu_Q(T) that the tabulated input does not have.
`eos = 7` takes all six fits as they are instead (`CrossMode::Fitted`); its
chi11BQ follows the tables to <1% up to 200 MeV but decays too fast above. Its
first-order surface differs off the mu_B axis, so it has its own overlay file,
`assets/first_order_surface_param_fitted.dat` (`first_order_surface --fitted`).

**Domain.** All four are defined for T >= Tlow = 80 MeV only: the contour is
anchored on the seam there and every query below it returns NaN (the solver
then reports a NaN Jacobian at each step). A run or an EoS-explorer scan that
asks for a lower temperature is clamped to 80 MeV with a warning in the log.
(This holds for all of `eos = 3` to `eos = 7`.)

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

### Gibbs mixed phase (`eos = 5`, `eos = 6`, `eos = 7`)

Past the critical point the contour EoS has two mechanically stable phases at
the same (T, mu_B, mu_Q), a dilute and a dense one. The homogeneous models
(`eos = 3, 4`) always return the phase of highest pressure, so along a
trajectory the densities jump when mu_B crosses the coexistence value
mu_B*(T, mu_Q): a Maxwell construction, the system being entirely in one phase
or entirely in the other.

With conserved charges (n_B/s = b, electric neutrality with the leptons) the
system can instead sit on the coexistence surface as a **mixture**: a volume
fraction lambda in the dilute phase and 1 - lambda in the dense one, both at
the same T, mu_B*, mu_Q and pressure (the Gibbs conditions), the conserved
densities being the lever-rule averages n_X = lambda n_X^dil +
(1 - lambda) n_X^den, s likewise, P = P*. Going through the transition the
chemical potentials then move continuously along the coexistence surface while
lambda goes from 1 to 0, instead of jumping. `src/GibbsMixedPhase.cpp`
implements this on top of either lattice model (`GibbsPhase::MixedPhaseEoS`).

Since a mixture is not a function of (T, mu) alone, the mixed phase is exposed
through a *stretched* baryon coordinate x that the trajectory solver uses in
place of mu_B: x <= mu_B* is the dilute phase at mu_B = x; mu_B* < x <
mu_B* + W is the mixed phase at mu_B = mu_B* with lambda = 1 - (x - mu_B*)/W;
x >= mu_B* + W is the dense phase at mu_B = x - W. Everything is continuous in
x, so the existing Newton solver walks through the transition; the width W
(100 MeV) is arbitrary and never enters a physical result --
`QCD::physicalMuB` and `QCD::phaseFraction` map x back, and the GUI reports
the physical mu_B and logs lambda whenever the trajectory is inside the mixed
phase (`trajectory.txt` carries it as a last column, NaN when homogeneous).
mu_B*(T, mu_Q) is located exactly, by bisection of the model's own branch
switch on the bare contour (the seam cancels there, so no Thermal-FIST solve
is needed for the search), and memoized per point. Strangeness is not
conserved in the cosmic setting, so mu_S = 0 throughout and the two phases
share (T, mu_B, mu_Q) only.

What it changes in a run: with the homogeneous EoS the trajectory solver finds
no state at all where the required n_B/s falls inside the density gap of the
transition (at b = 0.06 with zero lepton asymmetries that is T = 104..108 MeV
for the parametrized input), while the Gibbs variant passes through the mixed
phase there and coincides with the homogeneous one everywhere else. Near the
transition the five equations can also have several roots; the worker now
keeps, among the converged roots, the one closest to the previous step (it
logs when this kicks in), so the trajectory stays on the branch continuously
connected to its high-temperature start.

### Cross-checking against the reference

`gui/eos_check.cpp` prints the same observables, in the same units and layout,
as the reference's `eos_line`, so the two can be diffed column by column
(`--tabulated` selects the `eos = 3/5` lattice input, `--no-hrg` the bare
contour):

```sh
# this repository
./build/eos_check 300 30 0 90 200 10
# s_contours_c-dev4-tristan  (mu = |(300,30,0)| = 301.4963 MeV,
#                             theta = acos(300/mu), phi = 0)
./build/eos_line 300 90 200 10 --dir 5.710593 0 --qvdw
```

### First-order surface overlay

`assets/first_order_surface_param.dat` (the "first-order surface" overlay of
the 3D view for `eos = 4`) is generated by `gui/first_order_surface.cpp`. For
every (T, mu_Q) of a regular grid at mu_S = 0 it scans mu_B and bisects the
point where the engine's branch selection (highest-pressure stable branch,
`Engine::invert`) switches from the dilute to the dense phase -- the
equal-pressure Maxwell point, solved to 1e-6 MeV in mu_B. The last column is
the baryon-density jump n_B(dense) - n_B(dilute) in fm^-3. The QvdW-HRG seam
cancels in both the pressure difference and the density jump, so the tool runs
with it switched off. Regenerate (about a minute on 10 cores) with

```sh
./build/first_order_surface 80 114.5 0.25 0.5 -o ../assets/first_order_surface_param.dat
```

(positionals: Tmin Tmax dT dmuQ in MeV; `--diag FILE` also writes the phase
densities and the pressure mismatch of every point). The muQ = 0 line agrees
with s_contours_c-dev4's `coexistence` macro to better than 0.01 MeV in mu_B.

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
   (`eos = 3` to `eos = 7`) report the missing dependency at start-up; the
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
