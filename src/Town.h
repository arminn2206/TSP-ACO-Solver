//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include "Primitive.h"
#include <cassert>

class Town : public Primitive
{
protected:
    gui::Point _location;
public:
    Town() = default;

    Town(const td::String& name, const gui::Point& location, const gui::Size& sz)
        : Primitive(name)
        , _location(location)
    {
        gui::Point center(location.x * sz.width, location.y * sz.height);
        gui::Circle c(center, cTownR);
        _shape.createCircle(c);
    }

    // Move constructor
    Town(Town&& t) noexcept
        : Primitive(std::move(t))
        , _location(t._location)
    {
    }

    // Move assignment
    Town& operator=(Town&& t) noexcept
    {
        if (this != &t)
        {
            Primitive::operator=(std::move(t));
            _location = std::move(t._location);
        }
        return *this;
    }

    // Delete copy
    Town(const Town&) = delete;
    Town& operator=(const Town&) = delete;

    const gui::Point& getLocation() const { return _location; }

    void updatePosition(const gui::Size& sz)
    {
        gui::Point newLocation(_location.x * sz.width, _location.y * sz.height);
        gui::Circle c(newLocation, cTownR);
        _shape.updateCircleNodes(c);
    }

protected:
    void showTownName(const Primitive::Options& options, td::ColorID colorID) const
    {
        if (!options.showAllTownNames)
        {
            if (_guiState != Status::OnPath)
                return;
        }
        td::TextAlignment align = td::TextAlignment::Left;
        gui::Point townLocation(_location.x * options.viewSize.width, _location.y * options.viewSize.height);
        gui::Rect r(townLocation, options.viewSize);
        if (_location.x > 0.9)
        {
            align = td::TextAlignment::Right;
            r.left = 0;
            r.right = townLocation.x;
        }
        else if (_location.x > 0.1)
        {
            align = td::TextAlignment::Center;
            r.left = townLocation.x - options.viewSize.width / 2;
            r.right = townLocation.x + options.viewSize.width / 2;
        }

        gui::DrawableString::draw(_name, r, gui::Font::ID::SystemNormal, colorID, align);
    }

public:
    void draw(const Primitive::Options& options) const
    {
        if (_id == options.startID)
        {
            //Draw start flag (TSP has no separate goal town - tour returns to start)
            gui::Point flagLocation(_location.x * options.viewSize.width, _location.y * options.viewSize.height - options.flagSize);
            gui::Size sz{ gui::CoordType(options.flagSize), gui::CoordType(options.flagSize) };
            gui::Rect r(flagLocation, sz);
            options.imgStart.draw(r);
        }

        //draw town symbol using its color which is based on visited state of the town
        switch (_guiState)
        {
        case Primitive::Status::Unvisited:
            _shape.drawFill(options.normalColor);
            showTownName(options, td::ColorID::SysText);
            break;
        case Primitive::Status::OnPath:
        {
            _shape.drawFill(options.pathColor);
            showTownName(options, options.txtColor);
        }
        break;
        default:
            assert(false);
        }
    }

    void reset()
    {
        Primitive::reset();
    }
};