//
//  ViewCompare.h
//  Side-by-side statistics for the two most recently COMPLETED runs, plus the
//  difference between them.
//
//  Why this exists
//  ---------------
//  The proposal's third goal is to "analyze convergence behavior across iterations".
//  The live chart shows one run converging and the CSV export lets that run be analysed
//  offline, but neither answers the comparative question the analysis is actually
//  about: does raising beta help? does a higher evaporation rate converge sooner or
//  just noisier? That needs two runs next to each other, and the application otherwise
//  destroys run N the instant run N+1 starts (Model::findPath() -> reset()).
//
//  Only completed runs are compared. ViewMap::captureCompletedRun() refuses to store a
//  run halted with Stop, because a shortened run's best cost is not worse in any
//  meaningful sense - it simply had fewer iterations - and a table of raw kilometres
//  would present that as a quality difference.
//
//  Implementation notes
//  --------------------
//  - A gui::Canvas rendering text, like ViewStats, with one addition: the content is
//    fixed for the lifetime of the dialog, so no animation loop is started and onDraw()
//    runs on demand.
//  - The two RunRecords are copied in at construction. They are plain data, so the
//    dialog cannot be affected by a run started behind it.
//  - The delta column is only filled where subtraction means something. Alpha, seed and
//    ant count get a dash: "beta went from 1 to 6" is the setting, not a result, and
//    printing "+5.0" under a Results heading invites it to be read as an outcome.
//
#pragma once
#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <td/MutableString.h>
// Deliberately "ViewMap.h", not "MapModel.h" directly - the convention ViewStats.h and
// ViewConvergence.h already follow. MapModel.h pulls in <random>/<algorithm>, and
// including those in a translation unit that hasn't already picked up ViewMap.h's own
// includes causes an "incomplete type mem::Buffer::TList" cascade. RunRecord, all this
// file needs, is declared in MapModel.h, which ViewMap.h already includes.
#include "ViewMap.h"

class ViewCompare : public gui::Canvas
{
protected:
    RunRecord _newer;
    RunRecord _older;
    gui::Size _size;
    td::MutableString _mStr;

    static constexpr float cPadX = 14;
    static constexpr float cTitleY = 8;
    static constexpr float cHeaderY = 40;
    static constexpr float cFirstRowY = 68;
    static constexpr float cRowHeight = 24;
    static constexpr float cLabelWidth = 210;
    static constexpr float cGroupGap = 10;   // extra space above a group heading

    // ---- derived quantities -------------------------------------------------
    //
    // All computed from the stored RunRecord rather than re-read from the Model, which
    // has long since been reset by the time this dialog opens.

    static float improvementPct(const RunRecord& r)
    {
        if (!r.nnValid || !r.tourFound || r.nnLength <= 1e-6f)
            return 0.0f;
        return 100.0f * (r.nnLength - r.bestLength) / r.nnLength;
    }

    // Cost of the best tour built during the FINAL iteration. Read against the
    // best-so-far value beside it, this says whether the colony was still finding
    // competitive tours at the end or had collapsed onto one path.
    static float finalIterBest(const RunRecord& r)
    {
        return r.iterBest.empty() ? 0.0f : r.iterBest.back();
    }

    // Mean of the per-iteration best over the whole run. A crude but honest spread
    // measure: the further it sits above the best-so-far, the more the colony was still
    // exploring rather than re-walking its incumbent.
    static float meanIterBest(const RunRecord& r)
    {
        if (r.iterBest.empty())
            return 0.0f;
        double sum = 0;
        for (float v : r.iterBest)
            sum += v;
        return float(sum / double(r.iterBest.size()));
    }

    // Same display heuristic as ViewStats/ViewConvergence.
    static bool didConverge(const RunRecord& r)
    {
        return r.tourFound && r.bestIteration > 0 &&
            (r.iterationsRun - r.bestIteration) >= cConvergencePatienceIterations;
    }

    // ---- drawing helpers ----------------------------------------------------

    float panelWidth() const
    {
        return (_size.width > 0) ? float(_size.width) : 700.0f;
    }

    // Three equal value columns to the right of the label column.
    float columnWidth() const
    {
        float avail = panelWidth() - cPadX * 2 - cLabelWidth;
        if (avail < 90.0f)
            avail = 90.0f;
        return avail / 3.0f;
    }

