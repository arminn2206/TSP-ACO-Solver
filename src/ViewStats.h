//
//  ViewStats.h
//  The run-statistics sidebar: iteration count, best cost found, runtime, and the
//  greedy baseline the colony is measured against, as required by the project
//  proposal's "Output" section.
//
//  A gui::Canvas rendering text rather than a panel of gui::Label widgets, for
//  refresh: labels would need a gui::Timer to update while the worker runs, whereas a
//  Canvas drives itself with the same low-rate animation loop ViewConvergence already
//  uses. It also keeps the threading discipline identical - one polled, mutex-guarded
//  snapshot per frame, read on the UI thread only.
//
//  Like ViewConvergence, this owns no ACO data: it holds a pointer to ViewMap and pulls
//  a RunStats snapshot once per onDraw(). MainView starts its animation in lockstep
//  with the map, and the canvas stops its own loop once the run ends.
//
//  On the two quality rows: "best cost" on its own is unanchored, because whether it is
//  good depends entirely on which cities were drawn. The nearest-neighbour row gives it
//  a reference - a greedy tour over the same city set, computed once at load time - and
//  the improvement row is the percentage by which the colony's tour is shorter. That
//  percentage is shown SIGNED and never clamped: a run cut short with Stop, or one
//  given too few ants or iterations, genuinely can fail to beat a greedy tour, and a
//  sidebar that could only report success would be worthless as an instrument.
//
#pragma once
#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <td/MutableString.h>
#include "ViewMap.h"

class ViewStats : public gui::Canvas
{
protected:
    ViewMap* _pViewMap = nullptr;
    gui::Size _size;
    td::MutableString _mStr;

    static constexpr float cPadX = 10;
    static constexpr float cTitleY = 6;
    static constexpr float cFirstRowY = 30;
    static constexpr float cRowHeight = 20;

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
    }

    // One "Label            value" line. The label is left-aligned at x=cPadX and the
    // value right-aligned against the panel's right edge, so the numbers line up in a
    // column regardless of how long the translated labels are.
    void drawRow(float y, const td::String& label, const td::String& value)
    {
        gui::DrawableString::draw(label, gui::Point(cPadX, y),
            gui::Font::ID::SystemNormal, td::ColorID::SysText);

        gui::Rect r(0, y, _size.width - cPadX, y + cRowHeight);
        gui::DrawableString::draw(value, r, gui::Font::ID::SystemNormal,
            td::ColorID::SysText, td::TextAlignment::Right);
    }

    void onDraw(const gui::Rect& rect) override
    {
        // Runs on every exit path, so a Stop click or a run finishing naturally always
        // halts this canvas's animation loop.
        auto autoStopIfMapStopped = [this]() {
            if (_pViewMap && !_pViewMap->isRunning() && isAnimating())
                stopAnimation();
            };

        if (_size.width <= 0 || _size.height <= 0 || !_pViewMap)
        {
            autoStopIfMapStopped();
            return;
        }

        gui::DrawableString::draw(tr("statsTitle"), gui::Point(cPadX, cTitleY),
            gui::Font::ID::SystemLargerBold, td::ColorID::SysText);

        RunStats s = _pViewMap->getRunStats();
        double runtime = _pViewMap->getRuntimeSeconds();

        float y = cFirstRowY;

        // Iteration: "12 / 100"
        _mStr.reset();
        _mStr.appendFormat("%d / %d", s.iteration, s.numIterations);
        drawRow(y, tr("statsIter"), _mStr.getString());
        y += cRowHeight;

        // Best cost. Until the first ant completes a tour, bestLength is still the
        // FLT_MAX sentinel, which would print as 3.4e38 - show a dash instead.
        _mStr.reset();
        if (s.tourFound)
            _mStr.appendFormat("%.1f km", s.bestLength);
        else
            _mStr.appendFormat("--");
        drawRow(y, tr("statsBest"), _mStr.getString());
        y += cRowHeight;

        // Iteration the current best was found at - the number that says when the search
        // stopped making progress.
        _mStr.reset();
        if (s.tourFound)
            _mStr.appendFormat("%d", s.bestIteration);
        else
            _mStr.appendFormat("--");
        drawRow(y, tr("statsBestIter"), _mStr.getString());
        y += cRowHeight;

        // Greedy baseline over the same cities, directly beneath "best cost" so the two
        // are read together. Available as soon as the cities load, so it is populated
        // before Start and survives Reset.
        _mStr.reset();
        if (s.nnValid)
            _mStr.appendFormat("%.1f km", s.nnLength);
        else
            _mStr.appendFormat("--");
        drawRow(y, tr("statsNN"), _mStr.getString());
        y += cRowHeight;

        // How much shorter the colony's tour is than the greedy one. Signed: '+' means
        // ACO won, '-' means it did not, which is a real outcome for a stopped or
        // under-parameterised run and is not hidden.
        _mStr.reset();
        if (s.nnValid && s.tourFound)
            _mStr.appendFormat("%+.1f %%", s.improvementPct);
        else
            _mStr.appendFormat("--");
        drawRow(y, tr("statsGain"), _mStr.getString());
        y += cRowHeight;

        // Runtime
        _mStr.reset();
        _mStr.appendFormat("%.2f s", runtime);
        drawRow(y, tr("statsTime"), _mStr.getString());
        y += cRowHeight;

        // Problem size / parameters worth having on screen during a demo
        _mStr.reset();
        _mStr.appendFormat("%d", s.numCities);
        drawRow(y, tr("statsCities"), _mStr.getString());
        y += cRowHeight;

        _mStr.reset();
        _mStr.appendFormat("%d", s.numAnts);
        drawRow(y, tr("statsAnts"), _mStr.getString());
        y += cRowHeight;

        // Seed: 0 is the "not reproducible" sentinel, so name it rather than printing a
        // bare 0 that looks like a real seed value.
        _mStr.reset();
        if (s.seed == cSeedRandom)
            _mStr.appendFormat("%s", tr("seedRandom").c_str());
        else
            _mStr.appendFormat("%d", s.seed);
        drawRow(y, tr("statsSeed"), _mStr.getString());
        y += cRowHeight;

        // Status line. "Converged" is the same display heuristic ViewConvergence uses -
        // the search itself always runs to numIterations or until Stop.
        td::String status;
        if (s.searching)
            status = s.converged ? tr("statusConverged") : tr("statusRunning");
        else if (s.tourFound)
            status = tr("statusDone");
        else
            status = tr("statusIdle");
        drawRow(y, tr("statsStatus"), status);

        autoStopIfMapStopped();
    }

public:
    ViewStats()
        : Canvas()
    {
        _mStr.reserve(64);
        setPreferredFrameRateRange(10, 10); // a text panel doesn't need 60fps
        enableResizeEvent(true);
    }

    void setViewMap(ViewMap* pViewMap)
    {
        _pViewMap = pViewMap;
    }

    void start()
    {
        startAnimation();
    }

    // Forces a one-off repaint. Needed after Reset or New cities: by then no run is in
    // progress, so this canvas has already self-stopped its animation loop and would
    // otherwise keep showing the previous run's numbers.
    void refresh()
    {
        reDraw();
    }
};