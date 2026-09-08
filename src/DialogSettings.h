//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/Dialog.h>
#include "ViewSettings.h"
#include <gui/Application.h>

class DialogSettings : public gui::Dialog
{
protected:
    ViewSettings _viewSettings;

    bool onClick(Dialog::Button::ID btnID, gui::Button* pButton) override
    {
        if (btnID == Dialog::Button::ID::OK)
        {
            //            auto pApp = getApplication();
            auto appProperties = getAppProperties();

            td::String strTr = _viewSettings.getTranslationExt();
            if (strTr.length() > 0)
            {
                //write translation info back to properties
                appProperties->setValue("translation", strTr);
            }

            bool showTownNames = _viewSettings.showTownNames();
            appProperties->setValue("showTownNames", showTownNames);

            bool playSound = _viewSettings.playSound();
            appProperties->setValue("playSound", playSound);

            appProperties->setValue("pheromoneMode", (int)_viewSettings.pheromoneDisplayMode());
            appProperties->setValue("pheromoneColor", _viewSettings.pheromoneColor());
        }
        return true;
    }
public:
    // options / playSound are the LIVE application state, passed straight through to
    // ViewSettings so the dialog opens showing what the application is actually doing.
    // See ViewSettings' constructor for why the dialog no longer re-reads the properties
    // store to find that out.
    DialogSettings(gui::Frame* pFrame, td::UINT4 wndID,
        const Primitive::Options& options, bool playSound)
        : gui::Dialog(pFrame, { {gui::Dialog::Button::ID::OK, tr("Ok"), gui::Button::Type::Default},
                                {gui::Dialog::Button::ID::Cancel, tr("Cancel")} }, gui::Size(450, 100), wndID)
        , _viewSettings(options, playSound)
    {
        setTitle(tr("dlgSettings"));
        setCentralView(&_viewSettings);
    }

    void setMainTB(gui::ToolBar* pTB)
    {
        _viewSettings.setMainTB(pTB);
    }

    bool showTownNames() const
    {
        return _viewSettings.showTownNames();
    }

    bool playSound() const
    {
        return _viewSettings.playSound();
    }

    PheromoneDisplayMode pheromoneDisplayMode() const
    {
        return _viewSettings.pheromoneDisplayMode();
    }

    td::ColorID pheromoneColor() const
    {
        return _viewSettings.pheromoneColor();
    }

    ~DialogSettings() {
        if (_viewSettings.isRestartRequired())
        {
            gui::Alert::showYesNoQuestion(tr("RestartRequired"), tr("RestartRequiredInfo"), tr("Restart"), tr("DoNoRestart"), [this](gui::Alert::Answer answer) {
                if (answer == gui::Alert::Answer::Yes)
                {
                    auto pApp = getApplication();
                    //clean up and save here whatever you need, the application is about to terminate... and restart fresh....
                    pApp->restart();
                }
                });
        }
    }


};