    void drawCell(int column, float y, const td::String& text, td::ColorID color,
        gui::Font::ID font = gui::Font::ID::SystemNormal)
    {
        float w = columnWidth();
        float left = cPadX + cLabelWidth + float(column) * w;
        gui::Rect r(left, y, left + w, y + cRowHeight);
        gui::DrawableString::draw(text, r, font, color, td::TextAlignment::Right);
    }

    void drawLabel(float y, const td::String& text, td::ColorID color,
        gui::Font::ID font = gui::Font::ID::SystemNormal)
    {
        gui::DrawableString::draw(text, gui::Point(cPadX, y), font, color);
    }

    // One "label | newer | older | delta" line. Any of the three value strings may be
    // empty, in which case that cell is left blank.
    void drawRow(float& y, const td::String& label,
        const td::String& vNewer, const td::String& vOlder, const td::String& vDelta)
    {
        drawLabel(y, label, td::ColorID::SysText);
        drawCell(0, y, vNewer, td::ColorID::SysText);
        drawCell(1, y, vOlder, td::ColorID::SysText);
        drawCell(2, y, vDelta, td::ColorID::SysText);
        y += cRowHeight;
    }

    // Convenience wrappers that format into the shared MutableString. Each returns a
    // td::String by value because all three cells of a row have to exist at once and a
    // single reused buffer cannot hold three values simultaneously.
    td::String fmtInt(int v)
    {
        _mStr.reset();
        _mStr.appendFormat("%d", v);
        return _mStr.getString();
    }

    td::String fmtKm(float v)
    {
        _mStr.reset();
        _mStr.appendFormat("%.1f km", v);
        return _mStr.getString();
    }

    td::String fmtKmSigned(float v)
    {
        _mStr.reset();
        _mStr.appendFormat("%+.1f km", v);
        return _mStr.getString();
    }

    td::String fmtReal(float v, int decimals)
    {
        _mStr.reset();
        if (decimals == 1)
            _mStr.appendFormat("%.1f", v);
        else
            _mStr.appendFormat("%.2f", v);
        return _mStr.getString();
    }

    td::String fmtPct(float v)
    {
        _mStr.reset();
        _mStr.appendFormat("%+.1f %%", v);
        return _mStr.getString();
    }

    td::String fmtSeconds(float v)
    {
        _mStr.reset();
        _mStr.appendFormat("%.2f s", v);
        return _mStr.getString();
    }

    td::String fmtSecondsSigned(float v)
    {
        _mStr.reset();
        _mStr.appendFormat("%+.2f s", v);
        return _mStr.getString();
    }

    td::String fmtIntSigned(int v)
    {
        _mStr.reset();
        _mStr.appendFormat("%+d", v);
        return _mStr.getString();
    }

    td::String fmtSeed(int seed)
    {
        if (seed == cSeedRandom)
            return tr("seedRandom");
        _mStr.reset();
        _mStr.appendFormat("%d", seed);
        return _mStr.getString();
    }

