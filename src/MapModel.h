//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
//  Rewritten for ACO/TSP: complete-graph ant colony optimization.
//

#pragma once
#include "Town.h"
#include <dense/Matrix.h>
#include <random>
#include <cmath>
#include <limits>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string>
#include <cassert>

// Everything needed to reconstruct and analyse one completed (or stopped) run, in a
// single locked snapshot. RunExport.h turns this into CSV.
//
// A plain data struct holding std:: types rather than a view onto the Model: the
// export path must not hold Model's mutex while it does file I/O, and the strings
// have to outlive the td::String objects inside the towns.
//
// It carries the PROBLEM (cities, coordinates, distance matrix, greedy baseline), the
// PARAMETERS and the RESULT - the minimum needed for a third party to reproduce the
// run from the file alone.
struct RunRecord
{
    // ---- problem instance -------------------------------------------------
    int numCities = 0;
    std::vector<std::string> cityNames;
    std::vector<double> cityLat, cityLon; // original degrees, as read from the map file
    std::vector<double> cityX, cityY;     // normalized [0,1] display coordinates
    std::vector<double> dist;             // row-major numCities x numCities, kilometres
    float nnLength = 0;
    bool  nnValid = false;
    GraphType startMarkerID = 1;

    // Which city draw of this session produced the instance above (0 = the draw made
    // at launch, 1 = after the first "New cities", and so on).
    //
    // Exported because the seed alone does NOT identify the city set: loadTowns()
    // seeds the shuffle with makeRNG(seed, cityDrawIndex * 2). Seed + cityDrawIndex
    // together pin the instance exactly.
    int cityDrawIndex = 0;

    // ---- parameters (constant for the whole run, per the project proposal) --
    int numAnts = 0;
    int numIterations = 0;
    int seed = 0;
    float alpha = 0;
    float beta = 0;
    float evaporationRate = 0;
    float Q = 0;
    float initialPheromone = 0;

    // ---- result ------------------------------------------------------------
    bool tourFound = false;
    float bestLength = 0;
    int bestIteration = 0;  // iteration the best tour was found at
    int iterationsRun = 0;  // may be < numIterations if Stop was pressed
    std::vector<GraphType> bestTour;

    // The three per-iteration series. All the same length by construction.
    std::vector<float>  bestSoFar;
    std::vector<float>  iterBest;
    std::vector<double> pheromoneL1;

    // Wall-clock duration, filled in by the caller: the run timer lives in ViewMap,
    // which is also where a run starts and finishes.
    double runtimeSeconds = 0;

    // True only if the iteration loop ran all the way to numIterations without Stop
    // ever being pressed. Set from Model::_ranToCompletion, i.e. recorded by the
    // worker at the moment the loop exited - the callers cannot derive it, because
    // Stop pressed during the FINAL iteration still leaves _iteration ==
    // numIterations.
    bool completed = false;
};

struct RawTownData
{
    td::String nodeID;
    td::String name;
    double lat = 0;
    double lon = 0;
};

// One town-pair edge snapshot for the pheromone background layer. Points are already
// scaled to the current view size, so ViewMap can build gui::Shape lines directly.
struct PheromoneEdge
{
    gui::Point p1;
    gui::Point p2;
    double value = 0;
};

// Snapshot of convergence state for the ViewConvergence status line. "converged"
// means the best-so-far cost hasn't improved for cConvergencePatienceIterations
// consecutive iterations.
struct ConvergenceInfo
{
    int bestIteration = 0;    // iteration (1-based) at which the current best was found
    int currentIteration = 0; // iteration reached so far
    int patience = 0;         // patience threshold used to decide "converged"
    bool converged = false;
};

// The two curves ViewConvergence plots, pulled together under a single lock so they
// can never be one element out of step with each other.
//
//  bestSoFar[i] - cost of the best tour found in iterations 1..i+1. Monotonically
//                 non-increasing, so it is always a descending staircase and on its
//                 own says nothing about how hard the colony is still searching.
//  iterBest[i]  - cost of the best tour built by any ant DURING iteration i+1 alone.
//                 The noisy line: it shows the search exploring, and visibly settles
//                 onto the staircase as the trail sharpens. Dorigo & Stutzle plot
//                 both together for exactly this reason.
struct ConvergenceSeries
{
    std::vector<float> bestSoFar;
    std::vector<float> iterBest;
};

// Everything the stats sidebar (ViewStats) displays, in one consistent snapshot, so
// the panel can never show an iteration number from after the best cost beside it.
struct RunStats
{
    int iteration = 0;
    int numIterations = 0;
    int numAnts = 0;
    int numCities = 0;
    int seed = 0;
    int bestIteration = 0;
    float bestLength = 0;
    bool tourFound = false;   // false => bestLength is still the FLT_MAX sentinel
    bool converged = false;
    bool searching = false;

    // Greedy baseline for the current city set, so "best cost" can be read as a
    // quality claim rather than a bare number. Available as soon as the cities load.
    float nnLength = 0;
    bool  nnValid = false;
    // 100 * (nnLength - bestLength) / nnLength. Meaningful only when nnValid && tourFound.
    float improvementPct = 0;
};

class Model
{
public:
    enum class InfoLoc : td::BYTE { TopLeft = 0, BottomLeft, TopRight, BottomRight };

    std::vector<Town> _towns;
    // Town name -> 1-based town ID, used by MainView::populateCombos() to fill the
    // Start-town combo box.
    MapTownNameToID _mapTownNameToNodeID;
    std::thread _pathFinder;
    std::mutex _mutex;
    Primitive::Options _options;
    InfoLoc _infoLoc = InfoLoc::TopLeft;

protected:
    // Distances between every pair of towns (air distance, complete graph) -
    // N x N natID dense matrix.
    dense::DblMatrix _dist{ 1,1 };
    // Pheromone matrix (n x n). OWNED EXCLUSIVELY BY THE WORKER THREAD once a run has
    // started: initPheromone(), constructTour(), evaporatePheromone() and
    // depositPheromone() all touch it without locking, which is safe precisely because
    // no other thread reads it directly. The UI thread reads _pheromoneSnapshot below
    // instead.
    //
    // The DIAGONAL is held at exactly zero for the whole run - see initPheromone().
    dense::DblMatrix _pheromone{ 1,1 };

