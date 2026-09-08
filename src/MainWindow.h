//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/Window.h>
#include "MenuBar.h"
#include "ToolBar.h"
#include "MainView.h"
#include <functional>
#include <td/MutableString.h>
#include "DialogSettings.h"
#include "DialogCompare.h"
#include "DialogHelp.h"
#include <cassert>


class MainWindow : public gui::Window
{
protected:
    gui::Image _imgStart;
    gui::Image _imgStop;
    MenuBar _mainMenuBar;
    ToolBar _toolBar;

    std::function<void()> _fnUpdateMenuAndTB;

    MainView _mainView;
    const td::UINT4 _cSettingsDlgID = 1000; //any unique id among dialogs
    const td::UINT4 _cCompareDlgID = 1001;  //Compare-last-two-runs dialog
    const td::UINT4 _cHelpDlgID = 1002;     //App -> Help dialog

    // Scratch buffer for alert text that has to carry a number (see actionID == 50).
    td::MutableString _mStrAlert;

protected:

    void onInitialAppearance() override //will be called only once
    {
        updateMenuAndTB();
        _mainView.setFocusToAnimation();
    }

    void updateMenuAndTB()
    {
        bool isRunning = _mainView.isRunning();
        gui::MenuItem* pMenuItem = _mainMenuBar.getItem(cMenuAnimation, 0, 0, 10);
        pMenuItem->setChecked(isRunning);

        // Compare is only meaningful when two COMPLETED runs are in memory and nothing
        // is running, but gui::MenuItem in this SDK build has no setEnabled(), so the
        // item cannot be greyed out. onActionItem()'s guard (with an explanatory alert)
        // is the enforcement; see actionID == 50 below.

        gui::ToolBarItem* pTBItem = _toolBar.getItem(cMenuAnimation, 0, 0, 10);
        if (pTBItem)
        {
            if (isRunning)
            {
                pTBItem->setImage(&_imgStop);
                pTBItem->setLabel(tr("stop"));
                pTBItem->setTooltip(tr("stopTT"));
            }
            else
            {
                pTBItem->setImage(&_imgStart);
                pTBItem->setLabel(tr("start"));
                pTBItem->setTooltip(tr("startTT"));
            }
        }
    }

    bool shouldClose() override
    {
        if (_mainView.isRunning())
        {
            showAlert(tr("closeNOK"), tr("cloeErr"));
            return false;
        }
        return true;
    }

    bool onClick(gui::Dialog* dlg, td::UINT4 dlgID) override
    {
        //get notification from dialog
        if (dlgID == _cSettingsDlgID)
        {
            if (dlg->getClickedButtonID() == gui::Dialog::Button::ID::OK)
            {
                DialogSettings* pSettingsDlg = (DialogSettings*)dlg;

                auto& viewMap = _mainView.getViewMap();
                auto& options = viewMap.getOptions();
                bool bShowTownNames = pSettingsDlg->showTownNames();
                options.showAllTownNames = bShowTownNames;

                bool playSound = pSettingsDlg->playSound();
                viewMap.playSound(playSound);

                options.pheromoneMode = pSettingsDlg->pheromoneDisplayMode();
                options.pheromoneColor = pSettingsDlg->pheromoneColor();
                viewMap.invalidatePheromoneCache(); // pick up the new mode/color immediately

                viewMap.refresh();
            }
            return true;
        }

        // The compare dialog is read-only: nothing to write back when it is dismissed.
        // Acknowledged here rather than falling through to "return false", which would
        // tell the framework the notification went unhandled.
        if (dlgID == _cCompareDlgID)
            return true;

        // Same as Compare: Help is read-only, nothing to write back.
        if (dlgID == _cHelpDlgID)
            return true;

        return false;
    }

