//
//  RunExport.h
//  Writes one completed run out as CSV, so the convergence behaviour the app shows live
//  can also be analysed offline - what the project proposal means by "analyze
//  convergence behavior across iterations". Without this the curve is visible but not
//  examinable: you cannot compare two parameter settings, average over seeds, or put a
//  plot in a report.
//
//  Two files per export:
//
//    aco_<stamp>_convergence.csv   parameters as '#' comment lines, then one row per
//                                  iteration: best-so-far, iteration-best, and the
//                                  pheromone matrix L1 norm. The analysis file - loads
//                                  directly into Excel, or into pandas with
//                                  read_csv(path, comment='#').
//
//    aco_<stamp>_instance.csv      the problem itself: city coordinates, the best tour
//                                  with per-leg distances, and the full natID dense
//                                  distance matrix. Lets a third party verify the
//                                  reported tour length or re-run the instance
//                                  elsewhere.
//
//  Two files rather than one because the convergence file has to stay a single clean
//  rectangular table or it stops being loadable by the tools anyone would use to
//  analyse it; the instance file is reference material and can hold three
//  differently-shaped blocks.
//
//  Reproducibility: two numbers are needed to pin a run down, not one - the seed AND
//  the city_draw_index, because loadTowns() seeds the city shuffle with
//  makeRNG(seed, cityDrawIndex * 2). Both are written to the header, and a
//  random-seeded run is labelled as not reproducible rather than given a misleading 0.
//  Independently of that, the instance file carries the full city list and distance
//  matrix, so the problem can always be reconstructed even when the run cannot be
//  replayed.
//
//  This header owns ALL of the project's file I/O. MapModel.h deliberately does not
//  include <fstream>/<filesystem>; it hands out a RunRecord and knows nothing about
//  where it goes.
//
#pragma once
#include "MapModel.h"   // natID headers first, std headers below - the include order
                        // the rest of this project already compiles cleanly with
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <locale>
#include <filesystem>
#include <system_error>
#include <ctime>
#include <cstdio>

namespace aco_export
{
    // Compact stamp for filenames: 20260902_143301
    inline std::string timestampCompact()
    {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return std::string(buf);
    }

    // Human-readable stamp for the file header: 2026-09-02 14:33:01
    inline std::string timestampReadable()
    {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return std::string(buf);
    }