    // Per-iteration pheromone contribution, Delta-tau in Dorigo & Stutzle's notation.
    //
    // Kept separate so the Ant System global update reads as the reference states it:
    //
    //     tau        <- (1 - rho) * tau        evaporatePheromone()   -> operator*=
    //     Delta-tau  <- sum over ants of Q/L   depositPheromone()     -> per-edge
    //     tau        <- tau + Delta-tau        applyPheromoneDeposit()-> operator+=
    //
    // It also means the deposit is summed at full precision before being added to a
    // trail that may be orders of magnitude larger.
    //
    // Same ownership rule as _pheromone: worker thread only, no locking needed.
    dense::DblMatrix _deltaPheromone{ 1,1 };

    // Row-major n x n copy of _pheromone as of the last publishPheromoneSnapshot().
    // Written by the worker and read by the UI thread, both strictly under _mutex.
    // _pheromoneSnapshotN == 0 means "no run has produced pheromone data yet".
    std::vector<double> _pheromoneSnapshot;
    size_t _pheromoneSnapshotN = 0;

    std::vector<GraphType> _bestTour; // 1-based town IDs, in visiting order
    float _bestLength = std::numeric_limits<float>::max();

    // Written on the worker thread and read on the UI thread (or vice versa) outside
    // any lock. Plain int/bool here would be a data race - formally UB, and the
    // compiler would be free to hoist the _stopRequested read out of the loop.
    std::atomic<int> _iteration{ 0 };
    std::atomic<int> _bestFoundAtIteration{ 0 }; // iteration (1-based) the current _bestLength was found at

    // The authoritative answer to "did this run finish?", recorded by the worker at
    // the end of runACO(). Comparing _iteration against _options.numIterations is NOT
    // equivalent: Stop pressed during the FINAL iteration breaks the ant loop, still
    // completes the pheromone update, and still leaves _iteration == numIterations.
    std::atomic<bool> _ranToCompletion{ false };

    // best-so-far cost after each completed iteration (index 0 = after iteration 1).
    // Read/written under _mutex.
    std::vector<float> _costHistory;

    // Best cost produced by any ant DURING each completed iteration, same indexing and
    // same length as _costHistory. Also read/written under _mutex.
    std::vector<float> _iterBestHistory;

    // L1 norm of the pheromone matrix after each completed iteration, same indexing
    // and length as the two series above. Read/written under _mutex.
    //
    // dense::Matrix::getL1Norm() is the COLUMN-SUM norm (verified against the SDK
    // header, not assumed). Since tau is symmetric and non-negative, that is the total
    // trail mass incident on the single most heavily reinforced city - NOT "total
    // pheromone", and it must not be labelled as such. It climbs as the colony commits
    // to a small set of edges, so its levelling off is a convergence signal that does
    // not look at tour cost at all. Exported so the report can plot it against cost.
    //
    // Because the diagonal is held at zero (initPheromone), each column sum is purely
    // the trail on that city's real incident edges.
    std::vector<double> _pheromoneNormHistory;

    // Greedy nearest-neighbour tour length for the current city set, the quality
    // baseline in the stats sidebar.
    //
    // Belongs to the PROBLEM INSTANCE, not to a run: computed once in loadTowns() and
    // deliberately surviving reset(), so Reset does not blank it out. clean() clears
    // it, because that always precedes a fresh city set.
    float _nnLength = 0;
    bool  _nnValid = false;

    // Original geographic coordinates (degrees) of the selected cities, parallel to
    // _towns. Town itself keeps only the normalized [0,1] display position, which is
    // meaningless outside this window's bounding box - an exported instance has to
    // carry the real coordinates. Instance data, so cleared by clean(), not reset().
    std::vector<std::pair<double, double>> _townLatLong;

    gui::Shape _tourShape;
    // Only ever touched with _mutex held, so this one does NOT need to be atomic.
    bool _tourShapeDirty = false;

    std::atomic<bool> _searching{ false };
    std::atomic<bool> _stopRequested{ false };
    std::atomic<bool> _tourFound{ false };

    // How many times loadTowns() has drawn a city set in this process, used as the RNG
    // stream index for the city subsampling.
    //
    // Deliberately NOT cleared by reset() or clean(): it counts draws for the whole
    // session, so "New cities" gives a DIFFERENT set each time even with a fixed seed,
    // while relaunching with that seed replays the identical sequence of sets.
    unsigned int _cityDrawCount = 0;

    // The draw ordinal that produced the city set currently loaded. Unlike
    // _cityDrawCount (which keeps advancing) this describes the instance on screen, so
    // it is what getRunRecord() exports. See RunRecord::cityDrawIndex.
    unsigned int _cityDrawIndex = 0;

protected:
    // Builds a Mersenne Twister engine for one specific use ("stream").
    //
    // A seed of cSeedRandom (0) means "not reproducible": the engine is seeded from
    // std::random_device. Any other value makes the result deterministic.
    //
    // The stream index keeps independent uses off the same number sequence - seeding
    // the city shuffle and the ants' roulette draws identically would silently
    // correlate two things that should be independent. The offset is the 32-bit
    // golden-ratio constant, the standard choice for this kind of mixing.
    //
    // Stream allocation:
    //   even streams (0, 2, 4, ...) - city subsampling in loadTowns(), one per draw
    //   stream 1                    - the ants' decisions in runACO()
    // Splitting by parity means the city stream can advance indefinitely without ever
    // landing on the ant stream.
    static std::mt19937 makeRNG(int seed, unsigned int stream)
    {
        if (seed == cSeedRandom)
        {
            std::random_device rd;
            return std::mt19937(rd());
        }
        return std::mt19937((unsigned int)seed + stream * 0x9E3779B9u);
    }

