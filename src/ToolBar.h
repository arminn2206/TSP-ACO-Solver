//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//

#pragma once
#include <gui/ToolBar.h>
#include <gui/Image.h>
#include <gui/Symbol.h>
#include "Constants.h"

class ToolBar : public gui::ToolBar
{
    gui::Image _settings;
    gui::Image _newCities;
    gui::Image _compare;
    gui::Image _exportCSV;
public:
    // imgReset reuses an already-loaded image (MainWindow passes its Stop icon) so
    // this doesn't depend on a ":reset" resource that may not exist in the
    // professor's original resource bundle. ":newCities", ":compare" and
    // ":exportCSV" are image resources declared in res/main.xml.
    //
    // Action IDs must match the menu ones (MenuBar.h) - toolbar and menu items are
    // both dispatched through MainWindow::onActionItem:
    //   cMenuApp/10       -> Settings dialog
    //   cMenuAnimation/10 -> Start-Stop toggle
    //   cMenuAnimation/20 -> Reset
    //   cMenuAnimation/30 -> New cities
    //   cMenuAnimation/40 -> Export run (CSV)
    //   cMenuAnimation/50 -> Compare last two completed runs
    //
    // Compare uses the SHORT label tr("compareTB") rather than the menu's
    // tr("compareRuns"): toolbar labels sit under the icon and set the minimum
    // toolbar width, so the descriptive wording would stretch the whole bar.
    ToolBar(gui::Image* imgRun, gui::Image* imgReset)
        : gui::ToolBar("mainTB", 6)
        , _settings(":settings")
        , _newCities(":newCities")
        , _compare(":compare")
        , _exportCSV(":exportCSV")
    {
        addItem(tr("settings"), &_settings, tr("settingsTT"), 10, 0, 0, 10);
        addItem(tr("start"), imgRun, tr("startTT"), cMenuAnimation, 0, 0, 10);
        addItem(tr("reset"), imgReset, tr("resetTT"), cMenuAnimation, 0, 0, 20);
        addItem(tr("newCities"), &_newCities, tr("newCitiesTT"), cMenuAnimation, 0, 0, 30);
        addItem(tr("exportCSV"), &_exportCSV, tr("exportCSVTT"), cMenuAnimation, 0, 0, 40);
        addItem(tr("compareTB"), &_compare, tr("compareRunsTT"), cMenuAnimation, 0, 0, 50);
    }
};