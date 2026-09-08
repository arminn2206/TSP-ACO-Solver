//
//  ViewConvergence.h
//  The live convergence chart, the project proposal's "primary visualization":
//  best-so-far tour cost and per-iteration best cost against iteration number, with
//  the greedy nearest-neighbour tour as a fixed reference line.
//
//  Why two curves and a baseline
//  -----------------------------
//  The best-so-far series is monotonically non-increasing, so on its own it is always
//  a descending staircase, and with the Y axis auto-scaled to the data that staircase
//  always fills the plot height - a run that improved by 40% and one that improved by
//  0.3% look identical. Two things fix that:
//
//   1. The per-iteration best is plotted underneath it. That is the noisy series: it
//      shows the colony exploring and visibly settles onto the staircase as the trail
//      sharpens. This is the pair Dorigo & Stutzle plot, and it makes convergence
//      something you can see rather than something a status string asserts.
//   2. The nearest-neighbour tour length is drawn as a horizontal reference line and
//      folded into the Y range. It is a property of the city set rather than of the
//      run, so it anchors the scale to something external.
//
//  Design notes
//  ------------
//  - This canvas owns no ACO data. It holds a pointer to the ViewMap and pulls
//    thread-safe copies of the two series, the run stats and the convergence info once
//    per onDraw().
//  - The copies are pulled and the gui::Shape polylines built in the same onDraw()
//    call, and onDraw() only ever runs on the main UI thread, so the "gui::Shape must
//    only be built on the main thread" rule is respected without a dirty flag.
//  - Runs its own 10fps animation loop, started by MainView in lockstep with the
//    ViewMap and stopped by this canvas itself.
//  - Axes, labels and title use td::ColorID::SysText, resolved per desktop theme. The
//    three data colours must stand out against the text and each other, so they are
//    chosen from isDarkMode(); the best-so-far colour tracks options.pathColor so the
//    curve and the tour on the map read as the same thing.
//  - EVERY string drawn here comes from the translation files, including the
//    nearest-neighbour tag (convNN1 / convNN2).
//
#pragma once
#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <td/MutableString.h>
#include <vector>
#include "ViewMap.h"

class ViewConvergence : public gui::Canvas
{
protected:
    ViewMap* _pViewMap = nullptr;
    gui::Size _size;

    // One shape per drawn element. Members rather than locals so the buffers are reused
    // across frames instead of reallocated ten times a second.
    gui::Shape _bestShape;
    gui::Shape _iterShape;
    gui::Shape _nnShape;

    td::ColorID _bestColor = td::ColorID::DarkRed;  // best-so-far (matches the map tour)
    td::ColorID _iterColor = td::ColorID::Gray;     // per-iteration best
    td::ColorID _nnColor = td::ColorID::DarkBlue;   // greedy baseline

    // Left margin holds the cost labels; top margin holds the title and the legend
    // below it; bottom margin holds the iteration labels.
    static constexpr float cMarginLeft = 56;
    // Right margin holds the two-line nearest-neighbour tag (convNN1 / convNN2), sized
    // for the longer of the two supported languages: EN "neighbour 1234" rather than BA
    // "susjed 1234". A third language with a longer word would need this revisited.
    static constexpr float cMarginRight = 85;
    static constexpr float cMarginTop = 38;
    static constexpr float cMarginBottom = 24;

    static constexpr float cTitleY = 2;
    static constexpr float cLegendY = 19;