    //air distance between any two nodes (same formula as professor's original)
    float calcDistance(GraphType nodeA, GraphType nodeB)
    {
        auto fromNode = nodeA - 1;
        auto toNode = nodeB - 1;
        const auto& locFrom = _towns[fromNode].getLocation();
        const auto& locTo = _towns[toNode].getLocation();
        auto dx = math::abs(locFrom.x - locTo.x);
        auto dy = math::abs(locFrom.y - locTo.y);
        float dxKm = float(dx * _options.ratioLong);
        float dyKm = float(dy * _options.ratioLat);
        return std::sqrt(dxKm * dxKm + dyKm * dyKm);
    }

    void buildDistanceMatrix()
    {
        size_t n = _towns.size();
        _dist.reserve((td::UINT4)n, (td::UINT4)n, nullptr, true); // true = zero-initialize storage
        // Explicit, because reserve() may reuse an allocation from a previous, larger
        // city set. Only the strict upper/lower triangle is written below, so the
        // diagonal relies on this to be zero.
        _dist.zeros();
        auto m = _dist.getManipulator();
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = i + 1; j < n; ++j)
            {
                double d = calcDistance(GraphType(i + 1), GraphType(j + 1));
                m(i, j) = d;
                m(j, i) = d;
            }
        }
    }

    // Greedy baseline: repetitive nearest neighbour.
    //
    // Runs the classic nearest-neighbour construction from EVERY possible starting
    // city and keeps the shortest of the n tours it produces.
    //
    // Best-of-all-starts on purpose: plain NN from one city is heavily start-dependent
    // and typically lands ~25% above optimal, which makes for a flatteringly weak
    // comparison. The repetitive variant is genuinely competitive (~15% above optimal
    // at this size), so beating it is a claim worth making. O(n^3), which at
    // n <= cMaxCities (25) is ~15k operations, run once per city set rather than per run.
    //
    // Called from loadTowns() on the UI thread, after buildDistanceMatrix().
    float computeNearestNeighbourTour()
    {
        size_t n = _towns.size();
        if (n < 2)
            return 0.0f;

        auto d = _dist.getManipulator();
        float best = std::numeric_limits<float>::max();
        std::vector<bool> used(n, false);

        for (size_t s = 0; s < n; ++s)
        {
            std::fill(used.begin(), used.end(), false);
            size_t cur = s;
            used[cur] = true;
            float total = 0;

            // False if the construction from this start ever fails to find a next city.
            // Same discipline as isHamiltonianTour(): a short tour is CHEAPER than a
            // real one, so it would silently become the reported baseline and make the
            // colony's improvement look worse than it is. Unreachable on a complete
            // graph; discarded rather than trusted.
            bool complete = true;

            for (size_t step = 1; step < n; ++step)
            {
                size_t bestJ = n;   // n == "none chosen yet"
                double bestD = 0;
                for (size_t j = 0; j < n; ++j)
                {
                    if (used[j])
                        continue;
                    double dj = d(cur, j);
                    if (bestJ == n || dj < bestD)
                    {
                        bestJ = j;
                        bestD = dj;
                    }
                }
                if (bestJ == n)
                {
                    complete = false;
                    break; // unreachable on a complete graph, but do not loop forever
                }

                used[bestJ] = true;
                total += (float)bestD;
                cur = bestJ;
            }

            if (!complete)
                continue; // discard this start entirely rather than record a short tour

            total += (float)d(cur, s); // close the cycle back to this start
            if (total < best)
                best = total;
        }

        // If every start was discarded, return the "no baseline" sentinel rather than
        // FLT_MAX: loadTowns() sets _nnValid from "nn > 0", so 0 correctly disables the
        // baseline row and the chart's reference line, while FLT_MAX would pass that
        // test and blow up the chart's Y range.
        if (best == std::numeric_limits<float>::max())
            return 0.0f;

        return best;
    }

    // Rotates a tour so that it begins at anchorID, without changing which cycle it
    // represents or how long it is.
    //
    // Ants start from random cities while the map still draws a flag at
    // options.startID. A TSP tour is a cycle, so "starts at X" is purely presentation;
    // rotating the stored best tour keeps the drawn polyline consistent with the flag
    // without constraining the search.
    static void rotateTourToStart(std::vector<GraphType>& tour, GraphType anchorID)
    {
        auto it = std::find(tour.begin(), tour.end(), anchorID);
        if (it == tour.end() || it == tour.begin())
            return;
        std::rotate(tour.begin(), it, tour.end());
    }

    // Copies the worker-owned _pheromone matrix into the mutex-guarded snapshot the UI
    // thread reads. Worker thread ONLY, and only where the matrix is consistent. Takes
    // the lock internally, so callers must NOT already hold it.
    void publishPheromoneSnapshot()
    {
        size_t n = _towns.size();
        std::vector<double> snap;
        if (n >= 2)
        {
            snap.resize(n * n);
            auto m = _pheromone.getManipulator();
            for (size_t i = 0; i < n; ++i)
                for (size_t j = 0; j < n; ++j)
                    snap[i * n + j] = m(i, j);
        }

        lock();
        _pheromoneSnapshot.swap(snap);
        _pheromoneSnapshotN = (n >= 2) ? n : 0;
        unlock();
    }

    void initPheromone()
    {
        size_t n = _towns.size();
        _pheromone.reserve((td::UINT4)n, (td::UINT4)n, nullptr, true); // true = zero-initialize storage

        // Explicit, for the same reason buildDistanceMatrix() clears _dist: reserve()
        // may reuse an allocation from a previous, larger city set, and only the
        // off-diagonal entries are written below.
        _pheromone.zeros();

        auto m = _pheromone.getManipulator();
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = 0; j < n; ++j)
            {
                // The DIAGONAL is deliberately left at zero rather than seeded with tau0.
                //
                // tau(i,i) is the trail on an edge from a city to itself, which does not
                // exist: constructTour() marks the current city visited before
                // enumerating candidates, so m(c,c) is never read, and depositPheromone()
                // can never write it.
                //
                // It was not harmless. Seeded with tau0 it entered evaporatePheromone()'s
                // whole-matrix scaling and decayed geometrically, and getL1Norm() is a
                // column sum - so every entry of the exported pheromone_l1 series would
                // carry an extra tau0*(1-rho)^k term underneath the real trail mass.
                if (i == j)
                    continue;
                m(i, j) = _options.initialPheromone;
            }
        }

        // Delta-tau must be conformable with tau for applyPheromoneDeposit()'s
        // operator+=, so it is allocated here, in the one place tau's dimensions are
        // set, and the two can never drift apart.
        _deltaPheromone.reserve((td::UINT4)n, (td::UINT4)n, nullptr, true);
        _deltaPheromone.zeros();

        // Publish the uniform starting state so the pheromone layer has something to
        // draw from the very first frame of the run.
        publishPheromoneSnapshot();
    }

    std::vector<GraphType> constructTour(GraphType start, std::mt19937& rng)
    {
        size_t n = _towns.size();
        std::vector<bool> visited(n + 1, false); // 1-based indexing
        std::vector<GraphType> tour;
        tour.reserve(n);

        GraphType current = start;
        visited[current] = true;
        tour.push_back(current);

        auto pher = _pheromone.getManipulator();
        auto dist = _dist.getManipulator();

        for (size_t step = 1; step < n; ++step)
        {
            std::vector<GraphType> candidates;
            std::vector<double> weights;
            double sum = 0;

            for (GraphType c = 1; c <= (GraphType)n; ++c)
            {
                // The current city is always marked visited, so c == current is skipped
                // here - which is why the zero diagonal in _pheromone is never read.
                if (visited[c])
                    continue;

                double tauVal = pher(current - 1, c - 1);
                double d = dist(current - 1, c - 1);
                double eta = (d > 1e-6) ? (1.0 / d) : 1e6;
                double w = std::pow(tauVal, _options.alpha) * std::pow(eta, _options.beta);

                candidates.push_back(c);
                weights.push_back(w);
                sum += w;
            }

            if (candidates.empty())
                break; // safety, should not happen

            GraphType next;
            if (sum <= 0.0)
            {
                std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
                next = candidates[pick(rng)];
            }
            else
            {
                std::uniform_real_distribution<double> pick(0.0, sum);
                double r = pick(rng);
                double acc = 0;
                next = candidates.back();
                for (size_t k = 0; k < candidates.size(); ++k)
                {
                    acc += weights[k];
                    if (r <= acc)
                    {
                        next = candidates[k];
                        break;
                    }
                }
            }

            visited[next] = true;
            tour.push_back(next);
            current = next;
        }

        return tour;
    }

    // Feasibility check: a tour is only a solution if it is a permutation of 1..n -
    // right length, every id in range, no id twice.
    //
    // constructTour() cannot currently produce an infeasible tour, but if it ever did,
    // the returned tour would be SHORT and therefore CHEAPER than any real one:
    // tourLength() would report that small number, it would beat _bestLength, and the
    // infeasible tour would be drawn on the map, plotted on the curve and reported as
    // beating the baseline by a wide margin. A solver must never accept a solution
    // that violates its own constraints, so this is checked rather than assumed.
    //
    // O(n) with n <= cMaxCities (25), run once per ant.
    bool isHamiltonianTour(const std::vector<GraphType>& tour) const
    {
        size_t n = _towns.size();
        if (n < 2 || tour.size() != n)
            return false;

        std::vector<bool> seen(n + 1, false); // 1-based ids
        for (GraphType id : tour)
        {
            if (id < 1 || id >(GraphType)n)
                return false;
            if (seen[id])
                return false;
            seen[id] = true;
        }
        return true;
    }

    // Not const: dense::DblMatrix access goes through a (non-const) manipulator.
    float tourLength(const std::vector<GraphType>& tour)
    {
        if (tour.size() < 2)
            return 0.0f;

        auto dist = _dist.getManipulator();

        float total = 0;
        for (size_t i = 0; i + 1 < tour.size(); ++i)
            total += (float)dist(tour[i] - 1, tour[i + 1] - 1);
        total += (float)dist(tour.back() - 1, tour.front() - 1); // close the loop
        return total;
    }

    // tau <- (1 - rho) * tau
    //
    // Evaporation is defined as a uniform scaling of the trail, so this is one
    // dense::Matrix::operator*= rather than a nested n^2 loop - it lets the matrix
    // library traverse in its own storage order.
    //
    // Scaling the whole matrix also scales the diagonal, which is exactly why the
    // diagonal is initialised to zero: zero stays zero and never reaches getL1Norm().
    void evaporatePheromone()
    {
        double keep = 1.0 - (double)_options.evaporationRate;
        _pheromone *= keep;
    }

    // Delta-tau <- 0, called once per iteration before any ant deposits.
    void beginPheromoneDeposit()
    {
        _deltaPheromone.zeros();
    }

    // Accumulates ONE ant's contribution Q/L into Delta-tau. Does not touch tau.
    //
    // Per-edge by necessity: a tour touches exactly n of the n^2 entries, so no
    // whole-matrix operation expresses "add Q/L along this permutation". The two steps
    // that ARE whole-matrix (evaporatePheromone, applyPheromoneDeposit) are both
    // library calls.
    void depositPheromone(const std::vector<GraphType>& tour, float length)
    {
        // Second line of defence, and also how the empty slots left in antTours by
        // rejected ants (or by Stop landing mid-iteration) are skipped:
        // isHamiltonianTour() is false for an empty vector, so this one test covers
        // both. Reinforcing an infeasible tour is worse than merely reporting it - the
        // pheromone biases every subsequent ant towards the same edges.
        //
        // It is also what guarantees the Delta-tau diagonal stays zero.
        if (length <= 0.0f || !isHamiltonianTour(tour))
            return;

        double deposit = _options.Q / length;
        auto m = _deltaPheromone.getManipulator();
        for (size_t i = 0; i + 1 < tour.size(); ++i)
        {
            size_t a = tour[i] - 1, b = tour[i + 1] - 1;
            m(a, b) = m(a, b) + deposit;
            m(b, a) = m(b, a) + deposit;
        }
        size_t a = tour.back() - 1, b = tour.front() - 1;
        m(a, b) = m(a, b) + deposit;
        m(b, a) = m(b, a) + deposit;
    }

    // tau <- tau + Delta-tau, as a single dense::Matrix::operator+=.
    //
    // The dimension check is not ceremony: operator+= requires conformable operands,
    // and failing loudly beats adding mismatched buffers if a future change ever
    // resizes one without the other.
    void applyPheromoneDeposit()
    {
        assert(_pheromone.getNoOfRows() == _deltaPheromone.getNoOfRows());
        assert(_pheromone.getNoOfCols() == _deltaPheromone.getNoOfCols());
        _pheromone += _deltaPheromone;
    }

    // Builds the gui::Shape for the tour line. MUST be called only from the main (UI)
    // thread, since gui::Shape construction touches graphics resources.
    void rebuildTourShape()
    {
        if (_bestTour.size() < 2)
            return;

        std::vector<gui::Point> pts;
        pts.reserve(_bestTour.size() + 1);
        for (GraphType id : _bestTour)
        {
            const auto& loc = _towns[id - 1].getLocation();
            pts.push_back(gui::Point(loc.x * _options.viewSize.width, loc.y * _options.viewSize.height));
        }
        const auto& loc0 = _towns[_bestTour.front() - 1].getLocation();
        pts.push_back(gui::Point(loc0.x * _options.viewSize.width, loc0.y * _options.viewSize.height));

        _tourShape.createPolyLine(pts.data(), pts.size());
    }

    // Safe to call from the worker thread: only touches plain data, no gui::Shape.
    void updateTourGUIState()
    {
        for (auto& t : _towns)
            t.setGUIState(Primitive::Status::Unvisited);

        for (GraphType id : _bestTour)
            _towns[id - 1].setGUIState(Primitive::Status::OnPath);
    }

    void runACO()
    {
        size_t n = _towns.size();
        if (n < 2)
            return;

        buildDistanceMatrix();
        initPheromone();

        lock();
        _bestTour.clear();
        _bestLength = std::numeric_limits<float>::max();
        _tourFound = false;
        _tourShapeDirty = false;
        _costHistory.clear();
        _iterBestHistory.clear();
        // Cleared here alongside the other two series, not only in reset(). findPath()
        // calls reset() immediately before spawning this thread, so today this changes
        // nothing - but the three series are appended together, must stay the same
        // length and are read together by the export, so any future path reaching
        // runACO() without going through reset() must not leave one carrying a tail.
        _pheromoneNormHistory.clear();
        unlock();

        _iteration = 0;
        // This run has not finished yet; a stale true would let a run stopped early
        // report itself as complete.
        _ranToCompletion = false;

        // The city the STORED best tour is rotated to begin at, so the drawn polyline
        // lines up with the start flag. A display anchor only - it does not constrain
        // where the ants begin.
        GraphType anchorID = _options.startID;
        if (anchorID < 1 || anchorID >(GraphType)n)
            anchorID = 1;

        // Stream 1 - the ant decision stream. See makeRNG().
        std::mt19937 rng = makeRNG(_options.seed, 1);

        // Ants are placed on randomly chosen cities, as in Dorigo & Stutzle's Ant
        // System. Launching every ant from one fixed city does not change which tours
        // are reachable (a tour is a cycle) but it does correlate the ants' early
        // decisions, since they all face the identical first choice under identical
        // pheromone.
        std::uniform_int_distribution<unsigned int> startPick(1, (unsigned int)n);

        for (int iter = 0; iter < _options.numIterations; ++iter)
        {
            if (_stopRequested)
                break;

            _iteration = iter + 1;

            std::vector<std::vector<GraphType>> antTours(_options.numAnts);
            std::vector<float> antLengths(_options.numAnts, 0.0f);

            // Best tour built during THIS iteration alone, and whether any ant completed
            // at all (false only if Stop landed on the very first ant).
            float iterBest = std::numeric_limits<float>::max();
            bool antCompleted = false;

            for (int a = 0; a < _options.numAnts; ++a)
            {
                if (_stopRequested)
                    break;

                GraphType antStart = (GraphType)startPick(rng);
                std::vector<GraphType> tour = constructTour(antStart, rng);

                // Reject anything that is not a permutation of 1..n BEFORE it is costed
                // or compared - see isHamiltonianTour(). The slot is left empty and
                // antCompleted untouched, so a rejected ant contributes to neither curve
                // nor to the deposit step.
                if (!isHamiltonianTour(tour))
                    continue;

                antTours[a] = std::move(tour);
                antLengths[a] = tourLength(antTours[a]);
                antCompleted = true;

                if (antLengths[a] < iterBest)
                    iterBest = antLengths[a];

                if (antLengths[a] < _bestLength)
                {
                    // Rotate OUTSIDE the lock - pure vector work on a local copy.
                    std::vector<GraphType> bestCopy = antTours[a];
                    rotateTourToStart(bestCopy, anchorID);

                    lock();
                    _bestLength = antLengths[a];
                    _bestTour = std::move(bestCopy);
                    _tourFound = true;
                    _tourShapeDirty = true;
                    _bestFoundAtIteration = _iteration.load();
                    unlock();
                }
            }

            // ---- Ant System global pheromone update -------------------------------
            //   tau       <- (1 - rho) * tau          (dense::Matrix::operator*=)
            //   Delta-tau <- sum_k Q / L_k            (per-edge accumulation)
            //   tau       <- tau + Delta-tau          (dense::Matrix::operator+=)
            //
            // Three named steps so this maps one-to-one onto the update rule in Dorigo &
            // Stutzle. Note the order: evaporation applies to the trail as it stood at
            // the START of this iteration, and this iteration's deposits are added
            // afterwards and are therefore NOT evaporated, which is what the rule
            // specifies.
            evaporatePheromone();

            beginPheromoneDeposit();
            for (int a = 0; a < _options.numAnts; ++a)
                depositPheromone(antTours[a], antLengths[a]);
            applyPheromoneDeposit();
            // -----------------------------------------------------------------------

            // Sampled after the full update, so every entry describes the trail exactly
            // as the NEXT iteration's ants will see it.
            double pheromoneNorm = _pheromone.getL1Norm();

            // Matrix is now in a consistent post-update state - safe to publish. Must
            // happen outside the lock block below (it locks itself).
            publishPheromoneSnapshot();

            lock();
            updateTourGUIState();
            // Record both series or neither, so they stay the same length. If Stop
            // landed on the very first ant of the very first iteration, no ant completed
            // and _bestLength is still the FLT_MAX sentinel; appending it would put a
            // 3.4e38 point on the curve and flatten every real sample.
            if (antCompleted && _tourFound)
            {
                _costHistory.push_back(_bestLength);
                _iterBestHistory.push_back(iterBest);
                _pheromoneNormHistory.push_back(pheromoneNorm);
            }
            unlock();

            thread::sleepMilliSeconds(_options.sleepMS);
        }

        // Record HOW the loop was left, while the worker still knows. Reading
        // _stopRequested rather than counting iterations is the whole point: Stop during
        // the final iteration leaves _iteration == numIterations and then falls out
        // through the normal exit condition, which an iteration count cannot distinguish
        // from a genuine finish.
        _ranToCompletion = !_stopRequested.load();
    }

