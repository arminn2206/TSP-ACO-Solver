//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/Canvas.h>
#include "MapModel.h"
#include <td/Timer.h>
#include <xml/DOMParser.h>
#include <vector>
#include <td/MutableString.h>
#include <cassert>

class ViewMap : public gui::Canvas
{
protected:
    Model _model;

    gui::Sound _flightSound;
    gui::Sound _cheeringSound;
    gui::Size _size;
    std::function<void()> _fnUpdateMenuAndTB;
    td::Timer<false> _fpsTimer; //measures time between last FPS display and current frame
    td::Timer<false> _solutionTimer;
    gui::CoordType _infoHeight = 0;
    td::MutableString _mStrInfo;

    double _FPS = 0;        //frame per second
    int _nFrames = 0;       //used for frame per second computation

    bool _playSound = true;

    // True once Start has been pressed, until the next Reset or city reload.
    // _solutionTimer reports a duration even when it was never started, so without this
    // flag the stats sidebar would show a runtime before any run had happened.
    bool _runStarted = false;

    // Pheromone background layer: one gui::Shape line per drawn edge, plus the width to
    // render it at (thickness encodes relative pheromone strength). Rebuilt on the main
    // thread inside onDraw(), throttled via _pheromoneTimer so the O(n^2) geometry
    // rebuild doesn't run at 60fps - drawWire() itself still runs every frame.
    struct PheromoneLine
    {
        gui::Shape shape;
        float width = 1.0f;
    };
    std::vector<PheromoneLine> _pheromoneShapes;
    td::Timer<false> _pheromoneTimer;
    bool _forcePheromoneRebuild = true; // true forces an immediate rebuild on the next onDraw()

    // ---------------------------------------------------------------------
    // Completed-run history, for the "Compare last two runs" dialog.
    //
    // Nothing in Model survives a Start: findPath() calls reset(), which wipes the best
    // tour, both convergence series and every counter before the worker is spawned.
    // This keeps the last two finished runs in memory so they can be compared without
    // leaving the application. Newest first: _runHistory[0] is the most recent.
    //
    // ONLY COMPLETED RUNS ARE STORED. A run halted with Stop is excluded on purpose:
    // its curves end early and its best cost is whatever the colony happened to have
    // reached, so a table of raw kilometres would present "fewer iterations" as a
    // quality difference. See captureCompletedRun().
    //
    // Cleared by loadModel(), because a new city set makes the stored costs
    // incomparable. NOT cleared by resetRun(): Reset keeps the same cities.
    // ---------------------------------------------------------------------
    static constexpr size_t cMaxRunHistory = 2;
    std::vector<RunRecord> _runHistory;

    // Called from threadCompleted(), on the main thread, after the worker has been
    // joined - so the model is quiescent and getRunRecord() sees a final state.
    void captureCompletedRun()
    {
        RunRecord rec = _model.getRunRecord();

        // Model does not own the run timer, so this one field is filled in here - the
        // same division of labour MainView::exportRun() uses. "completed" arrives
        // already set from Model::_ranToCompletion.
        rec.runtimeSeconds = getRuntimeSeconds();

        if (!rec.completed)
            return; // stopped early - not comparable, see the note above

        // A run can report completed==true and still have produced nothing worth
        // storing if numIterations was tiny or every ant was rejected. Same pair of
        // conditions exportRun() uses to decide there is a result at all.
        if (!rec.tourFound || rec.bestSoFar.empty())
            return;

        _runHistory.insert(_runHistory.begin(), std::move(rec));
        if (_runHistory.size() > cMaxRunHistory)
            _runHistory.resize(cMaxRunHistory);
    }

