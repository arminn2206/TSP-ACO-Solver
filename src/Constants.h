//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//

#pragma once
#include <td/Types.h>

constexpr td::BYTE cMenuApp = 10;
constexpr td::BYTE cMenuAnimation = 30;

constexpr float cLineWidthOnPath = 5.5f;

// ACO defaults
constexpr int   cDefaultNumAnts = 20;
constexpr int   cDefaultNumIterations = 100;
constexpr float cDefaultAlpha = 1.0f;         // pheromone influence
constexpr float cDefaultBeta = 3.0f;          // heuristic (1/distance) influence
constexpr float cDefaultEvaporationRate = 0.5f;
constexpr float cDefaultInitialPheromone = 1.0f;
constexpr float cDefaultQ = 100.0f;           // pheromone deposit constant

// Master RNG seed for the whole application.
//
// cSeedRandom (0) means "not reproducible": every engine is seeded from
// std::random_device. Any other value makes the application deterministic.
//
// The seed alone does NOT identify a run:
//   * ant decisions are pinned by the seed alone - makeRNG(seed, 1) is rebuilt at
//     the top of every runACO();
//   * the city set is pinned by the seed TOGETHER WITH the draw ordinal -
//     loadTowns() seeds its shuffle with makeRNG(seed, cityDrawCount * 2), and the
//     counter advances on every draw so "New cities" really draws new cities.
//
// So relaunching with the same seed replays the same SEQUENCE of city sets, while
// typing a new seed mid-session reseeds only the ants until "New cities" is pressed.
// This is why RunExport.h writes both seed and city_draw_index.
constexpr int cSeedRandom = 0;
constexpr int cDefaultSeed = cSeedRandom;
constexpr int cMinSeed = 0;
constexpr int cMaxSeed = 2147483647; // fits td::INT4 / the seed NumericEdit
// City subsampling (per project proposal: 15-25 randomly generated/selected cities)
constexpr int cMinCities = 15;
constexpr int cMaxCities = 25;

// Padding around the bounding box of the selected cities, as FRACTIONS OF THE BOX'S
// OWN SPAN so it behaves identically at every zoom level. Vertical padding is
// asymmetric because the start flag is drawn above a city and its name below, and
// the latitude axis is flipped on screen (yMax = top of the display).
constexpr double cMarginFractionX = 0.06;      // left/right - town names extend sideways
constexpr double cMarginFractionTop = 0.12;    // start-town flag is drawn above the dot
constexpr double cMarginFractionBottom = 0.08; // town name is drawn below the dot

// Fallback padding used only if the selected cities are collinear along an axis
// (zero span), which would otherwise produce a zero-size box and draw nothing.
constexpr double cMarginDegenerateFallback = 0.05;

// Convergence display heuristic: if the best-so-far cost hasn't improved for this
// many consecutive iterations, the run is reported as "converged".
constexpr int cConvergencePatienceIterations = 15;

// Pheromone edge visualization (ViewMap background layer).
// Threshold mode draws only edges above mean * this multiplier, so it self-adjusts
// as the colony converges instead of needing a fixed top-K count.
constexpr double cPheromoneThresholdMultiplier = 1.5;
constexpr float  cPheromoneLineWidthMin = 0.5f;
constexpr float  cPheromoneLineWidthMax = 4.0f;
// Throttle for the (O(n^2)) geometry rebuild only - drawWire() still runs every frame.
constexpr double cPheromoneRebuildIntervalSec = 0.15;