public:
    Model()
    {
        auto pApp = gui::getApplication();
        if (pApp->isDarkMode())
        {
            _options.normalColor = td::ColorID::Gray;
            _options.pathColor = td::ColorID::Orange;
            _options.txtColor = td::ColorID::Cyan;
            _options.pheromoneColor = td::ColorID::Green;
        }
        else
        {
            _options.normalColor = td::ColorID::Gray;
            _options.pathColor = td::ColorID::DarkRed;
            _options.txtColor = td::ColorID::DarkBlue;
            _options.pheromoneColor = td::ColorID::Blue;
        }
    }

    // Without this, destroying a Model while _pathFinder is still joinable runs
    // std::thread's destructor on a live thread, which calls std::terminate() and kills
    // the process with no diagnostic. MainWindow::shouldClose() refuses to close while
    // a run is in progress, so this is the safety net for every other path.
    ~Model()
    {
        _stopRequested = true;
        if (_pathFinder.joinable())
            _pathFinder.join();
    }

    void lock() { _mutex.lock(); }
    void unlock() { _mutex.unlock(); }

    void reset()
    {
        lock();
        _bestTour.clear();
        _bestLength = std::numeric_limits<float>::max();
        _tourFound = false;
        _tourShapeDirty = false;
        _costHistory.clear();
        _iterBestHistory.clear();
        _pheromoneNormHistory.clear();
        // Snapshot is mutex-guarded, so it must be cleared inside this lock block.
        _pheromoneSnapshot.clear();
        _pheromoneSnapshotN = 0;
        unlock();

        // NOTE: _nnLength / _nnValid are deliberately NOT cleared here. The greedy
        // baseline describes the city set, not the run, so it must survive a Reset -
        // clean() clears it, because that always precedes a new city set.

        _iteration = 0;
        _bestFoundAtIteration = 0;
        _stopRequested = false;
        _searching = false;
        // Belongs to the run being discarded, and a run that does not exist has
        // certainly not completed.
        _ranToCompletion = false;

        for (auto& town : _towns)
            town.reset();
    }

    void clean()
    {
        _towns.clear();
        _mapTownNameToNodeID.clear();
        // Instance data, discarded with the city set it describes. reset() must NOT
        // clear these: Reset keeps the same cities.
        _townLatLong.clear();
        _nnLength = 0;
        _nnValid = false;
        reset();
    }

    void init(unsigned int nTowns)
    {
        _towns.reserve(nTowns);
    }

    // Draws the towns and the best tour. Run statistics are no longer appended to
    // ViewMap's on-canvas overlay - they live in the ViewStats sidebar.
    void draw()
    {
        lock();

        for (const auto& town : _towns)
            town.draw(_options);

        if (_bestTour.size() >= 2)
        {
            if (_tourShapeDirty)
            {
                rebuildTourShape();
                _tourShapeDirty = false;
            }
            _tourShape.drawWire(_options.pathColor, cLineWidthOnPath);
        }

        unlock();
    }

    // Called from ViewMap::onResize (UI thread) whenever the canvas changes size.
    //
    // THE WHOLE BODY runs under the lock, including the _options.viewSize write:
    // getPheromoneEdges() and rebuildTourShape() both read it inside the lock, so
    // writing it unguarded while locked readers read it would be a data race - and a
    // lock that protects the readers but not the writer protects nothing. Resizing
    // during a run is entirely ordinary, so this is a reachable case.
    //
    // Nothing called from here re-enters the lock, and the critical section is O(n)
    // over at most cMaxCities (25) towns.
    void updateModelSize(const gui::Size& sz)
    {
        lock();

        _options.viewSize = sz;
        for (auto& town : _towns)
            town.updatePosition(sz);

        if (_bestTour.size() >= 2)
            _tourShapeDirty = true; // rebuild on next draw() with the new size

        unlock();
    }

    void append(Town& t)
    {
        GraphType townID = GraphType(_towns.size() + 1);
        t.setID(townID);

        // Copied out BEFORE the move below, so this does not depend on Primitive's move
        // constructor happening to copy _name rather than move it.
        td::String townName = t.getName();

        _towns.emplace_back(std::move(t));

        _mapTownNameToNodeID[townName] = townID;
    }

    std::thread& getThread() { return _pathFinder; }

    bool findPath(const gui::thread::MainThreadSharedFunction& completionCallBack)
    {
        if (_searching)
            return true;

        if (_towns.size() < 2)
            return false;

        reset();
        _searching = true;
        _pathFinder = std::thread([this, completionCallBack]()
            {
                runACO();
                _searching = false;
                gui::thread::asyncExecInMainThread(completionCallBack);
            });
        return true;
    }

    void stopSearching()
    {
        _stopRequested = true;
    }

    Primitive::Options& getOptions() { return _options; }

    const MapTownNameToID& getMapNameToID() const { return _mapTownNameToNodeID; }

    // Number of towns currently loaded. Distinct from getMapNameToID().size(), which
    // counts unique NAMES: town ids run 1..getTownCount() whether or not two towns
    // share a name, so anything indexed by id must be sized from this.
    //
    // Safe to call from the UI thread without the lock: _towns is only ever resized by
    // loadTowns()/clean(), both of which run on the UI thread while no worker is active.
    size_t getTownCount() const { return _towns.size(); }

    void setDistanceRatios(float rLat, float rLong)
    {
        _options.ratioLat = rLat;
        _options.ratioLong = rLong;
    }

    InfoLoc getInfoLocation() const { return _infoLoc; }

    void setInfoLocPosition(const td::String& strLoc)
    {
        _infoLoc = InfoLoc::TopLeft;
        if (strLoc.length() != 2)
            return;
        if (strLoc.compare("BR", 2))
            _infoLoc = InfoLoc::BottomRight;
        else if (strLoc.compare("BL", 2))
            _infoLoc = InfoLoc::BottomLeft;
        else if (strLoc.compare("TR", 2))
            _infoLoc = InfoLoc::TopRight;
    }

    bool isSearching() const { return _searching; }

    bool isGoalFound() const { return _tourFound; }

    // Both convergence series in one consistent snapshot. Taking them under a single
    // lock matters: pulled separately, a worker append landing between the two copies
    // would hand the canvas one series of length k and another of length k+1.
    //
    // There is deliberately no separate accessor for the nearest-neighbour baseline: it
    // travels inside RunStats, so the sidebar and the chart's reference line both get
    // it in the same snapshot as the best cost it is compared against.
    ConvergenceSeries getConvergenceSeries()
    {
        ConvergenceSeries s;
        lock();
        s.bestSoFar = _costHistory;
        s.iterBest = _iterBestHistory;
        unlock();
        return s;
    }

    int getNumIterations() const { return _options.numIterations; }

    // Thread-safe snapshot for the convergence chart's status line. "Converged" is a
    // display heuristic only - the search always runs the full numIterations (or until
    // Stop is pressed).
    ConvergenceInfo getConvergenceInfo()
    {
        lock();
        ConvergenceInfo info;
        info.bestIteration = _bestFoundAtIteration;
        info.currentIteration = _iteration;
        info.patience = cConvergencePatienceIterations;
        info.converged = _tourFound && (info.currentIteration - info.bestIteration) >= info.patience;
        unlock();
        return info;
    }

    // One consistent snapshot of everything the ViewStats sidebar shows.
    //
    // The atomics are read outside the lock (individually safe by construction), while
    // _towns.size(), _bestLength and _tourFound are read inside it - _bestLength in
    // particular is a plain float written by the worker under this same lock.
    RunStats getRunStats()
    {
        RunStats s;
        s.numIterations = _options.numIterations;
        s.numAnts = _options.numAnts;
        s.seed = _options.seed;
        s.iteration = _iteration;
        s.bestIteration = _bestFoundAtIteration;
        s.searching = _searching;

        lock();
        s.numCities = (int)_towns.size();
        s.bestLength = _bestLength;
        s.tourFound = _tourFound;
        s.nnLength = _nnLength;
        s.nnValid = _nnValid;
        unlock();

        s.converged = s.tourFound &&
            (s.iteration - s.bestIteration) >= cConvergencePatienceIterations;

        // Percentage by which the colony's tour is shorter than the greedy one.
        // Positive means ACO won. Left signed rather than clamped: a run cut short by
        // Stop, or one given too few ants or iterations, genuinely can fail to beat the
        // baseline, and hiding that would be dishonest.
        if (s.nnValid && s.tourFound && s.nnLength > 1e-6f)
            s.improvementPct = 100.0f * (s.nnLength - s.bestLength) / s.nnLength;

        return s;
    }

    // Everything needed to export and reproduce this run, in one lock block.
    //
    // Called only from the UI thread and only while no run is in progress, but it still
    // takes the lock, because the histories and the best tour are mutex-guarded members.
    //
    // Returns by value on purpose: the caller does file I/O with the result, which must
    // not happen with Model's mutex held.
    //
    // runtimeSeconds is the ONE field left for the caller to fill - Model does not own
    // the run timer, which lives in ViewMap.
    RunRecord getRunRecord()
    {
        RunRecord r;

        lock();

        size_t n = _towns.size();
        r.numCities = (int)n;
        r.cityNames.reserve(n);
        r.cityLat.reserve(n);
        r.cityLon.reserve(n);
        r.cityX.reserve(n);
        r.cityY.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            r.cityNames.emplace_back(_towns[i].getName().c_str());

            const auto& loc = _towns[i].getLocation();
            r.cityX.push_back(loc.x);
            r.cityY.push_back(loc.y);

            // Guarded rather than assumed: _townLatLong is filled in lockstep with
            // _towns, but indexing past the end is a far worse outcome than a zero.
            if (i < _townLatLong.size())
            {
                r.cityLat.push_back(_townLatLong[i].first);
                r.cityLon.push_back(_townLatLong[i].second);
            }
            else
            {
                r.cityLat.push_back(0);
                r.cityLon.push_back(0);
            }
        }

        // The natID dense distance matrix, flattened row-major. Dimensions come from the
        // matrix itself rather than from _towns.size(), so a mismatch produces an empty
        // block in the file instead of reading out of bounds.
        if (n > 0 && _dist.getNoOfRows() == (td::UINT4)n && _dist.getNoOfCols() == (td::UINT4)n)
        {
            r.dist.resize(n * n);
            auto d = _dist.getManipulator();
            for (size_t i = 0; i < n; ++i)
                for (size_t j = 0; j < n; ++j)
                    r.dist[i * n + j] = d(i, j);
        }

        r.nnLength = _nnLength;
        r.nnValid = _nnValid;
        r.startMarkerID = _options.startID;
        r.cityDrawIndex = (int)_cityDrawIndex;

        r.numAnts = _options.numAnts;
        r.numIterations = _options.numIterations;
        r.seed = _options.seed;
        r.alpha = _options.alpha;
        r.beta = _options.beta;
        r.evaporationRate = _options.evaporationRate;
        r.Q = _options.Q;
        r.initialPheromone = _options.initialPheromone;

        r.tourFound = _tourFound.load();
        r.bestLength = r.tourFound ? _bestLength : 0.0f;
        r.bestIteration = _bestFoundAtIteration.load();
        r.iterationsRun = _iteration.load();
        r.bestTour = _bestTour;

        // Recorded by the worker at the moment it left the iteration loop, rather than
        // inferred from iterationsRun.
        r.completed = _ranToCompletion.load();

        r.bestSoFar = _costHistory;
        r.iterBest = _iterBestHistory;
        r.pheromoneL1 = _pheromoneNormHistory;

        unlock();

        return r;
    }

    // Every town-pair's pheromone level as of the worker's last published snapshot,
    // with endpoints already scaled to the current view size. Empty until a run has
    // published one.
    //
    // Threading: EVERYTHING read out of the Model - the pheromone values, the town
    // count, the town positions and the view size - is copied inside one lock block,
    // and the O(n^2) edge list is assembled afterwards from those local copies. In
    // particular the "_towns.size() != n" consistency check has to happen inside the
    // lock it is meant to protect, or it reads as sound while validating nothing.
    std::vector<PheromoneEdge> getPheromoneEdges()
    {
        std::vector<double> snap;
        std::vector<gui::Point> pts;   // town centres, already scaled to view size
        size_t n = 0;

        // --- critical section: copy every input this function needs ---
        lock();
        size_t snapN = _pheromoneSnapshotN;
        if (snapN >= 2 &&
            _pheromoneSnapshot.size() == snapN * snapN &&
            _towns.size() == snapN)
        {
            n = snapN;
            snap = _pheromoneSnapshot;

            pts.reserve(n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto& loc = _towns[i].getLocation();
                pts.push_back(gui::Point(loc.x * _options.viewSize.width,
                    loc.y * _options.viewSize.height));
            }
        }
        // n stays 0 when no run has published yet, or when the town list has been
        // reloaded since the snapshot was taken and the two no longer agree.
        unlock();
        // --- end critical section ---

        std::vector<PheromoneEdge> edges;
        if (n < 2)
            return edges;

        edges.reserve(n * (n - 1) / 2);
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = i + 1; j < n; ++j)
                edges.push_back(PheromoneEdge{ pts[i], pts[j], snap[i * n + j] });
        }
        return edges;
    }

    // Called by ViewMap::loadModel() after parsing the raw XML data. Randomly selects
    // cMinCities..cMaxCities towns (project proposal: "15-25 randomly generated
    // cities"), then positions them. No road network is loaded - ACO treats every pair
    // of selected cities as directly connected.
    void loadTowns(std::vector<RawTownData> allTowns,
        float ratioLat, float ratioLong, float scale, const gui::Size& sz)
    {
        if (allTowns.size() < 2)
            return;

        // City-selection stream. Even stream indices belong to this draw (see
        // makeRNG()); the counter advances on every call so each successive draw uses a
        // fresh stream. Hard-coding stream 0 would mean that with a non-zero seed every
        // press of "New cities" reshuffled with the identical engine and produced the
        // EXACT SAME city set. Advancing keeps both properties that matter: successive
        // presses differ, and relaunching with the same seed replays the same sequence.
        _cityDrawIndex = _cityDrawCount;
        std::mt19937 rng = makeRNG(_options.seed, _cityDrawCount * 2);
        ++_cityDrawCount;

        std::shuffle(allTowns.begin(), allTowns.end(), rng);

        size_t maxAvailable = allTowns.size();
        size_t lo = math::Min((size_t)cMinCities, maxAvailable);
        size_t hi = math::Min((size_t)cMaxCities, maxAvailable);
        size_t count = lo;
        if (hi > lo)
        {
            std::uniform_int_distribution<size_t> pickCount(lo, hi);
            count = pickCount(rng);
        }
        allTowns.resize(count);

        double xMin = allTowns[0].lon;
        double xMax = xMin;
        double yMin = allTowns[0].lat;
        double yMax = yMin;
        for (const auto& rt : allTowns)
        {
            xMin = math::Min(rt.lon, xMin);
            xMax = math::Max(rt.lon, xMax);
            yMin = math::Min(rt.lat, yMin);
            yMax = math::Max(rt.lat, yMax);
        }
        xMin *= scale;
        xMax *= scale;
        yMin *= scale;
        yMax *= scale;

        // Pad the bounding box by a fraction of its own span, so nothing drawn at a city
        // is clipped by the canvas edge. Screen y is flipped relative to latitude below
        // ("yMax - y"), so yMax is the TOP of the display - which is where the
        // start-town flag needs room.
        double spanX = xMax - xMin;
        double spanY = yMax - yMin;

        double marginX = (spanX > 1e-9) ? spanX * cMarginFractionX : cMarginDegenerateFallback;
        double marginTop = (spanY > 1e-9) ? spanY * cMarginFractionTop : cMarginDegenerateFallback;
        double marginBottom = (spanY > 1e-9) ? spanY * cMarginFractionBottom : cMarginDegenerateFallback;

        xMin -= marginX;
        xMax += marginX;
        yMin -= marginBottom;
        yMax += marginTop;

        double dx = xMax - xMin;
        double dy = yMax - yMin;
        if (dx == 0 || dy == 0)
            return;

        setDistanceRatios(ratioLat * dy, ratioLong * dx);
        init((unsigned int)allTowns.size());

        for (const auto& rt : allTowns)
        {
            double x = rt.lon * scale;
            x = (x - xMin) / dx;

            double y = rt.lat * scale;
            y = (yMax - y) / dy;

            gui::Point location(x, y);
            Town t(rt.name, location, sz);
            append(t);

            // Parallel to _towns, in the same order, so the exported instance carries
            // real coordinates.
            _townLatLong.emplace_back(rt.lat, rt.lon);
        }

        // Distances depend only on the towns' normalized coordinates and the ratios set
        // just above, none of which change again until the next load - so the matrix is
        // built once, here, rather than at the start of every run. That also makes it
        // available before Start, which is what lets the greedy baseline show in the
        // sidebar as soon as the cities appear.
        //
        // runACO() still rebuilds it defensively on the worker thread; that is cheap
        // (O(n^2), n <= 25) and means a future path reaching a run without coming
        // through here cannot operate on a stale matrix.
        buildDistanceMatrix();

        float nn = computeNearestNeighbourTour();
        lock();
        _nnLength = nn;
        _nnValid = (nn > 0.0f);
        unlock();
    }
};