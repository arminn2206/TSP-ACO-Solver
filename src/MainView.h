//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/NumericEdit.h>
#include <gui/CheckBox.h>
#include <gui/ColorPicker.h>
#include <gui/Button.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include <gui/Slider.h>
#include <gui/ComboBox.h>
#include <gui/Timer.h>
#include <cnt/SafeFullVector.h>   // _comboIDs

#include "ViewMap.h"
#include "ViewConvergence.h"
#include "ViewStats.h"
#include "RunExport.h"           // CSV export of a finished run (all file I/O lives there)

class MainView : public gui::View
{
private:
protected:
    using ComboIDs = struct ids
    {
        GraphType townID;
        int posInCombo;
    };

    // ---------------------------------------------------------------------
    // Guard rails for the ACO parameters typed into the UI. The values go straight from
    // the NumericEdits into Primitive::Options and from there into the worker thread,
    // with no further checking. Two concrete failures without them:
    //
    //  * numAnts <= 0 reaches
    //        std::vector<std::vector<GraphType>> antTours(_options.numAnts);
    //    in Model::runACO(). A negative value converts to an enormous size_t and throws
    //    std::length_error on the worker thread, where nothing catches it, so the
    //    process calls std::terminate(). Zero doesn't crash but produces a run in which
    //    no ant moves, leaving _bestLength at FLT_MAX and plotting 3.4e38.
    //
    //  * evaporationRate outside (0,1) makes "keep = 1 - rate" negative, driving the
    //    pheromone matrix negative. std::pow(negative, alpha) is then NaN, and because
    //    every comparison against NaN is false the "sum <= 0" fallback in
    //    constructTour() never fires - instead a NaN upper bound reaches
    //    std::uniform_real_distribution, which is UB.
    //
    // Values outside the range are clamped AND written back into the widget, so the user
    // can see what the run is actually going to use.
    // ---------------------------------------------------------------------
    static constexpr int   cMinNumAnts = 1;
    static constexpr int   cMaxNumAnts = 500;
    static constexpr int   cMinNumIterations = 1;
    static constexpr int   cMaxNumIterations = 100000;
    static constexpr float cMinAlpha = 0.0f;
    static constexpr float cMaxAlpha = 10.0f;
    static constexpr float cMinBeta = 0.0f;
    static constexpr float cMaxBeta = 10.0f;
    static constexpr float cMinEvaporation = 0.01f;
    static constexpr float cMaxEvaporation = 0.99f;