    // Quotes a field and escapes embedded quotes, per RFC 4180. City names are the only
    // free text written, and none of the Bosnian ones contain a comma today - but a
    // format that silently corrupts itself the first time one does is not worth the two
    // lines saved.
    inline std::string csvQuote(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('"');
        for (char c : s)
        {
            if (c == '"')
                out.push_back('"'); // doubled, per RFC 4180
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    // Opens a stream imbued with the classic locale.
    //
    // This matters more than it looks: a GUI toolkit is entitled to call setlocale()
    // during start-up, and under a locale such as bs_BA or de_DE the decimal point
    // becomes a COMMA - which in a comma-separated file does not merely look odd, it
    // silently shifts every subsequent column.
    inline bool openCsv(std::ofstream& ofs, const std::filesystem::path& p)
    {
        ofs.open(p);
        if (!ofs.is_open())
            return false;
        ofs.imbue(std::locale::classic());

        // UTF-8 BOM. City names in this project (Kotor Varoš, Teslić, Goražde...)
        // contain non-ASCII characters; without a BOM Excel guesses Windows-1250 and
        // renders them as mojibake.
        ofs << "\xEF\xBB\xBF";

        ofs << std::fixed;
        return true;
    }

    // Chooses the output folder: Desktop, falling back to the user's home directory if
    // Desktop doesn't exist. Never fails outright - the caller finds out about a genuine
    // problem when the file open fails, which is a better error to report than "could
    // not locate Desktop".
    inline std::filesystem::path outputDir()
    {
        namespace fs = std::filesystem;
        std::error_code ec;

        // Try Desktop first
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif

        if (home)
        {
            fs::path desktop = fs::path(home) / "Desktop";

            // Both conditions are required. "exists(desktop, ec) || !ec" would be a bug:
            // a path that simply does not exist is not an ERROR, so exists() returns
            // false while ec stays clear, "!ec" is true, and the non-existent Desktop is
            // returned anyway - which matters on Windows, where OneDrive folder
            // redirection routinely leaves the user Desktop folder absent.
            if (fs::exists(desktop, ec) && !ec)
                return desktop;

            ec.clear(); // do not carry a stale error into the caller's next call
        }

        // Fallback: home directory itself
        if (home)
            return fs::path(home);

        // Last resort: current directory
        return fs::path(".");
    }

    // Writes the '#' comment header shared by both files, in key,value form so it
    // survives being grepped.
    inline void writeCommonHeader(std::ofstream& o, const RunRecord& r, const char* title)
    {
        o << "# " << title << "\n";
        o << "# generated," << timestampReadable() << "\n";
        o << std::setprecision(0);
        o << "# cities," << r.numCities << "\n";
        o << "# ants," << r.numAnts << "\n";
        o << "# iterations_requested," << r.numIterations << "\n";
        o << "# iterations_run," << r.iterationsRun << "\n";
        o << "# completed," << (r.completed ? 1 : 0) << "\n";

        if (r.seed == cSeedRandom)
            o << "# seed,random   (run is NOT reproducible)\n";
        else
            o << "# seed," << r.seed << "\n";

        // The seed alone does not identify the city set - see RunRecord::cityDrawIndex.
        o << "# city_draw_index," << r.cityDrawIndex << "\n";

        o << std::setprecision(4);
        o << "# alpha," << r.alpha << "\n";
        o << "# beta," << r.beta << "\n";
        o << "# evaporation_rho," << r.evaporationRate << "\n";
        o << "# Q," << r.Q << "\n";
        o << "# initial_pheromone," << r.initialPheromone << "\n";

        o << std::setprecision(3);
        if (r.nnValid)
            o << "# nn_baseline_km," << r.nnLength << "\n";
        else
            o << "# nn_baseline_km,\n";

        if (r.tourFound)
            o << "# best_cost_km," << r.bestLength << "\n";
        else
            o << "# best_cost_km,\n";

        o << "# best_found_at_iter," << r.bestIteration << "\n";

        if (r.nnValid && r.tourFound && r.nnLength > 0)
        {
            double gain = 100.0 * (double(r.nnLength) - double(r.bestLength)) / double(r.nnLength);
            o << "# improvement_vs_nn_pct," << gain << "\n";
        }
        else
            o << "# improvement_vs_nn_pct,\n";

        o << "# runtime_s," << r.runtimeSeconds << "\n";
    }

    // ---- file 1: the analysis table ---------------------------------------------
    inline bool writeConvergenceCsv(const RunRecord& r, const std::filesystem::path& p)
    {
        std::ofstream o;
        if (!openCsv(o, p))
            return false;

        writeCommonHeader(o, r, "ACO / TSP - convergence series");
        o << "#\n";
        o << "# best_so_far_km   cost of the best tour found in iterations 1..iter\n";
        o << "# iter_best_km     cost of the best tour built during this iteration alone\n";
        o << "# pheromone_l1     L1 (max column-sum) norm of the pheromone matrix after\n";
        o << "#                  the update - trail concentration on the most reinforced city\n";
        o << "#\n";
        o << "iter,best_so_far_km,iter_best_km,pheromone_l1\n";

        // All three series are appended together under one lock in Model::runACO(), so
        // they are the same length. min() rather than assuming it, because a file that is
        // one column short is recoverable and a crash during export is not.
        size_t n = r.bestSoFar.size();
        if (r.iterBest.size() < n)   n = r.iterBest.size();
        if (r.pheromoneL1.size() < n) n = r.pheromoneL1.size();

        for (size_t i = 0; i < n; ++i)
        {
            o << (i + 1) << ',';
            o << std::setprecision(4) << r.bestSoFar[i] << ',';
            o << std::setprecision(4) << r.iterBest[i] << ',';
            o << std::setprecision(6) << r.pheromoneL1[i] << '\n';
        }

        o.close();
        return o.good() || !o.fail();
    }

    // ---- file 2: the problem instance -------------------------------------------
    inline bool writeInstanceCsv(const RunRecord& r, const std::filesystem::path& p)
    {
        std::ofstream o;
        if (!openCsv(o, p))
            return false;

        writeCommonHeader(o, r, "ACO / TSP - problem instance and best tour");
        o << "# start_marker_city_id," << r.startMarkerID << "\n";
        o << "#\n";
        o << "# Three blocks follow, separated by blank lines: cities, best tour,\n";
        o << "# distance matrix (km). Coordinates are given both as read from the map\n";
        o << "# file (degrees) and as normalized display positions in [0,1].\n";
        o << "#\n";

        // --- block 1: cities
        o << "city_id,name,lat_deg,long_deg,x_norm,y_norm\n";
        for (int i = 0; i < r.numCities; ++i)
        {
            o << (i + 1) << ','
                << csvQuote(r.cityNames[i]) << ','
                << std::setprecision(6) << r.cityLat[i] << ','
                << std::setprecision(6) << r.cityLon[i] << ','
                << std::setprecision(6) << r.cityX[i] << ','
                << std::setprecision(6) << r.cityY[i] << '\n';
        }
        o << "\n";

        // --- block 2: the best tour, with the distance of each leg
        //
        // The final row closes the cycle back to the first city, so the leg_km column
        // sums to exactly the best_cost_km in the header - making the reported cost
        // checkable with one spreadsheet formula rather than taken on trust.
        o << "tour_position,city_id,city_name,leg_km\n";
        size_t n = r.bestTour.size();
        size_t nc = (size_t)r.numCities;
        bool haveDist = (r.dist.size() == nc * nc);

        for (size_t i = 0; i < n; ++i)
        {
            GraphType from = r.bestTour[i];
            GraphType to = r.bestTour[(i + 1) % n]; // wraps: last leg returns to start

            o << (i + 1) << ',' << from << ',';
            if (from >= 1 && (size_t)from <= r.cityNames.size())
                o << csvQuote(r.cityNames[from - 1]);
            else
                o << csvQuote("?");
            o << ',';

            if (haveDist && from >= 1 && to >= 1 && (size_t)from <= nc && (size_t)to <= nc)
                o << std::setprecision(4) << r.dist[((size_t)from - 1) * nc + ((size_t)to - 1)];
            o << '\n';
        }
        o << "\n";

        // --- block 3: the natID dense distance matrix
        if (haveDist)
        {
            o << "dist_km";
            for (size_t j = 0; j < nc; ++j)
                o << ',' << (j + 1);
            o << '\n';

            for (size_t i = 0; i < nc; ++i)
            {
                o << (i + 1);
                for (size_t j = 0; j < nc; ++j)
                    o << ',' << std::setprecision(4) << r.dist[i * nc + j];
                o << '\n';
            }
        }

        o.close();
        return o.good() || !o.fail();
    }

    // Writes both files.
    //
    // On success msgOut receives the folder and the two file names, which the caller
    // shows in an alert - an export the user cannot locate is not much better than no
    // export. On failure msgOut receives the path that could not be written, since
    // "permission denied" is only actionable if you know where it was denied.
    inline bool writeRunFiles(const RunRecord& r, std::string& msgOut)
    {
        namespace fs = std::filesystem;

        std::string stamp = timestampCompact();
        fs::path dir = outputDir();
        fs::path fConv = dir / ("aco_" + stamp + "_convergence.csv");
        fs::path fInst = dir / ("aco_" + stamp + "_instance.csv");

        if (!writeConvergenceCsv(r, fConv))
        {
            msgOut = "Could not write:\n" + fConv.string();
            return false;
        }
        if (!writeInstanceCsv(r, fInst))
        {
            msgOut = "Could not write:\n" + fInst.string();
            return false;
        }

        std::ostringstream os;
        os << "Saved to:\n" << dir.string() << "\n\n"
            << fConv.filename().string() << "\n"
            << fInst.filename().string();
        msgOut = os.str();
        return true;
    }
}