    bool onActionItem(gui::ActionItemDescriptor& aiDesc) override
    {
        auto [menuID, firstSubMenuID, lastSubMenuID, actionID] = aiDesc.getIDs();
        //        auto pActionItem = aiDesc.getActionItem();
        switch (menuID)
        {
        case cMenuApp:
        {
            if (actionID == 20)
            {
                // App -> Help: static explanation dialog, no live state to seed it
                // with, unlike Settings.
                auto pDlg = getAttachedWindow(_cHelpDlgID);
                if (pDlg)
                    pDlg->setFocus();
                else
                {
                    DialogHelp* pHelpDlg = new DialogHelp(this, _cHelpDlgID);
                    pHelpDlg->keepOnTopOfParent();
                    pHelpDlg->open();
                }
                return true;
            }

            // actionID == 10 (Settings) and any other App-menu item falls through here.
            auto pDlg = getAttachedWindow(_cSettingsDlgID);
            if (pDlg)
                pDlg->setFocus();
            else
            {
                // Seed the dialog from the LIVE state rather than letting it re-read the
                // properties store with defaults of its own - see ViewSettings'
                // constructor for why.
                auto& viewMap = _mainView.getViewMap();
                DialogSettings* pSettingsDlg = new DialogSettings(this, _cSettingsDlgID,
                    viewMap.getOptions(), viewMap.isPlaySoundEnabled());
                pSettingsDlg->keepOnTopOfParent();
                pSettingsDlg->setMainTB(&_toolBar);
                pSettingsDlg->open();
            }
            return true;
        }
        break;
        case cMenuAnimation:
            if (actionID == 10)
            {
                _mainView.startStop();
                return true;
            }
            else if (actionID == 20)
            {
                if (_mainView.isRunning())
                    showAlert(tr("resetBusy"), tr("resetBusyInfo"));
                else
                    _mainView.resetRun();
                return true;
            }
            else if (actionID == 30)
            {
                // Draw a new random 15-25 city instance. Same guard as Reset: the worker
                // thread owns the town list while it runs.
                //
                // NOTE on the resource IDs: natGUI truncates translation keys, so
                // "newCitiesWhileRunning" and "newCitiesWhileRunningInfo" collided as a
                // single key. Shortened to newCitiesBusy / newCitiesBusyInfo.
                if (_mainView.isRunning())
                {
                    showAlert(tr("newCitiesBusy"), tr("newCitiesBusyInfo"));
                }
                else
                {
                    _mainView.newCities();
                    // newCities() drops the completed-run history (the stored costs
                    // belong to the old city set), so Compare has to be re-evaluated -
                    // nothing else would do it until the next run.
                    updateMenuAndTB();
                }
                return true;
            }
            else if (actionID == 40)
            {
                // Export the finished run to CSV (RunExport.h / MainView::exportRun).
                //
                // Same running-guard as Reset and New cities, for a different reason:
                // the other two would race the worker thread, this one would capture a
                // half-finished convergence series and write it out as a result.
                if (_mainView.isRunning())
                {
                    showAlert(tr("exportBusy"), tr("exportBusyInfo"));
                    return true;
                }

                td::String msg;
                int rc = _mainView.exportRun(msg);
                if (rc > 0)
                    showAlert(tr("exportOK"), msg);       // msg = folder + file names
                else if (rc == 0)
                    showAlert(tr("exportNone"), tr("exportNoneInfo"));
                else
                    showAlert(tr("exportFail"), msg);     // msg = the path that failed
                return true;
            }
            else if (actionID == 50)
            {
                // Compare the last two completed runs.
                //
                // Both guards below are the real enforcement, not belt-and-braces: the
                // menu item cannot be disabled in this SDK build, and getHistoryRun(1) on
                // a one-entry history is an out-of-range read, so it has to be impossible
                // to reach rather than merely unlikely.
                if (_mainView.isRunning())
                {
                    showAlert(tr("compareBusy"), tr("compareBusyInfo"));
                    return true;
                }

                if (!_mainView.canCompareRuns())
                {
                    // The message carries the actual number of stored runs. Without it
                    // the alert is undiagnosable: "not enough runs" looks wrong to
                    // someone who has just watched several runs go by, and gives no clue
                    // that runs ended with Stop are deliberately not counted.
                    _mStrAlert.reset();
                    _mStrAlert.appendFormat(tr("compareNoneInfo").c_str(),
                        (int)_mainView.getCompletedRunCount());
                    showAlert(tr("compareNone"), _mStrAlert.getString());
                    return true;
                }

                auto pDlg = getAttachedWindow(_cCompareDlgID);
                if (pDlg)
                {
                    pDlg->setFocus();
                }
                else
                {
                    // Index 0 is the most recent completed run, index 1 the one before
                    // it. Both are copied inside ViewCompare's constructor, so the dialog
                    // is unaffected by anything started behind it.
                    DialogCompare* pCompareDlg = new DialogCompare(this, _cCompareDlgID,
                        _mainView.getHistoryRun(0),
                        _mainView.getHistoryRun(1));
                    pCompareDlg->keepOnTopOfParent();
                    pCompareDlg->open();
                }
                return true;
            }
            break;
        default:
            assert(false);
        }
        return false;
    }

public:
    MainWindow()
        : gui::Window(gui::Size(1200, 800))
        , _imgStart(":start")
        , _imgStop(":stop")
        , _toolBar(&_imgStart, &_imgStop)
        , _fnUpdateMenuAndTB(std::bind(&MainWindow::updateMenuAndTB, this))
        , _mainView(_fnUpdateMenuAndTB)
    {
        setTitle(tr("appTitle"));
        _mainMenuBar.setAsMain(this);
        setToolBar(_toolBar);
        setCentralView(&_mainView);
    }

    ~MainWindow()
    {
    }

};