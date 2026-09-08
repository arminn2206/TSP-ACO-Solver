//
//  DialogHelp.h
//  Modeless dialog wrapping ViewHelp, opened from App -> Help. Same pattern as
//  DialogCompare: constructed with new, opened with open(), kept above its parent,
//  registered under a unique dialog id so MainWindow can raise the existing instance
//  instead of stacking duplicates.
//
//  Read-only, so it carries a single OK button and overrides no click handler.
//
#pragma once
#include <gui/Dialog.h>
#include <gui/Application.h>
#include "ViewHelp.h"

class DialogHelp : public gui::Dialog
{
protected:
    ViewHelp _viewHelp;

public:
    DialogHelp(gui::Frame* pFrame, td::UINT4 wndID)
        : gui::Dialog(pFrame,
            { {gui::Dialog::Button::ID::OK, tr("Ok"), gui::Button::Type::Default} },
            gui::Size(780, 760), wndID)
    {
        setTitle(tr("dlgHelp"));
        setCentralView(&_viewHelp);
    }
};