    void rebuildPheromoneShapes()
    {
        _pheromoneShapes.clear();

        auto edges = _model.getPheromoneEdges();
        if (edges.empty())
            return;

        double minV = edges[0].value;
        double maxV = edges[0].value;
        double sum = 0;
        for (const auto& e : edges)
        {
            minV = math::Min(minV, e.value);
            maxV = math::Max(maxV, e.value);
            sum += e.value;
        }
        double mean = sum / edges.size();
        double range = maxV - minV;
        if (range < 1e-9)
            range = 1.0; // flat pheromone (e.g. right at the start) - avoid a divide-by-zero

        const Primitive::Options& options = _model.getOptions();
        bool thresholdMode = (options.pheromoneMode == PheromoneDisplayMode::Threshold);
        double thresholdVal = mean * cPheromoneThresholdMultiplier;

        _pheromoneShapes.reserve(edges.size());
        for (const auto& e : edges)
        {
            if (thresholdMode && e.value < thresholdVal)
                continue;

            gui::Point pts[2] = { e.p1, e.p2 };
            float norm = float((e.value - minV) / range);
            float width = cPheromoneLineWidthMin + norm * (cPheromoneLineWidthMax - cPheromoneLineWidthMin);

            PheromoneLine line;
            line.shape.createLines(pts, 2);
            line.width = width;
            _pheromoneShapes.push_back(std::move(line));
        }
    }

protected:

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
        _model.updateModelSize(newSize);
        _forcePheromoneRebuild = true; // town positions moved - edge geometry is now stale
    }

    void onDraw(const gui::Rect& rect) override
    {
        // Pheromone background layer - drawn first so towns/tour render on top of it.
        {
            double elapsed = _pheromoneTimer.getDurationInSeconds();
            if (_forcePheromoneRebuild || elapsed > cPheromoneRebuildIntervalSec)
            {
                rebuildPheromoneShapes();
                _pheromoneTimer.start();
                _forcePheromoneRebuild = false;
            }

            const Primitive::Options& options = _model.getOptions();
            for (const auto& line : _pheromoneShapes)
                line.shape.drawWire(options.pheromoneColor, line.width);
        }

        _mStrInfo.reset();
        if (isAnimating())
        {
            double dT = _fpsTimer.getDurationInSeconds();
            if (dT > 0.25) //update FPS each 0.25s
            {
                _fpsTimer.start();
                _FPS = ++_nFrames / dT;

                _nFrames = 0;
            }
            else
            {
                ++_nFrames;
            }
        }
        else
        {
            _fpsTimer.stop();
            _nFrames = 0;
        }

        auto time = _solutionTimer.getDurationInMilliseconds();
        if (_FPS > 0.09)
            _mStrInfo.appendFormat("FPS:%.1f\nt=%.1f ms", _FPS, time);
        else
            _mStrInfo.appendFormat("FPS:-.-\nt=%.3f ms", time);

        // This overlay is just FPS + elapsed time; iteration, ants and best cost live in
        // the ViewStats sidebar.
        _model.draw();

        if (_infoHeight == 0)
        {
            const td::String& strInfo = _mStrInfo.getString();
            gui::Size infoSize;
            gui::DrawableString::measure(strInfo, infoSize, gui::Font::ID::SystemLargerBold);
            _infoHeight = infoSize.height;
        }

        const td::String& str = _mStrInfo.getString();
        if (!str.isEmpty())
        {
            auto loc = _model.getInfoLocation();
            switch (loc)
            {
            case Model::InfoLoc::TopLeft:
            {
                gui::Point pt(5, 5); //5 pixels offset from the view's bottom
                gui::DrawableString::draw(str, pt, gui::Font::ID::SystemLargerBold, td::ColorID::SysText);
            }
            break;
            case Model::InfoLoc::BottomLeft:
            {
                gui::Point pt(5, _size.height - _infoHeight - 5); //5 pixels offset from the view's bottom
                gui::DrawableString::draw(str, pt, gui::Font::ID::SystemLargerBold, td::ColorID::SysText);
            }
            break;
            case Model::InfoLoc::TopRight:
            {
                gui::Point pt(_size.width - 5, 5); //5 pixels offset from the view's bottom

                gui::Rect r(0, pt.y, pt.x, _size.height);
                gui::DrawableString::draw(str, r, gui::Font::ID::SystemLargerBold, td::ColorID::SysText, td::TextAlignment::Right);
            }
            break;
            case Model::InfoLoc::BottomRight:
            {
                gui::Point pt(_size.width - 5, _size.height - _infoHeight - 5); //5 pixels offset from the view's bottom

                gui::Rect r(0, pt.y, pt.x, _size.height);
                gui::DrawableString::draw(str, r, gui::Font::ID::SystemLargerBold, td::ColorID::SysText, td::TextAlignment::Right);
            }
            break;
            default:
                assert(false);
            }
        }
    }