    td::String dash()
    {
        _mStr.reset();
        _mStr.appendFormat("--");
        return _mStr.getString();
    }

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
    }

    void onDraw(const gui::Rect& rect) override
    {
        // No "if (_size.width <= 0) return;" guard here, unlike ViewStats and
        // ViewConvergence: those animate, so skipping a frame that arrives before
        // onResize costs nothing. This canvas draws once, on demand, so an early return
        // could leave the dialog permanently blank. panelWidth() falls back to a
        // sensible default instead, and the layout self-corrects on the first resize.
        drawLabel(cTitleY, tr("compareTitle"), td::ColorID::SysText,
            gui::Font::ID::SystemLargerBold);

        // Column headings. The newer run is on the left because it is the one the user
        // just watched finish.
        drawLabel(cHeaderY, tr("cmpMetric"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        drawCell(0, cHeaderY, tr("cmpNewer"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        drawCell(1, cHeaderY, tr("cmpOlder"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        drawCell(2, cHeaderY, tr("cmpDelta"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);

        float y = cFirstRowY;

        // ---- the problem -----------------------------------------------------
        drawLabel(y, tr("cmpGrpProblem"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        y += cRowHeight;

        drawRow(y, tr("statsCities"), fmtInt(_newer.numCities), fmtInt(_older.numCities), dash());
        drawRow(y, tr("statsNN"),
            _newer.nnValid ? fmtKm(_newer.nnLength) : dash(),
            _older.nnValid ? fmtKm(_older.nnLength) : dash(),
            dash());

        y += cGroupGap;

        // ---- the parameters ---------------------------------------------------
        //
        // No delta column here on purpose: these are the inputs the user changed, and a
        // signed difference under a column the eye reads as "result" is misleading.
        drawLabel(y, tr("cmpGrpParams"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        y += cRowHeight;

        drawRow(y, tr("statsAnts"), fmtInt(_newer.numAnts), fmtInt(_older.numAnts), dash());
        drawRow(y, tr("lblNumIterations"), fmtInt(_newer.numIterations), fmtInt(_older.numIterations), dash());
        drawRow(y, tr("lblAlpha"), fmtReal(_newer.alpha, 2), fmtReal(_older.alpha, 2), dash());
        drawRow(y, tr("lblBeta"), fmtReal(_newer.beta, 2), fmtReal(_older.beta, 2), dash());
        drawRow(y, tr("lblEvaporation"), fmtReal(_newer.evaporationRate, 2), fmtReal(_older.evaporationRate, 2), dash());
        drawRow(y, tr("statsSeed"), fmtSeed(_newer.seed), fmtSeed(_older.seed), dash());

        y += cGroupGap;

        // ---- the result -------------------------------------------------------
        drawLabel(y, tr("cmpGrpResult"), td::ColorID::SysText, gui::Font::ID::SystemLargerBold);
        y += cRowHeight;

        // Best cost. The delta is (newer - older), so a NEGATIVE number means the newer
        // run found a shorter tour - the opposite of the usual "bigger is better"
        // reading, which is why the improvement row below it exists.
        drawRow(y, tr("statsBest"),
            fmtKm(_newer.bestLength), fmtKm(_older.bestLength),
            fmtKmSigned(_newer.bestLength - _older.bestLength));

        // ...and the same thing stated so that positive is unambiguously better.
        drawRow(y, tr("statsGain"),
            fmtPct(improvementPct(_newer)), fmtPct(improvementPct(_older)),
            fmtPct(improvementPct(_newer) - improvementPct(_older)));

        drawRow(y, tr("statsBestIter"),
            fmtInt(_newer.bestIteration), fmtInt(_older.bestIteration),
            fmtIntSigned(_newer.bestIteration - _older.bestIteration));

        drawRow(y, tr("cmpConverged"),
            didConverge(_newer) ? tr("cmpYes") : tr("cmpNo"),
            didConverge(_older) ? tr("cmpYes") : tr("cmpNo"),
            dash());

        drawRow(y, tr("cmpFinalIter"),
            fmtKm(finalIterBest(_newer)), fmtKm(finalIterBest(_older)),
            fmtKmSigned(finalIterBest(_newer) - finalIterBest(_older)));

        drawRow(y, tr("cmpMeanIter"),
            fmtKm(meanIterBest(_newer)), fmtKm(meanIterBest(_older)),
            fmtKmSigned(meanIterBest(_newer) - meanIterBest(_older)));

        drawRow(y, tr("statsTime"),
            fmtSeconds(float(_newer.runtimeSeconds)), fmtSeconds(float(_older.runtimeSeconds)),
            fmtSecondsSigned(float(_newer.runtimeSeconds - _older.runtimeSeconds)));

        y += cGroupGap;

        // Footnote, worth stating explicitly: the history is cleared whenever a new city
        // set is drawn, so two runs can only appear here if they were solved over the
        // same cities - which is what makes the kilometre columns comparable at all.
        drawLabel(y, tr("cmpNote"), td::ColorID::SysText);
    }

public:
    ViewCompare(const RunRecord& newer, const RunRecord& older)
        : Canvas()
        , _newer(newer)
        , _older(older)
    {
        _mStr.reserve(64);
        enableResizeEvent(true);
        // Deliberately no setPreferredFrameRateRange()/startAnimation(): the content is a
        // fixed snapshot of two finished runs and never changes while visible.
    }
};