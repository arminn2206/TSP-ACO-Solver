//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//

#pragma once
#include <gui/MenuBar.h>
#include "Constants.h"

class MenuBar : public gui::MenuBar
{
private:
    gui::SubMenu subFirst;
    gui::SubMenu subThread;
protected:
    void populateFirstMenu()
    {
        auto& items = subFirst.getItems();
        items[0].initAsActionItem(tr("settings"), 10); //prevedeno u natGUI
        items[1].initAsSeparator();
        items[2].initAsQuitAppActionItem(tr("Quit"), "q"); //prevedeno u natGUI
    }

    void populateThreadMenu()
    {
        auto& items = subThread.getItems();
        items[0].initAsActionItem(tr("start"), 10);
        items[0].setAsCheckable();
        items[1].initAsActionItem(tr("reset"), 20);
        // Reloads Bosnia.xml and re-runs the random 15-25 city subsampling, giving a
        // brand new TSP instance without restarting the app.
        items[2].initAsActionItem(tr("newCities"), 30);
        // Writes the finished run to CSV (RunExport.h). Mirrored on the toolbar.
        items[3].initAsActionItem(tr("exportCSV"), 40);
        // Side-by-side comparison of the last two COMPLETED runs (DialogCompare).
        // Mirrored on the toolbar.
        //
        // Always enabled: gui::MenuItem in this SDK build exposes no setEnabled(), so
        // the guard lives entirely in MainWindow::onActionItem() (actionID 50), which
        // refuses the action and reports how many completed runs are stored.
        items[4].initAsActionItem(tr("compareRuns"), 50);
    }
public:
    MenuBar()
        : gui::MenuBar(2) //App menu + Animation menu (Model menu removed: the app only ever loads Bosnia.xml)
        , subFirst(cMenuApp, tr("App"), 3) //allocate items for the Application subMenu
        , subThread(cMenuAnimation, tr("Animation"), 5) //allocate items for the Animation subMenu (Start, Reset, New cities, Export CSV, Compare runs)
    {
        populateFirstMenu();
        populateThreadMenu();
        _menus[0] = &subFirst;
        _menus[1] = &subThread;
        //        prepare();
    }

    ~MenuBar()
    {
    }
};