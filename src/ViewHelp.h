//
//  ViewHelp.h
//  Static in-app explanation of what the application does and how to read it,
//  opened from App -> Help. Not a live view - it holds no pointer into the model and
//  draws once, on demand, the same as ViewCompare.
//
//  Content is plain English only. The rest of the UI (menu/toolbar labels, dialog
//  titles, field labels) goes through tr() and has a Bosnian translation in
//  res/tr/BA/main.xml; this body text does not. Translating several dozen lines of
//  prose was judged not worth doing for a help screen at this stage - the menu entry
//  and dialog title ARE translated, so a Bosnian-language user still finds and opens
//  it, just reads English once inside.
//
//  Layout is the same fixed-row technique ViewStats and ViewCompare already use:
//  gui::DrawableString::draw() does not wrap text within a Rect, so every line below
//  is pre-wrapped by hand to fit the dialog width. If it ever runs long enough to
//  clip against the bottom of the dialog, widen DialogHelp's Size or drop cRowHeight
//  rather than trying to add wrapping here.
//
#pragma once
#include <gui/Canvas.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <vector>

class ViewHelp : public gui::Canvas
{
protected:
    struct Line
    {
        td::String text;
        bool heading = false;   // drawn larger/bold, with extra space above
        bool spacer = false;    // blank line - drawn as nothing, just advances y
    };

    std::vector<Line> _lines;
    gui::Size _size;

    static constexpr float cPadX = 16;
    static constexpr float cTitleY = 10;
    static constexpr float cFirstRowY = 44;
    static constexpr float cRowHeight = 17;
    static constexpr float cHeadingGap = 8;  // extra space above a heading

    void addHeading(const char* text)
    {
        Line l;
        l.text = text;
        l.heading = true;
        _lines.push_back(l);
    }

    void addLine(const char* text)
    {
        Line l;
        l.text = text;
        _lines.push_back(l);
    }

    void addSpacer()
    {
        Line l;
        l.spacer = true;
        _lines.push_back(l);
    }

    void buildContent()
    {
        addHeading("What this application does");
        addLine("Solves the Travelling Salesman Problem with Ant Colony Optimization");
        addLine("(ACO): find the shortest closed tour that visits every city exactly");
        addLine("once. Each run draws 15-25 cities at random from 100 real Bosnian");
        addLine("cities and searches for a short tour over them.");
        addSpacer();

        addHeading("How the search works");
        addLine("A population of ants builds tours iteration by iteration. Each ant");
        addLine("chooses its next city by a mix of pheromone strength and distance -");
        addLine("nearby cities and heavily-used edges are more likely to be picked.");
        addLine("After every iteration, pheromone evaporates a little everywhere and");
        addLine("is deposited more heavily along shorter tours, so the colony");
        addLine("gradually concentrates on good routes.");
        addSpacer();

        addHeading("The map (left panel)");
        addLine("Dots are cities; the flag marks the tour's display start point.");
        addLine("The highlighted route is the best tour found so far. Faint lines");
        addLine("between cities show pheromone strength - thicker means more ants");
        addLine("have used that edge. This thins out as the colony converges on a");
        addLine("route instead of spreading pheromone everywhere.");
        addSpacer();

        addHeading("The convergence chart");
        addLine("Two curves against iteration number: the best tour found so far");
        addLine("(a descending staircase - it can only improve or stay flat), and");
        addLine("the best tour built during that single iteration (a noisier line");
        addLine("that shows the colony still exploring). The horizontal reference");
        addLine("line is a greedy nearest-neighbour tour over the same cities, so");
        addLine("the scale is anchored to something outside the run itself.");
        addSpacer();

        addHeading("The stats sidebar");
        addLine("Iteration - how far the current run has progressed.");
        addLine("Best cost - the shortest tour found so far, in kilometres.");
        addLine("Best at iter - which iteration produced that tour.");
        addLine("Nearest-neighbour - the greedy baseline over the same cities.");
        addLine("Improvement - how much shorter than that baseline, as a signed");
        addLine("percentage. A negative value means ACO has not yet beaten the");
        addLine("greedy tour, which can genuinely happen with a short run.");
        addSpacer();

        addHeading("Controls");
        addLine("Start / Stop - run or pause the search. Stopping early keeps");
        addLine("whatever best tour has been found, but that run cannot later be");
        addLine("compared (Compare only stores runs that finished).");
        addLine("Reset - clears the current run over the same city set.");
        addLine("New cities - draws a fresh random set of 15-25 cities.");
        addLine("Export CSV - saves the finished run's convergence data and the");
        addLine("full problem instance (coordinates, tour, distance matrix) as two");
        addLine("CSV files, for offline analysis.");
        addLine("Compare - shows the two most recently completed runs side by side,");
        addLine("with the differences between them.");
        addSpacer();

        addHeading("Settings");
        addLine("Language, dark/light-appropriate colours, whether to show every");
        addLine("town's name or only the ones on the current best tour, sound, and");
        addLine("how the pheromone trail is drawn. All ACO parameters (ants,");
        addLine("iterations, alpha, beta, evaporation rate, seed) and the animation");
        addLine("speed persist across restarts.");
        addSpacer();

        addHeading("Reproducibility");
        addLine("A fixed seed replays the same sequence of city draws and the same");
        addLine("ant decisions. Seed 0 means \"random\" and is not reproducible - the");
        addLine("exported CSV always records the seed actually used, and labels a");
        addLine("random run as such rather than writing a misleading 0.");
    }

    void onResize(const gui::Size& newSize) override
    {
        _size = newSize;
    }

    void onDraw(const gui::Rect& rect) override
    {
        gui::DrawableString::draw(tr("dlgHelp"), gui::Point(cPadX, cTitleY),
            gui::Font::ID::SystemLargerBold, td::ColorID::SysText);

        float y = cFirstRowY;
        for (const auto& l : _lines)
        {
            if (l.spacer)
            {
                y += cRowHeight * 0.6f;
                continue;
            }

            if (l.heading)
            {
                y += cHeadingGap;
                gui::DrawableString::draw(l.text, gui::Point(cPadX, y),
                    gui::Font::ID::SystemLargerBold, td::ColorID::SysText);
            }
            else
            {
                gui::DrawableString::draw(l.text, gui::Point(cPadX, y),
                    gui::Font::ID::SystemNormal, td::ColorID::SysText);
            }
            y += cRowHeight;
        }
    }

public:
    ViewHelp()
        : Canvas()
    {
        buildContent();
        enableResizeEvent(true);
        // Deliberately no animation: this is a fixed page, drawn once on demand -
        // same reasoning as ViewCompare.
    }
};