public:
    ViewMap(const std::function<void()>& fnUpdateMenuAndTB)
        : Canvas() //enable keyboard events
        , _flightSound(":flight")
        , _cheeringSound(":cheering")
        , _fnUpdateMenuAndTB(fnUpdateMenuAndTB)
    {
        gui::Application* pApp = gui::getApplication();
        auto appProperties = pApp->getProperties();
        _playSound = appProperties->getValue("playSound", _playSound);

        _mStrInfo.reserve(128);
        setPreferredFrameRateRange(60, 60);
        enableResizeEvent(true); //required to obtain onResize events (virtual method in this class)
    }


    void threadCompleted()
    {
        auto& th = _model.getThread();
        if (th.joinable())
            th.join();

        stopAnimation();
        if (_flightSound.getState() != gui::Sound::State::NotPlaying)
            _flightSound.stop();

        // Order matters:
        //   1. Stop the timer first, because captureCompletedRun() reads it through
        //      getRuntimeSeconds() and a still-running timer would store a runtime that
        //      drifts depending on when the snapshot was taken.
        //   2. Capture the run BEFORE _fnUpdateMenuAndTB(), which decides whether
        //      Compare is available - refreshing the menu first would leave it
        //      unavailable until the next event even though a second comparable run had
        //      just landed.
        _solutionTimer.stop();
        captureCompletedRun();

        _fnUpdateMenuAndTB();

        if (_playSound && _model.isGoalFound())
            _cheeringSound.play(); //play once to celebrate the goal achievement
        reDraw();
    }

    // Begins a run: spawns the ACO worker thread and, ONLY IF that succeeded, puts this
    // canvas into its animation loop.
    //
    // The order matters. Animating first and ignoring findPath()'s return value would,
    // on a failure (fewer than two towns loaded), leave the application unrecoverable:
    // the canvas animates forever so isRunning() stays true; no worker exists so nothing
    // calls threadCompleted(), the only caller of stopAnimation(); Stop merely sets
    // Model::_stopRequested, which no thread reads; and MainWindow::shouldClose()
    // refuses to close while isRunning() is true.
    //
    // Unreachable today (Bosnia.xml ships 99 towns), but the failure mode is total and
    // ordering the two calls correctly costs nothing.
    //
    // No race in checking the return value before starting the animation: the completion
    // callback is delivered through gui::thread::asyncExecInMainThread(), which queues
    // onto the main thread, and we are on it - so threadCompleted() cannot run until
    // this function has returned.
    void start()
    {
        _forcePheromoneRebuild = true; // fresh run - old pheromone geometry (if any) is stale

        // Wrap member function 'stop' into a lambda or std::function
        auto callBackStop = std::make_shared<gui::thread::MainThreadFunction>([this]() {
            threadCompleted();
            });

        if (!_model.findPath(callBackStop))
        {
            // No worker was started, so nothing must be put into a state that only
            // threadCompleted() knows how to leave: no animation loop, no continuous
            // flight sound, and _runStarted stays as it was.
            _fnUpdateMenuAndTB();
            return;
        }

        if (_playSound)
            _flightSound.play(true);
        _fpsTimer.start();
        _solutionTimer.start();
        _runStarted = true; // the runtime shown in the stats sidebar is meaningful from here on
        startAnimation();

        _fnUpdateMenuAndTB();
    }

    void stop()
    {
        _model.stopSearching();
    }

    bool isRunning() const
    {
        return isAnimating();
    }
private:

public:
    bool loadModel(const td::String& fileName)
    {
        _model.clean();
        _runStarted = false; // brand new problem instance - no run has happened on it yet

        // A new city set makes every stored run incomparable: tour costs are only
        // meaningful against the instance that produced them, and the compare dialog
        // reports raw kilometres.
        _runHistory.clear();

        reDraw(); //async call - redraw will happen later

        // Braces kept purely to scope the parser and the town list. This project must
        // reparse on every call - "New cities" depends on it, since a fresh subsample is
        // drawn inside loadTowns().
        {
            xml::FileParser parser;
            if (!parser.parseFile(fileName))
                return false;

            const auto& root = parser.getRootNode();
            if (root->getName().cCompare("Map") != 0)
                return false;

            float ratioLong = 100;
            float ratioLat = 75;
            root.getAttribValue("ratioLong", ratioLong);
            root.getAttribValue("ratioLat", ratioLat);
            float scale = 1;
            root.getAttribValue("scale", scale);

            td::String infoLoc;
            root.getAttribValue("infoLoc", infoLoc);
            _model.setInfoLocPosition(infoLoc);


            auto towns = root.getChildNode("Towns");
            if (!towns)
                return false;

            //Towns - Pass 1: collect every <Town> entry's raw data (id, name, lat, long)
            //into a plain list. They are deliberately NOT added to the Model yet - the
            //Model randomly subsamples 15-25 of these per the project proposal.
            //
            //The <Roads> section is intentionally not parsed or drawn: ACO treats every
            //pair of selected cities as directly connected (a complete graph using air
            //distance), so the road network from the BFS/DFS demo does not represent the
            //problem and, once towns were subsampled, showed only a handful of
            //coincidental edges.
            std::vector<RawTownData> allTowns;

            auto town = towns.getChildNode("Town");
            if (!town)
                return false;

            while (town.isOk())
            {
                td::String nodeID;
                if (!town.getAttribValue("id", nodeID))
                {
                    ++town;
                    continue;
                }

                bool duplicate = false;
                for (const auto& existing : allTowns)
                {
                    if (existing.nodeID == nodeID)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate)
                {
                    mu::dbgLog("Node %s entered twice", nodeID.c_str());
                    ++town;
                    continue;
                }

                double x = 0;
                if (!town.getAttribValue("long", x))
                {
                    ++town;
                    continue;
                }

                double y = 0;
                if (!town.getAttribValue("lat", y))
                {
                    ++town;
                    continue;
                }

                td::String name;
                if (!town.getAttribValue("name", name))
                {
                    ++town;
                    continue;
                }

                RawTownData rt;
                rt.nodeID = nodeID;
                rt.name = name;
                rt.lon = x;
                rt.lat = y;
                allTowns.push_back(rt);

                ++town;
            }

            if (allTowns.size() < 2)
                return false;

            //Hand the parsed towns to the Model: it randomly selects 15-25 of them,
            //computes the bounding box over that subset, and positions them.
            _model.loadTowns(allTowns, ratioLat, ratioLong, scale, _size);
            _forcePheromoneRebuild = true; // brand new town set - any old pheromone geometry is meaningless
        }
        return true;
    }

    Primitive::Options& getOptions()
    {
        return _model.getOptions();
    }

    const MapTownNameToID& getMapNameToID() const
    {
        return _model.getMapNameToID();
    }

    size_t getTownCount() const
    {
        return _model.getTownCount();
    }

    void refresh()
    {
        reDraw();
    }

    // Called by MainWindow after the Settings dialog changes pheromone mode/color, so
    // the layer picks up the change on the very next draw instead of waiting up to
    // cPheromoneRebuildIntervalSec.
    void invalidatePheromoneCache()
    {
        _forcePheromoneRebuild = true;
    }

    // Pass-through used by ViewConvergence to poll the model each frame. The
    // nearest-neighbour baseline it draws arrives through getRunStats() instead, in the
    // same snapshot as the best cost it is compared against.
    ConvergenceSeries getConvergenceSeries()
    {
        return _model.getConvergenceSeries();
    }

    int getNumIterations() const
    {
        return _model.getNumIterations();
    }

    ConvergenceInfo getConvergenceInfo()
    {
        return _model.getConvergenceInfo();
    }

    // Pass-throughs used by the ViewStats sidebar to poll the run state each frame.
    RunStats getRunStats()
    {
        return _model.getRunStats();
    }

    // Full run snapshot for CSV export (see RunExport.h). Separate from getRunStats()
    // because it is expensive by comparison - it copies the city list, both history
    // series and the whole n x n distance matrix - and is called once on a menu action
    // rather than at 10fps from a canvas.
    //
    // runtimeSeconds is the only field left unset: Model does not own the run timer.
    RunRecord getRunRecord()
    {
        return _model.getRunRecord();
    }

    // ---- completed-run history (Compare dialog) ------------------------------
    //
    // Returned by const reference rather than by value: each RunRecord carries the whole
    // n x n distance matrix and both convergence series, and the compare dialog only
    // reads them. The records live as long as this ViewMap, and the dialog is opened and
    // closed from the same (main) thread that mutates the history.
    size_t getCompletedRunCount() const { return _runHistory.size(); }

    const RunRecord& getHistoryRun(size_t idx) const { return _runHistory[idx]; }

    // Wall-clock duration of the current (or most recently finished) run, in seconds.
    // Returns 0 before the first Start and after a Reset or city reload, since td::Timer
    // reports a duration even when it was never started.
    double getRuntimeSeconds()
    {
        if (!_runStarted)
            return 0.0;
        return _solutionTimer.getDurationInSeconds();
    }

    // Clears the current run's results (best tour, cost history, pheromone layer,
    // iteration counters) so the user can start over without relaunching. No-op while a
    // search is running - resetting the model out from under the worker would race with
    // it; MainWindow guards this too and informs the user.
    void resetRun()
    {
        if (isRunning())
            return;

        _model.reset();
        _runStarted = false; // stats sidebar goes back to a 0.00 s runtime
        _pheromoneShapes.clear();
        _forcePheromoneRebuild = true;
        _infoHeight = 0; // overlay text changed size - remeasure
        reDraw();
    }

    // The live "play sound while searching" flag.
    //
    // Exists so DialogSettings can be seeded from the value actually in force rather
    // than by re-reading the properties store with a default of its own, which had
    // produced a checkbox that disagreed with the application and then wrote its wrong
    // value back on OK. This flag lives here rather than in Primitive::Options because
    // it is about this view's sound players, not about how the model is drawn.
    bool isPlaySoundEnabled() const { return _playSound; }

    void playSound(bool bPlay)
    {
        if (_model.isSearching())
        {
            auto soundPlayerState = _flightSound.getState();
            if (!bPlay)
            {
                if (soundPlayerState == gui::Sound::State::ContinuousPlay)
                    _flightSound.stop();
            }
            else
            {
                if (soundPlayerState == gui::Sound::State::NotPlaying)
                    _flightSound.play(true); //continuous play
            }
        }
        _playSound = bPlay;
    }
};