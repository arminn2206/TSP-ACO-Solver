//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/Shape.h>
#include <math/math.h>
#include <gui/Image.h>
#include <gui/Symbol.h>
#include <gui/Transformation.h>
#include <math/Constants.h>
#include <math/math.h>
#include <gui/Sound.h>
#include <gui/DrawableString.h>
#include <gui/Font.h>
#include <vector>
#include <map>
#include <atomic>        // Options::sleepMS - written by the UI thread (speed slider),
                         // read by the ACO worker thread inside its iteration loop.
#include "GraphType.h"
#include <thread/Thread.h>
#include <functional>
#include <gui/Thread.h>
#include <gui/Image.h>
#include <td/MutableString.h>  // kept: this header is where td::MutableString reaches
                               // several downstream files.
#include "Constants.h"   // cDefaultNumAnts/Alpha/Beta/Q/... used in Options below.
                         // NOTE: distinct from <math/Constants.h> included above.

const float cTownR = 4;

using CallBack = std::function<void(int)>;
using MapTownNameToID = std::map<td::String, GraphType>;

// How ViewMap's pheromone background layer chooses which edges to draw:
//  - All: every pair of towns (O(n^2) lines - complete but can look busy)
//  - Threshold: only edges above mean * cPheromoneThresholdMultiplier
enum class PheromoneDisplayMode : td::BYTE { All = 0, Threshold = 1 };

class Primitive
{
public:
    // Options is deliberately non-copyable (it holds a gui::Image and a std::atomic).
    // Everything takes it by reference via Model::getOptions() / ViewMap::getOptions().
    class Options
    {
    public:
        gui::Image imgStart;
        gui::Size viewSize;

        // Which city carries the start flag on the map, and which city the STORED
        // best tour is rotated to begin at (Model::rotateTourToStart).
        //
        // A PRESENTATION anchor, not an algorithm parameter: ants start from randomly
        // chosen cities, as in Dorigo & Stutzle, so this does not influence the search.
        // It fixes where the exported tour begins, which is what makes the exported
        // ordering readable and reproducible.
        GraphType startID = 1;
        float ratioLong = 400;
        float ratioLat = 375;

        // Delay between ACO iterations, in milliseconds.
        //
        // The one option genuinely written by one thread while another reads it: the
        // speed slider (UI thread) changes it live while the worker reads it at the
        // bottom of every iteration in Model::runACO(). Atomic for that reason.
        std::atomic<int> sleepMS{ 500 };

        int flagSize = 32;

        // The ACO parameters below are read ONCE by the worker thread at the start of
        // a run (MainView::readACOParamsFromUI() writes them just before
        // ViewMap::start()) and are not touched again until it ends, so they need not
        // be atomic. This is what the project proposal means by "all algorithm
        // parameters are kept constant throughout the run".
        int numAnts = cDefaultNumAnts;
        int numIterations = cDefaultNumIterations;
        float alpha = cDefaultAlpha;
        float beta = cDefaultBeta;
        float evaporationRate = cDefaultEvaporationRate;
        float initialPheromone = cDefaultInitialPheromone;
        float Q = cDefaultQ;

        // Master RNG seed - see cSeedRandom in Constants.h. Unlike the parameters
        // above it is ALSO read outside a run: Model::loadTowns() uses it for the city
        // subsampling, at construction and on every "New cities".
        int seed = cDefaultSeed;

        td::ColorID normalColor;
        td::ColorID pathColor;
        td::ColorID txtColor;
        bool showAllTownNames = true;

        // Pheromone background layer (edge thickness = relative pheromone strength)
        PheromoneDisplayMode pheromoneMode = PheromoneDisplayMode::Threshold;
        td::ColorID pheromoneColor;

        Options()
            : imgStart(":source")
        {
            gui::Application* pApp = gui::getApplication();
            auto appProperties = pApp->getProperties();
            showAllTownNames = appProperties->getValue("showTownNames", showAllTownNames);
        }

        Options(const Options&) = delete;
        Options& operator=(const Options&) = delete;
    };
    // ACO only ever marks a town as Unvisited or OnPath (part of the current best tour).
    enum class Status : td::BYTE { Unvisited = 0, OnPath };

protected:
    td::String _name;
    gui::Shape _shape;
    GraphType _id = 0;
    Status _guiState = Status::Unvisited;

protected:
    void reset()
    {
        _guiState = Status::Unvisited;
    }
public:
    Primitive() = default;

    Primitive(const td::String& name)
        : _name(name)
    {
    }

    // Move constructor. NOTE: _name is COPIED, not moved.
    Primitive(Primitive&& p) noexcept
        : _name(p._name)
        , _shape(std::move(p._shape))
        , _id(p._id)
        , _guiState(p._guiState)
    {
    }

    // Move assignment
    Primitive& operator=(Primitive&& p) noexcept
    {
        if (this != &p)
        {
            _name = p._name;
            _shape = std::move(p._shape);
            _id = p._id;
            _guiState = p._guiState;
        }
        return *this;
    }

    // Deleted copy operations (Shape is non-copyable)
    Primitive(const Primitive&) = delete;
    Primitive& operator=(const Primitive&) = delete;

    const td::String& getName() const
    {
        return _name;
    }

    void setGUIState(Status status)
    {
        _guiState = status;
    }

    Primitive::Status getGUIState() const
    {
        return _guiState;
    }

    void setID(GraphType primID)
    {
        _id = primID;
    }

    GraphType getID() const
    {
        return _id;
    }
};