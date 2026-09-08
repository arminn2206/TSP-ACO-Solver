//
//  Created by Izudin Dzafic on 18/10/2025.
//  Copyright © 2025 IDz. All rights reserved.
//
#pragma once
#include <gui/View.h>
#include <gui/Label.h>
#include <gui/ComboBox.h>
#include <gui/CheckBox.h>
#include <gui/LineEdit.h>
#include <gui/ColorPicker.h>
#include <gui/GridLayout.h>
#include <gui/GridComposer.h>
#include <gui/ToolBar.h>
#include "Primitive.h"
#include <cassert>

class ViewSettings : public gui::View
{

protected:
    gui::Label _lblLangNow;
    gui::LineEdit _leLang;
    gui::Label _lblLangNew;
    gui::ComboBox _cmbLangs;
    gui::CheckBox _chbToolbarIconsAndLabels;
    gui::CheckBox _cbShowNamesForEachTown;
    gui::CheckBox _chbPlaySound;
    gui::Label _lblPheromoneMode;
    gui::ComboBox _cmbPheromoneMode;
    gui::Label _lblPheromoneColor;
    gui::ColorPicker _pheromoneColorPicker;
    gui::GridLayout _mainLayout;
    gui::ToolBar* _pMainTB = nullptr;
    int _initialLangSelection;
public:
    // Takes the LIVE application state rather than re-reading the properties store.
    //
    // This dialog is a view onto settings that are already loaded, clamped and in force:
    // MainView's constructor reads them into Primitive::Options at start-up, and Model's
    // constructor supplies the light/dark defaults underneath that. Re-reading the store
    // here meant repeating every one of those defaults, and any that disagreed produced
    // a dialog that displayed something the application was not doing - and then wrote
    // the dialog's wrong value back on OK. That happened twice (see the playSound and
    // pheromone-colour notes below). Passing the live values in means each default is
    // written down in exactly one place.
    //
    // options is taken by const reference because Primitive::Options is non-copyable.
    // Nothing is retained - the values are read out here, in the constructor.
    //
    // playSound arrives separately because it lives in ViewMap rather than in Options.
    ViewSettings(const Primitive::Options& options, bool playSound)
        : _lblLangNow(tr("lblLang"))
        , _lblLangNew(tr("lblLang2"))
        , _chbToolbarIconsAndLabels(tr("chbTBIcsAndLbls"))
        , _cbShowNamesForEachTown(tr("chbShowTownNames"))
        , _chbPlaySound(tr("chbPlaySound"))
        , _lblPheromoneMode(tr("lblPheromoneMode"))
        , _lblPheromoneColor(tr("lblPheromoneColor"))
        , _mainLayout(7, 2)
    {
        gui::Application* pApp = getApplication();
        auto appProperties = pApp->getProperties();
        assert(appProperties);

        // The fallback MUST match main.cpp, which does
        //     appProperties->getValue("translation", "EN")
        // and passes the result to Application::init(). With "BA" here the mismatch was
        // not cosmetic: on a machine with nothing persisted, the application started in
        // English while this combo pre-selected Bosanski, that wrong index went into
        // _initialLangSelection so isRestartRequired() reported false, and pressing OK
        // wrote translation="BA" to the store. The next launch came up in Bosnian
        // without the user asking for it.
        td::String strTr = appProperties->getValue("translation", "EN");

        _leLang.setAsReadOnly();
        int newLangIndex = 0;
        //populate combo
        auto& langs = getSupportedLanguages();
        auto currTranslationIndex = getTranslationLanguageIndex();

        auto& strCurrentLanguage = langs[currTranslationIndex].getDescription();
        _leLang.setText(strCurrentLanguage);

        int i = 0;
        for (const auto& lang : langs)
        {
            if (lang.getExtension() == strTr)
                newLangIndex = i;
            _cmbLangs.addItem(lang.getDescription());
            ++i;
        }

        _cmbLangs.sizeToFit(); //adjust its size to fit the content

        bool showLabels = appProperties->getTBLabelVisibility(mu::IAppProperties::ToolBarType::Main, true);
        _chbToolbarIconsAndLabels.setChecked(showLabels);

        // From the live options, not from the properties store: Primitive::Options' own
        // constructor already did that read.
        _cbShowNamesForEachTown.setChecked(options.showAllTownNames);

        // From ViewMap, which owns this flag. Re-reading it here with a literal default
        // of false meant that on a first launch sound was actually playing while the
        // checkbox showed unchecked, and merely opening Settings and pressing OK wrote
        // that false back and silently turned the sound off. Taking the value from the
        // owner leaves no second literal to drift.
        _chbPlaySound.setChecked(playSound);

        // Pheromone display mode + color (see Primitive.h::PheromoneDisplayMode).
        //
        // The colour MUST come from options and not from a literal: the application's
        // pheromone colour is theme-dependent (Model's constructor sets Green in dark
        // mode, Blue in light), so a hardcoded Blue fallback made a dark-mode machine
        // draw green pheromone while this picker showed blue - and pressing OK wrote
        // Blue back and changed the map. The same failure as playSound above.
        //
        // The mode is clamped rather than trusted because it is stored as a raw int and
        // reaches options through a cast; an out-of-range value would index past the end
        // of a two-item combo.
        _cmbPheromoneMode.addItem(tr("phModeAll"));
        _cmbPheromoneMode.addItem(tr("phModeThreshold"));
        int pmIdx = (int)options.pheromoneMode;
        if (pmIdx < 0 || pmIdx > 1)
            pmIdx = (int)PheromoneDisplayMode::Threshold;
        _cmbPheromoneMode.selectIndex(pmIdx);

        _pheromoneColorPicker.setValue(options.pheromoneColor);

        _cmbLangs.selectIndex(newLangIndex);
        _initialLangSelection = newLangIndex;

        // populate grid
        gui::GridComposer gc(_mainLayout);
        gc.appendRow(_lblLangNow) << _leLang;
        gc.appendRow(_lblLangNew) << _cmbLangs;
        gc.appendRow(_chbToolbarIconsAndLabels, 0);
        gc.appendRow(_cbShowNamesForEachTown, 0);
        gc.appendRow(_chbPlaySound, 0);
        gc.appendRow(_lblPheromoneMode) << _cmbPheromoneMode;
        gc.appendRow(_lblPheromoneColor) << _pheromoneColorPicker;

        setLayout(&_mainLayout);

        //handler for checkbox that controls labels (this one requires immediate action 
        //to show/hide labels so that user can see the effects immediatelly
        _chbToolbarIconsAndLabels.onClick([this]()
            {
                if (_pMainTB)
                {
                    bool bShowLabelsOnMTB = _chbToolbarIconsAndLabels.isChecked();
                    _pMainTB->showLabels(bShowLabelsOnMTB);
                }
            });
    }

    td::String getTranslationExt()
    {
        td::String strExt;
        int currSelection = _cmbLangs.getSelectedIndex();
        if (currSelection >= 0)
        {
            auto& langs = getSupportedLanguages();
            strExt = langs[currSelection].getExtension();
        }

        return strExt;
    }

    bool showTownNames() const
    {
        return _cbShowNamesForEachTown.isChecked();
    }

    bool playSound() const
    {
        return _chbPlaySound.isChecked();
    }

    PheromoneDisplayMode pheromoneDisplayMode() const
    {
        int idx = _cmbPheromoneMode.getSelectedIndex();
        if (idx < 0)
            idx = (int)PheromoneDisplayMode::Threshold;
        return (PheromoneDisplayMode)idx;
    }

    td::ColorID pheromoneColor() const
    {
        return _pheromoneColorPicker.getValue();
    }

    void setMainTB(gui::ToolBar* pTB)
    {
        _pMainTB = pTB;
    }

    bool isRestartRequired() const
    {
        auto selectedLanguageIndex = _cmbLangs.getSelectedIndex();
        return (_initialLangSelection != selectedLanguageIndex);
    }

};