    // Fraction of the value range left empty above and below the data, so neither curve
    // is ever drawn flush against the frame.
    static constexpr float cRangePad = 0.06f;

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
    }

    // Maps one series to screen space. xDenom is the iteration count the X axis is
    // scaled against.
    //
    // series[i] is the value AFTER iteration (i+1), so it belongs at x = (i+1)/xDenom -
    // not i/(size-1), which would put the first sample on the "0" tick, a moment at
    // which no ant had yet built a tour, and leave the last sample one iteration short
    // of the right edge.
    void buildPoints(const std::vector<float>& series, int xDenom,
        float vMin, float vRange, std::vector<gui::Point>& out) const
    {
        float plotW = _size.width - cMarginLeft - cMarginRight;
        float plotH = _size.height - cMarginTop - cMarginBottom;
        float yBase = _size.height - cMarginBottom;

        out.clear();
        out.reserve(series.size());
        for (size_t i = 0; i < series.size(); ++i)
        {
            float xFrac = float(i + 1) / float(xDenom);
            float yFrac = (series[i] - vMin) / vRange;
            out.push_back(gui::Point(cMarginLeft + xFrac * plotW, yBase - yFrac * plotH));
        }
    }

    // Takes the best-so-far colour as a parameter rather than reading _bestColor,
    // because that colour is resolved per frame from the live path colour.
    void drawLegend(td::ColorID bestColor)
    {
        td::String lblBest = tr("convBest");
        td::String lblIter = tr("convIterBest");

        gui::DrawableString::draw(lblBest, gui::Point(cMarginLeft, cLegendY),
            gui::Font::ID::SystemNormal, bestColor);

        // Measured rather than a hard-coded offset, because the two labels are
        // translated and their widths differ per language.
        gui::Size szBest;
        gui::DrawableString::measure(lblBest, szBest, gui::Font::ID::SystemNormal);

        gui::DrawableString::draw(lblIter,
            gui::Point(cMarginLeft + szBest.width + 18, cLegendY),
            gui::Font::ID::SystemNormal, _iterColor);
    }

    void onDraw(const gui::Rect& rect) override
    {
        // Runs on every exit path below, so a Stop click or a run finishing naturally
        // always halts this canvas's animation loop - even if it happens before two data
        // points exist.
        auto autoStopIfMapStopped = [this]() {
            if (_pViewMap && !_pViewMap->isRunning() && isAnimating())
                stopAnimation();
            };

        if (_size.width <= 0 || _size.height <= 0)
        {
            autoStopIfMapStopped();
            return;
        }

        gui::DrawableString::draw(tr("convergenceTitle"), gui::Point(cMarginLeft, cTitleY),
            gui::Font::ID::SystemNormal, td::ColorID::SysText);

        // The best-so-far curve is drawn in the SAME colour as the tour on the map, so
        // the two read as one thing. Resolved here, every frame, rather than once in the
        // constructor: options.pathColor is live-editable through the colour picker, and
        // a cached colour would silently stop matching the map. The _bestColor member
        // survives only as the fallback before setViewMap() has been called.
        td::ColorID bestColor = _bestColor;
        if (_pViewMap)
            bestColor = _pViewMap->getOptions().pathColor;

        drawLegend(bestColor);

        // Axes, rebuilt each draw. Cheap at 10fps and keeps this canvas self-contained.
        gui::Point yPts[2] = { gui::Point(cMarginLeft, cMarginTop),
                                gui::Point(cMarginLeft, _size.height - cMarginBottom) };
        gui::Shape yAxis;
        yAxis.createLines(yPts, 2);
        yAxis.drawWire(td::ColorID::SysText, 1.0f);

        gui::Point xPts[2] = { gui::Point(cMarginLeft, _size.height - cMarginBottom),
                                gui::Point(_size.width - cMarginRight, _size.height - cMarginBottom) };
        gui::Shape xAxis;
        xAxis.createLines(xPts, 2);
        xAxis.drawWire(td::ColorID::SysText, 1.0f);

        if (!_pViewMap)
        {
            autoStopIfMapStopped(); // no-op (pointer null), but keeps the exit path uniform
            return;
        }

        int numIter = _pViewMap->getNumIterations();
        if (numIter < 1)
            numIter = 1;

        // X-axis min/max iteration labels, drawn regardless of how much data exists so
        // the axis is legible before the curves appear. The "0" tick sits on the Y axis
        // and means "before iteration 1", which is why no point is ever plotted there.
        gui::DrawableString::draw("0", gui::Point(cMarginLeft, _size.height - cMarginBottom + 4),
            gui::Font::ID::SystemNormal, td::ColorID::SysText);
        {
            td::MutableString mIterMax;
            mIterMax.reserve(16);
            mIterMax.appendFormat("%d", numIter);
            gui::Rect r(0, _size.height - cMarginBottom + 4, _size.width - cMarginRight, _size.height);
            gui::DrawableString::draw(mIterMax.getString(), r, gui::Font::ID::SystemNormal,
                td::ColorID::SysText, td::TextAlignment::Right);
        }

        // Convergence status line, top-right. Hidden until a run has actually started
        // (currentIteration == 0 right after a Reset).
        auto convInfo = _pViewMap->getConvergenceInfo();
        if (convInfo.currentIteration > 0)
        {
            td::MutableString mStatus;
            mStatus.reserve(64);
            // Format strings live in the translation files so word order and the
            // positions of the numbers can differ per language.
            //   convergedAtIter -> one %d  (iteration the best was found at)
            //   bestAtIter      -> three %d (best iter, current iter, total iters)
            if (convInfo.converged)
                mStatus.appendFormat(tr("convergedAtIter").c_str(), convInfo.bestIteration);
            else
                mStatus.appendFormat(tr("bestAtIter").c_str(),
                    convInfo.bestIteration, convInfo.currentIteration, numIter);

            gui::Rect r(_size.width / 2, 0, _size.width - cMarginRight, 16);
            gui::DrawableString::draw(mStatus.getString(), r, gui::Font::ID::SystemNormal,
                td::ColorID::SysText, td::TextAlignment::Right);
        }

        ConvergenceSeries series = _pViewMap->getConvergenceSeries();
        if (series.bestSoFar.size() < 2)
        {
            autoStopIfMapStopped();
            return; // nothing meaningful to plot yet
        }

        // The greedy baseline, folded into the Y range below so the scale is anchored to
        // a property of the city set rather than floating with whatever the run produced.
        RunStats stats = _pViewMap->getRunStats();
        bool haveNN = stats.nnValid && stats.nnLength > 0.0f;

        float vMin = series.bestSoFar[0];
        float vMax = series.bestSoFar[0];
        for (float v : series.bestSoFar)
        {
            vMin = math::Min(vMin, v);
            vMax = math::Max(vMax, v);
        }
        for (float v : series.iterBest)
        {
            vMin = math::Min(vMin, v);
            vMax = math::Max(vMax, v);
        }
        if (haveNN)
        {
            vMin = math::Min(vMin, stats.nnLength);
            vMax = math::Max(vMax, stats.nnLength);
        }

        if (vMax - vMin < 1e-3f)
            vMax = vMin + 1.0f; // avoid a zero-height plot on a perfectly flat curve

        // Breathing room top and bottom, so no curve is drawn flush against the frame
        // and the baseline is never hidden under the X axis.
        float pad = (vMax - vMin) * cRangePad;
        vMin -= pad;
        vMax += pad;
        float vRange = vMax - vMin;

        // Defensive: the worker appends at most numIterations entries, but a longer
        // history would push points past the right edge of the plot.
        int xDenom = numIter;
        if (xDenom < (int)series.bestSoFar.size())
            xDenom = (int)series.bestSoFar.size();

        float plotLeft = cMarginLeft;
        float plotRight = _size.width - cMarginRight;
        float plotH = _size.height - cMarginTop - cMarginBottom;
        float yBase = _size.height - cMarginBottom;

        // --- greedy baseline, drawn first so the curves sit on top of it ---
        if (haveNN)
        {
            float yNN = yBase - ((stats.nnLength - vMin) / vRange) * plotH;
            gui::Point nnPts[2] = { gui::Point(plotLeft, yNN), gui::Point(plotRight, yNN) };
            _nnShape.createLines(nnPts, 2);
            _nnShape.drawWire(_nnColor, 1.0f);

            // Two-line tag at the right end of the line, rather than on the Y axis where
            // it would collide with the min/max cost labels. Both lines are translated,
            // and the value is formatted through convNN2 rather than appended here, so a
            // language that needs the number in a different position can move it without
            // touching this file.
            gui::DrawableString::draw(tr("convNN1"), gui::Point(plotRight + 3, yNN - 15),
                gui::Font::ID::SystemNormal, _nnColor);

            td::MutableString mNN;
            mNN.reserve(32); // a translated word plus a 5-digit cost
            mNN.appendFormat(tr("convNN2").c_str(), stats.nnLength);
            gui::DrawableString::draw(mNN.getString(), gui::Point(plotRight + 3, yNN - 5),
                gui::Font::ID::SystemNormal, _nnColor);
        }

        // --- per-iteration best (the noisy series), underneath the staircase ---
        std::vector<gui::Point> pts;
        if (series.iterBest.size() >= 2)
        {
            buildPoints(series.iterBest, xDenom, vMin, vRange, pts);
            _iterShape.createPolyLine(pts.data(), pts.size());
            _iterShape.drawWire(_iterColor, 1.0f);
        }

        // --- best-so-far, drawn last and thickest so it stays readable on top ---
        buildPoints(series.bestSoFar, xDenom, vMin, vRange, pts);
        _bestShape.createPolyLine(pts.data(), pts.size());
        _bestShape.drawWire(bestColor, 2.0f);

        // Cost labels on the Y axis. These are the padded range bounds rather than the
        // raw data extremes, so they describe the axis actually drawn.
        td::MutableString mStr;
        mStr.reserve(32);
        mStr.appendFormat("%.0f", vMax);
        gui::DrawableString::draw(mStr.getString(), gui::Point(2, cMarginTop - 6),
            gui::Font::ID::SystemNormal, td::ColorID::SysText);

        mStr.reset();
        mStr.appendFormat("%.0f", vMin);
        gui::DrawableString::draw(mStr.getString(), gui::Point(2, yBase - 8),
            gui::Font::ID::SystemNormal, td::ColorID::SysText);

        autoStopIfMapStopped();
    }

public:
    ViewConvergence()
        : Canvas()
    {
        auto pApp = gui::getApplication();
        if (pApp && pApp->isDarkMode())
        {
            // Same light/dark pair as Model::_options.pathColor, so the best-so-far
            // curve and the best tour on the map are drawn in the same colour.
            _bestColor = td::ColorID::Orange;
            _iterColor = td::ColorID::Gray;
            _nnColor = td::ColorID::Cyan;
        }
        else
        {
            _bestColor = td::ColorID::DarkRed;
            _iterColor = td::ColorID::Gray;
            _nnColor = td::ColorID::DarkBlue;
        }

        setPreferredFrameRateRange(10, 10); // a line chart doesn't need 60fps
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

    // Forces a one-off repaint. Needed after a Reset: by then the run has ended, so this
    // canvas has already self-stopped its animation loop and would otherwise keep
    // showing the previous run's curves until the next Start.
    void refresh()
    {
        reDraw();
    }
};