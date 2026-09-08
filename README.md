# ACO TSP Solver

A desktop application that solves the Travelling Salesman Problem with Ant Colony Optimization, visualising the search live: the best tour on a map of Bosnia and Herzegovina, the pheromone trail as edge thickness, and the convergence curve as it develops.

Built on the [natID/natGUI](https://github.com/idzafic/natID) C++ framework. University project for the Non-linear Optimization course at the Faculty of Electrical Engineering (ETF), University of Sarajevo.

[![Release](https://img.shields.io/github/v/release/arminn2206/TSP-ACO-Solver)](https://github.com/arminn2206/TSP-ACO-Solver/releases)
[![Build](https://github.com/arminn2206/TSP-ACO-Solver/actions/workflows/release-all.yml/badge.svg)](https://github.com/arminn2206/TSP-ACO-Solver/actions)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE.txt)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)](https://github.com/arminn2206/TSP-ACO-Solver/releases/latest)

---

## Download

Prebuilt installers for all three platforms are on the [latest release](https://github.com/arminn2206/TSP-ACO-Solver/releases/latest).

| Platform | File | Install |
|---|---|---|
| Windows 10/11 (x64) | `TSP_ACO-win.zip` | Unzip, run the `.exe` (or the `.msi` directly). Keep both files together. |
| macOS — Apple Silicon | `TSP_ACO-macOS-Silicon.zip` | Unzip, drag to Applications. **First launch: right-click → Open.** |
| macOS — Intel | `TSP_ACO-macOS-Intel.zip` | Same as above. |
| Linux (Ubuntu 24.04+) | `TSP_ACO-linux.zip` | Unzip, then `sudo apt install ./tspaco.deb` |

The macOS builds are ad-hoc signed rather than notarised, so Gatekeeper blocks a normal double-click on first launch. Right-click → Open once, and it will open normally from then on.

---

## What it does

The Travelling Salesman Problem asks for the shortest closed tour visiting every city exactly once — an NP-hard combinatorial optimization problem. Ant Colony Optimization attacks it with a population of artificial ants that build tours probabilistically, biased by a pheromone trail that gets reinforced in proportion to tour quality. Good edges accumulate pheromone, the colony concentrates on them, and the tours get shorter.

Each run draws 15–25 cities at random from a set of 100 real Bosnian cities with true latitude/longitude coordinates, so the distance matrix reflects actual geography rather than random points in a square.

### Features

- **Live map** — cities, the current best tour, and the pheromone trail rendered as edge thickness that thins out as the colony converges on a route
- **Convergence chart** — best-so-far cost and per-iteration best cost against iteration number, with the greedy nearest-neighbour tour drawn as a fixed reference line
- **Statistics sidebar** — iteration, best cost, runtime, greedy baseline, and the signed improvement percentage over that baseline
- **Run comparison** — the two most recently completed runs side by side with their differences, so parameter changes can actually be evaluated
- **CSV export** — two files per run: a convergence table for offline analysis, and a full problem instance (coordinates, tour, distance matrix) for verification or replay
- **Reproducible runs** — a fixed seed replays a run exactly; seed and city-draw index are both written into every export. All ACO parameters, the seed, and the animation speed persist across restarts (clamped the same way whether they arrive from the UI or from a previous session)
- **Bilingual UI** — English and Bosnian, switchable at runtime (93 translated strings)
- **Light and dark themes** — colours adapt to the OS setting on startup

---

## Algorithm

The implementation follows the **Ant System** formulation from Dorigo & Stützle, *Ant Colony Optimization* (MIT Press, 2004).

**Tour construction.** From city $i$, an ant picks the next unvisited city $j$ by roulette-wheel selection over

$$p_{ij} \propto \tau_{ij}^{\alpha} \cdot \eta_{ij}^{\beta}, \qquad \eta_{ij} = 1/d_{ij}$$

where $\tau$ is pheromone intensity and $\eta$ is the distance heuristic. Ants start from randomly chosen cities each iteration — launching them all from one fixed city does not change which tours are reachable, but it correlates their early decisions, since every ant would face an identical first choice under identical pheromone.

**Global pheromone update**, applied once per iteration in three explicit steps:

$$\tau \leftarrow (1-\rho)\,\tau \qquad \Delta\tau_{ij} = \sum_k Q/L_k \qquad \tau \leftarrow \tau + \Delta\tau$$

Order matters: evaporation applies to the trail as it stood at the *start* of the iteration, and this iteration's deposits are added afterwards and are therefore not evaporated. Deposits are symmetric — $\Delta\tau_{ij}$ and $\Delta\tau_{ji}$ both receive $Q/L_k$ — since a TSP tour is undirected.

**Feasibility check.** Every constructed tour is validated as a genuine permutation of all cities before it is costed, compared against the incumbent, or allowed to deposit. An infeasible tour would be shorter than a real one and would silently become the reported best.

### Parameters

All parameters are configurable in the UI and are snapshotted once when Start is pressed, so they stay constant for the duration of a run.

| Parameter | Symbol | Default | Effect |
|---|---|---|---|
| Number of ants | — | 20 | Tours built per iteration |
| Iterations | — | 100 | Length of the run |
| Pheromone influence | α | 1.0 | Weight on the trail |
| Heuristic influence | β | 3.0 | Weight on 1/distance |
| Evaporation rate | ρ | 0.5 | Fraction of trail lost per iteration |
| Deposit constant | Q | 100.0 | Scales the per-ant deposit |
| Initial pheromone | τ₀ | 1.0 | Uniform starting trail |
| Random seed | — | random | Fixed value makes the run reproducible |

### Quality baseline

Best tour cost on its own is unanchored — whether 850 km is good depends entirely on which cities were drawn. Every run is therefore measured against a **repetitive nearest-neighbour tour** over the same city set, computed once at load time by running greedy NN from every possible start and keeping the best.

The improvement percentage is displayed **signed and unclamped**. A run stopped early, or given too few ants or iterations, genuinely can fail to beat a greedy tour, and a sidebar that could only report success would be useless as an instrument.

---

## Architecture

```
src/
├── main.cpp             Entry point
├── Application.h        natGUI application, creates the main window
├── MainWindow.h         Menu/toolbar dispatch, Start/Stop/Reset/Export/Compare
├── MainView.h           Splitter layout: map + convergence chart + stats sidebar
├── MapModel.h           ACO engine — distance & pheromone matrices, worker thread
├── ViewMap.h            Map canvas: cities, best tour, pheromone edges
├── ViewConvergence.h    Live convergence chart (primary output)
├── ViewStats.h          Run statistics sidebar
├── ViewCompare.h        Side-by-side comparison of two completed runs
├── DialogCompare.h      Modal host for the comparison view
├── ViewSettings.h       Parameter editor
├── DialogSettings.h     Modal host for settings, persists to OS properties
├── RunExport.h          All file I/O — CSV export
├── Town.h               City primitive: coordinates, name, draw state
├── Primitive.h          Base drawable with visit state
├── GraphType.h          City index type
├── Constants.h          Defaults, thresholds, tuning constants
├── MenuBar.h            Menu definitions
└── ToolBar.h            Toolbar, mirrors every menu action
```

### Matrices

Both the $N \times N$ distance matrix and the $N \times N$ pheromone matrix are natID `dense::DblMatrix` instances. The whole-matrix steps of the update rule use the library's own operators — evaporation is `operator*=`, the deposit application is `operator+=` — rather than hand-written nested loops, which lets the matrix class decide how to traverse its own storage.

The pheromone matrix's diagonal is deliberately left at zero rather than seeded with τ₀. A self-loop is never a legal move, so the diagonal is never read during tour construction; seeded with τ₀ it would instead enter evaporation and drift, silently contributing to the L1 norm reported in the export.

### Threading

The ACO loop runs on a `std::thread` so the UI stays responsive. The model owns a mutex guarding the published state — best tour, cost history, pheromone snapshot — and the worker publishes consistent snapshots that the UI polls once per frame. Widget updates are marshalled back to the main thread with `asyncExecInMainThread`. The worker never touches a widget directly.

The pheromone matrix is worker-owned and unlocked during the update itself, which is safe precisely because no other thread reads it: the UI reads a separate mutex-guarded snapshot published after each complete update, so it can never observe the trail mid-evaporation.

---

## Building from source

### Prerequisites

- [natID SDK](https://github.com/idzafic/natID) cloned to `~/natID.SDK` (and `~/natID.Utils`), with the prebuilt binaries for your platform extracted into `natID.SDK/bin` per that folder's `ReadMe.txt`
- CMake 3.18+
- A C++20 compiler — MSVC 2022+, AppleClang, or GCC 13+
- Linux only: `libgtk-4-dev libadwaita-1-dev libopenal-dev`

### Build

```bash
git clone https://github.com/arminn2206/TSP-ACO-Solver.git
cd TSP-ACO-Solver
cmake -S . -B build
cmake --build build --config Release
```

The binary lands in `~/natID.RAMDisk/Out/tspaco/Release/`.

> **Note for Visual Studio users:** build Release from the command line as shown above. The IDE's own CMake integration has been observed to produce a Debug binary while the configuration dropdown reads Release.

### Packaging

Installers are produced by the SDK's `SetupCollector` tool against [`packaging/tspaco.xml`](packaging/tspaco.xml):

```bash
SetupCollector <path-to-setups>/tspaco.xml
```

`packaging/GTK4.xml` overrides the SDK's own GTK package definition — the stock file points at `$MyBin/GTK/release/bin`, but the Windows SDK ships its GTK runtime DLLs flat in `$MyBin/GTK`.

---

## Continuous integration

[`.github/workflows/release-all.yml`](.github/workflows/release-all.yml) builds installers for all four targets — Windows, macOS ARM, macOS Intel, Linux — on every `v*` tag, and publishes them to a GitHub Release.

Each job installs the natID SDK from scratch, downloads the matching prebuilt binaries, builds in Release, runs `SetupCollector`, and uploads the result. Two environment quirks are handled: the SDK expects a RAM disk at a platform-specific mount point (`R:` / `/Volumes/RAMDisk` / `/media/RAMDisk`), faked with `subst` or a symlink, and the packaging config expects the sources at a fixed path, provided by symlinking the checkout.

The workflow can also be run manually from the Actions tab with `publish_release: no` to verify a build without creating a release.

---

## Project context

Implemented for the Non-linear Optimization course at ETF Sarajevo, extending the professor's `B_S03_Maps` natID example. The map rendering, canvas animation loop, and background-thread architecture come from that example; cities replace towns, TSP tour edges replace roads, and the threaded loop drives the ACO iteration engine.

The original `Graph` class from the example was dropped: ACO needs a complete graph as an $N \times N$ matrix for pheromone and distance lookups, not the adjacency-list structure a shortest-path search wants.

**Author:** Armin Memišević (index 20016)
**Theoretical reference:** Marco Dorigo & Thomas Stützle, *Ant Colony Optimization*, MIT Press, 2004

---

## License

MIT — see [LICENSE.txt](LICENSE.txt).

Built on the natID/natGUI framework and adapted from the `B_S03_Maps` example, both by [prof. Izudin Džafić](https://github.com/idzafic), used with permission.
