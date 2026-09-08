//
//  DialogCompare.h
//  Modeless dialog wrapping ViewCompare, opened from Animation -> Compare last two
//  runs. Same pattern as DialogSettings: constructed with new, opened with open(),
//  kept above its parent, registered under a unique dialog id so MainWindow can
//  raise the existing instance instead of stacking duplicates.
//
//  Read-only, so it carries a single OK button and overrides no click handler.
//
#pragma once
#include <gui/Dialog.h>
#include <gui/Application.h>
#include "ViewCompare.h"

class DialogCompare : public gui::Dialog
{
protected:
    ViewCompare _viewCompare;

public:
    // newer / older are ViewMap's history entries 0 and 1. The caller must have
    // checked that both exist (MainView::canCompareRuns()); the records are copied
    // immediately, so the dialog is unaffected by anything started behind it.
    DialogCompare(gui::Frame* pFrame, td::UINT4 wndID,
        const RunRecord& newer, const RunRecord& older)
        : gui::Dialog(pFrame,
            { {gui::Dialog::Button::ID::OK, tr("Ok"), gui::Button::Type::Default} },
            gui::Size(760, 620), wndID)
        , _viewCompare(newer, older)
    {
        setTitle(tr("compareTitle"));
        setCentralView(&_viewCompare);
    }
};