    static int clampInt(int v, int lo, int hi)
    {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    // Written as "!(v >= lo)" rather than "v < lo" on purpose: that form is also true
    // for NaN, so a NaN from a malformed field is clamped to lo instead of reaching the
    // algorithm.
    static float clampFloat(float v, float lo, float hi)
    {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    }

    gui::Label _lblNumAnts;
    gui::NumericEdit _neNumAnts;
    gui::Label _lblNumIterations;
    gui::NumericEdit _neNumIterations;
    gui::Label _lblAlpha;
    gui::NumericEdit _neAlpha;
    gui::Label _lblBeta;
    gui::NumericEdit _neBeta;
    gui::Label _lblEvaporation;
    gui::NumericEdit _neEvaporation;

    gui::Label _lblSpeed;
    gui::Slider _slSpeed;
    gui::Label _lblPathColor;
    gui::ColorPicker _pathColor;

    // Start-marker combo, labelled tr("lblStart") = "Start marker:" rather than "Start
    // town": since ants are launched from randomly chosen cities (Model::runACO), this
    // selects only which city gets the flag icon and which city the stored best tour is
    // rotated to begin at. Kept because it fixes the origin of the tour ordering, which
    // matters as soon as a tour is written out rather than only drawn.
    gui::Label _lblStart;
    gui::ComboBox _cmbStart;

    // Reproducibility seed. 0 = random (default); any other value makes the whole
    // application deterministic - see cSeedRandom in Constants.h and Model::makeRNG().
    gui::Label _lblSeed;
    gui::NumericEdit _neSeed;

    cnt::SafeFullVector<ComboIDs> _comboIDs;

    ViewMap _animation;
    ViewConvergence _convergence;
    ViewStats _stats;
    gui::GridLayout _gl;
    const int _maxDelay = 2000;

    // ---------------------------------------------------------------------
    // Re-entrancy guard for populateCombos().
    //
    // populateCombos() empties _comboIDs and then _cmbStart. Clearing and refilling the
    // combo box makes the toolkit emit selection-changed signals, which invoke the
    // _cmbStart.onChangedSelection handler - and that handler indexes _comboIDs, which
    // at that moment is empty or only partly rebuilt. cnt::SafeFullVector bounds-checks
    // with an assert, so the debug build would abort.
    //
    // While this flag is set the handler returns immediately: populateCombos() computes
    // and assigns the correct startID itself, so there is nothing for the handler to
    // contribute during a rebuild.
    // ---------------------------------------------------------------------
    bool _updatingCombos = false;
protected:

    bool onKeyPressed(const gui::Key& key) override
    {
        if (key.getType() == gui::Key::Type::ASCII)
        {
            char ch = key.getChar();
            if (ch == ' ')
            {
                startStop();
                return true;
            }
        }
        return false;
    }

    void setIntWidget(gui::NumericEdit& ne, int value)
    {
        td::Variant v(td::int4);
        v.setValue(td::INT4(value));
        ne.setValue(v);
    }

    void setFloatWidget(gui::NumericEdit& ne, float value)
    {
        td::Variant v(td::real4);
        v.setValue(value);
        ne.setValue(v);
    }

    // Separate from readACOParamsFromUI() because the seed is needed on a second path:
    // newCities() reseeds the city selection without a run ever starting, so it cannot
    // rely on Start having read the field first. Returns the clamped value put into
    // effect.
    int readSeedFromUI()
    {
        td::Variant v = _neSeed.getValue();
        td::INT4 val = 0;
        v.getValue(val);
        int raw = int(val);
        int clamped = clampInt(raw, cMinSeed, cMaxSeed);
        if (clamped != raw)
            setIntWidget(_neSeed, clamped);

        Primitive::Options& options = _animation.getOptions();
        options.seed = clamped;
        auto pApp = gui::getApplication();
        auto appProperties = pApp->getProperties();
        appProperties->setValue("seed", options.seed);
        return clamped;
    }

    void readACOParamsFromUI()
    {
        Primitive::Options& options = _animation.getOptions();
        auto pApp = gui::getApplication();
        auto appProperties = pApp->getProperties();

        {
            td::Variant v = _neNumAnts.getValue();
            td::INT4 val = 0;
            v.getValue(val);
            int raw = int(val);
            int clamped = clampInt(raw, cMinNumAnts, cMaxNumAnts);
            if (clamped != raw)
                setIntWidget(_neNumAnts, clamped);
            options.numAnts = clamped;
            appProperties->setValue("numAnts", options.numAnts);
        }
        {
            td::Variant v = _neNumIterations.getValue();
            td::INT4 val = 0;
            v.getValue(val);
            int raw = int(val);
            int clamped = clampInt(raw, cMinNumIterations, cMaxNumIterations);
            if (clamped != raw)
                setIntWidget(_neNumIterations, clamped);
            options.numIterations = clamped;
            appProperties->setValue("numIterations", options.numIterations);
        }
        {
            td::Variant v = _neAlpha.getValue();
            float val = 0;
            v.getValue(val);
            float clamped = clampFloat(val, cMinAlpha, cMaxAlpha);
            if (clamped != val)
                setFloatWidget(_neAlpha, clamped);
            options.alpha = clamped;
            appProperties->setValue("alpha", options.alpha);
        }
        {
            td::Variant v = _neBeta.getValue();
            float val = 0;
            v.getValue(val);
            float clamped = clampFloat(val, cMinBeta, cMaxBeta);
            if (clamped != val)
                setFloatWidget(_neBeta, clamped);
            options.beta = clamped;
            appProperties->setValue("beta", options.beta);
        }
        {
            td::Variant v = _neEvaporation.getValue();
            float val = 0;
            v.getValue(val);
            float clamped = clampFloat(val, cMinEvaporation, cMaxEvaporation);
            if (clamped != val)
                setFloatWidget(_neEvaporation, clamped);
            options.evaporationRate = clamped;
            appProperties->setValue("evaporationRate", options.evaporationRate);
        }

        readSeedFromUI();
    }

    void setupEventHandlers()
    {
        _slSpeed.onChangedValue([this]() {
            double val = _slSpeed.getValue();
            Primitive::Options& options = _animation.getOptions();
            int ms = math::Max(_maxDelay - int(val), 0);
            options.sleepMS = ms;   // std::atomic<int> - see Primitive.h
            auto pApp = gui::getApplication();
            auto appProperties = pApp->getProperties();
            appProperties->setValue("sleepMS", ms);
            });

        _pathColor.onChangedValue([this]() {
            td::ColorID colorID = _pathColor.getValue();
            Primitive::Options& options = _animation.getOptions();
            options.pathColor = colorID;
            auto pApp = gui::getApplication();
            auto appProperties = pApp->getProperties();
            appProperties->setValue("pathColor", options.pathColor);
            });

        _cmbStart.onChangedSelection([this]() {
            // Signals fired while populateCombos() is tearing down and refilling the
            // combo describe a half-built state - ignore them. See _updatingCombos.
            if (_updatingCombos)
                return;

            int iSel = _cmbStart.getSelectedIndex();

            // Bounds check, not just "iSel >= 0": cnt::SafeFullVector asserts on an
            // out-of-range index, so a stale index would abort the debug build.
            if (iSel < 0 || iSel >= (int)_comboIDs.size())
                return;

            Primitive::Options& options = _animation.getOptions();
            options.startID = _comboIDs[iSel].townID;
            // refresh() rather than anything heavier: nothing about the search changes
            // here, only where the flag is drawn. Safe to do mid-run for that reason.
            _animation.refresh();
            auto pApp = gui::getApplication();
            auto appProperties = pApp->getProperties();
            appProperties->setValue("startID", options.startID);
            });
    }

    void populateCombos(Primitive::Options& options, mu::IAppProperties* appProperties)
    {
        // Suppress the combo's selection-changed handler for the whole rebuild: the
        // clean() calls below and the addItem()/selectIndex() calls after them all emit
        // signals while _comboIDs is empty or incomplete.
        _updatingCombos = true;

        _comboIDs.clean();
        _cmbStart.clean();
        const auto& mapTownNameToID = _animation.getMapNameToID();

        // Sized by the TOWN COUNT, not by the size of the name->id map.
        //
        // These are not the same number: the map is keyed by town name, so two towns
        // sharing a name collapse into one entry. _comboIDs is indexed BY ID at
        // "[townID - 1]" below, and ids run 1..townCount regardless of how many names
        // are distinct - so sizing it from the map would let an id larger than the map
        // size write past the end.
        //
        // Bosnia.xml has 99 towns with 99 distinct names, so this cannot fire today. It
        // is a latent trap for anyone adding a town or swapping in a different map file.
        //
        // The loop still walks the map (so the combo stays alphabetical by name) and
        // fills only as many _comboIDs slots as there are map entries; surplus slots
        // stay zeroed by zeros(), which the startID clamp below treats as invalid.
        auto nTowns = _animation.getTownCount();
        auto nPairs = mapTownNameToID.size();
        _comboIDs.reserve(nTowns > nPairs ? nTowns : nPairs);
        _comboIDs.zeros();
        int iPos = 0;
        for (const auto& pair : mapTownNameToID)
        {
            const td::String townName = pair.first;
            GraphType townID = pair.second;
            _cmbStart.addItem(townName);
            _comboIDs[townID - 1].posInCombo = iPos;
            _comboIDs[iPos++].townID = townID;
        }

        options.startID = appProperties->getValue("startID", options.startID);

        // startID is 1-based, so the valid range is 1..n inclusive. Guard 0 explicitly
        // too: startID is unsigned, so "startID - 1" below would wrap to a huge index.
        // This clamp also covers the "New cities" case, where the persisted startID may
        // exceed the freshly drawn city count.
        if (options.startID < 1 || options.startID >(GraphType)_comboIDs.size())
            options.startID = 1;

        if (_comboIDs.size() > 0)
        {
            iPos = _comboIDs[options.startID - 1].posInCombo;
            _cmbStart.selectIndex(iPos);
        }

        _updatingCombos = false;
    }

public:
    MainView(const std::function<void()>& fnUpdateMenuAndTB)
        : _lblNumAnts(tr("lblNumAnts"))
        , _neNumAnts(td::int4)
        , _lblNumIterations(tr("lblNumIterations"))
        , _neNumIterations(td::int4)
        , _lblAlpha(tr("lblAlpha"))
        , _neAlpha(td::real4, gui::LineEdit::Messages::DoNotSend, true, "", 2)
        , _lblBeta(tr("lblBeta"))
        , _neBeta(td::real4, gui::LineEdit::Messages::DoNotSend, true, "", 2)
        , _lblEvaporation(tr("lblEvaporation"))
        , _neEvaporation(td::real4, gui::LineEdit::Messages::DoNotSend, true, "", 2)
        , _lblSpeed(tr("lblSpeed"))
        , _lblPathColor(tr("pathColor"))
        , _lblStart(tr("lblStart"))
        , _lblSeed(tr("lblSeed"))
        , _neSeed(td::int4)
        , _gl(3, 20)   // 20 columns: rows 1 and 2 need them (see the insert() calls below)
        , _animation(fnUpdateMenuAndTB)
    {
        _slSpeed.setRange(0, _maxDelay);

        Primitive::Options& options = _animation.getOptions();

        auto pApp = gui::getApplication();
        auto appProperties = pApp->getProperties();

        // Values persisted from a previous session get the same clamping as values typed
        // into the UI - a bad value written to the registry once would otherwise keep
        // coming back on every launch.
        options.numAnts = clampInt(appProperties->getValue("numAnts", options.numAnts),
            cMinNumAnts, cMaxNumAnts);
        options.numIterations = clampInt(appProperties->getValue("numIterations", options.numIterations),
            cMinNumIterations, cMaxNumIterations);
        options.alpha = clampFloat(appProperties->getValue("alpha", options.alpha),
            cMinAlpha, cMaxAlpha);
        options.beta = clampFloat(appProperties->getValue("beta", options.beta),
            cMinBeta, cMaxBeta);
        options.evaporationRate = clampFloat(appProperties->getValue("evaporationRate", options.evaporationRate),
            cMinEvaporation, cMaxEvaporation);

        // Must be read BEFORE loadModel() below: loadTowns() uses options.seed to draw
        // the city subsample, so a stale default here would defeat reproducibility on
        // the first launch after a seed was persisted.
        options.seed = clampInt(appProperties->getValue("seed", options.seed),
            cMinSeed, cMaxSeed);

        {
            int ms = appProperties->getValue("sleepMS", options.sleepMS.load());
            options.sleepMS = clampInt(ms, 0, _maxDelay);
        }

        options.pathColor = appProperties->getValue("pathColor", options.pathColor);

        // Pheromone background layer settings (set/persisted via the Settings dialog)
        {
            int pmRaw = appProperties->getValue("pheromoneMode", (int)options.pheromoneMode);
            options.pheromoneMode = (PheromoneDisplayMode)pmRaw;
        }
        options.pheromoneColor = appProperties->getValue("pheromoneColor", options.pheromoneColor);

        // loadModel() already ends with populateCombos(), so there is no second call here.
        loadModel();

        _convergence.setViewMap(&_animation);
        _stats.setViewMap(&_animation);

        //put values into gui elements
        setIntWidget(_neNumAnts, options.numAnts);
        setIntWidget(_neNumIterations, options.numIterations);
        setFloatWidget(_neAlpha, options.alpha);
        setFloatWidget(_neBeta, options.beta);
        setFloatWidget(_neEvaporation, options.evaporationRate);
        setIntWidget(_neSeed, options.seed);

        _slSpeed.setValue(_maxDelay - options.sleepMS.load());
        _pathColor.setValue(options.pathColor);

        setupEventHandlers();

        gui::GridComposer gc(_gl);

        gc.startNewRowWithSpace(5, 0)
            << _lblNumAnts << _neNumAnts
            << _lblNumIterations << _neNumIterations
            << _lblAlpha << _neAlpha
            << _lblBeta << _neBeta
            << _lblEvaporation << _neEvaporation
            << _lblSpeed << _slSpeed
            << _lblPathColor << _pathColor
            << _lblStart << _cmbStart
            << _lblSeed << _neSeed;

        // Rows 1 and 2 are placed with GridLayout::insert() rather than through the
        // composer, because they need explicit column spans (the third argument).
        //
        // Row 1: map in columns 0-14, stats sidebar beside it in columns 15-19.
        // Row 2: convergence chart across all 20 columns.
        //
        // The chart spans the full width rather than stopping under the map: the sidebar
        // is a fixed column of text and gains nothing from extra width below it, while
        // with 100 iterations plotted every extra pixel of chart width makes another
        // iteration distinguishable from its neighbours. The chart's own cMarginRight
        // keeps the nearest-neighbour label clear of the frame.
        _gl.insert(1, 0, _animation, 15);
        _gl.insert(1, 15, _stats, 5);
        _gl.insert(2, 0, _convergence, 20);

        setLayout(&_gl);
    }

    bool isRunning() const { return _animation.isRunning(); }

    void startStop()
    {
        if (_animation.isRunning())
        {
            _animation.stop();
        }
        else
        {
            readACOParamsFromUI();
            _animation.start();
            _convergence.start();
            _stats.start();
        }
    }

    void setFocusToAnimation() { _animation.setFocus(); }

    // Writes the finished run out as CSV, so its convergence behaviour can be analysed
    // offline rather than only watched. See RunExport.h for the file layout.
    //
    // Return code rather than bool, because the three outcomes need three different
    // messages and lumping "nothing to export" in with "the disk refused" would be
    // misleading:
    //     1  written, msgOut holds the folder and file names
    //     0  nothing to export - no completed run in memory
    //    -1  write failed, msgOut holds the path that could not be written
    //
    // MainWindow refuses to call this while a run is in progress; the guard here is the
    // real safety net, since exporting mid-run would capture a half-finished series and
    // present it as a result.
    //
    // A STOPPED run is still exportable, unlike the compare history. That is
    // deliberate: the CSV header carries both "iterations_run" and "completed", so a
    // partial run in a file is labelled as one, whereas the compare table would put its
    // cost beside a full run's with nothing to say they differ.
    int exportRun(td::String& msgOut)
    {
        if (_animation.isRunning())
            return 0;

        RunRecord rec = _animation.getRunRecord();

        // Require an actual tour AND at least one recorded iteration. Either alone is
        // not enough: a run stopped during its first iteration can have neither, and an
        // instance that has only been loaded has a baseline but no run to describe.
        if (!rec.tourFound || rec.bestSoFar.empty())
            return 0;

        // Model does not own the run timer, so this one field is filled in here.
        // "completed" arrives already set from Model::_ranToCompletion.
        rec.runtimeSeconds = _animation.getRuntimeSeconds();

        std::string msg;
        bool ok = aco_export::writeRunFiles(rec, msg);
        msgOut = td::String(msg.c_str());
        return ok ? 1 : -1;
    }

    // ---- completed-run history, for the Compare dialog -----------------------
    //
    // Pass-throughs to ViewMap, which owns the history because that is where a run
    // actually finishes (ViewMap::threadCompleted) and where the run timer lives.
    //
    // canCompareRuns() is what the Compare action checks; MainWindow re-checks it before
    // opening the dialog. The menu item cannot be greyed out in this SDK build (no
    // gui::MenuItem::setEnabled), so that check is the enforcement rather than a hint.
    bool canCompareRuns() const
    {
        return _animation.getCompletedRunCount() >= 2;
    }

    // Exposed so the "not enough runs" alert can state how many are actually stored.
    size_t getCompletedRunCount() const
    {
        return _animation.getCompletedRunCount();
    }

    const RunRecord& getHistoryRun(size_t idx) const
    {
        return _animation.getHistoryRun(idx);
    }

    void resetRun()
    {
        _animation.resetRun();
        _convergence.refresh(); // clear the previous run's curve (canvas isn't animating now)
        _stats.refresh();       // and the previous run's numbers
    }

    // Draws a brand new problem instance: reparses the map file and re-runs the random
    // 15-25 city subsampling in Model::loadTowns(), then clears all run state. Lets the
    // solver be demonstrated on several TSP instances in one session.
    //
    // Refuses while a search is running - swapping the town list out from under the
    // worker would race with it. MainWindow guards this too and shows an alert; the
    // check here is the actual safety net.
    void newCities()
    {
        if (_animation.isRunning())
            return;

        // The seed drives the city selection (see Model::loadTowns()), so it must be
        // picked up here too, not only on Start.
        readSeedFromUI();

        // Rebuilds the town list and, inside loadModel(), the Start-town combo. The
        // persisted startID may exceed the new city count, which populateCombos() clamps.
        loadModel();

        // Ordering matters: resetRun() must come after loadModel(), so the pheromone
        // geometry, cost history, iteration counters and cached info-text height are all
        // cleared against the town set that is actually on screen now.
        resetRun();
    }

    // Loads (or reloads) the one map this project ships, Bosnia.xml, and rebuilds the
    // Start-town combo from whichever 15-25 cities were drawn this time.
    bool loadModel()
    {
        td::String modelPath = getResFileName(":map");
        if (modelPath.isEmpty())
            return false;
        bool bToRet = _animation.loadModel(modelPath);
        Primitive::Options& options = _animation.getOptions();
        auto pApp = gui::getApplication();
        auto appProperties = pApp->getProperties();
        populateCombos(options, appProperties);
        return bToRet;
    }

    ViewMap& getViewMap() { return _animation; }
};