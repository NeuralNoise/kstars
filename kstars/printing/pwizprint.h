/***************************************************************************
                          pwizprint.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Wed Aug 3 2011
    copyright            : (C) 2011 by Rafał Kułaga
    email                : rl.kulaga@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef PWIZPRINT_H
#define PWIZPRINT_H

#include "ui_pwizprint.h"

class PrintingWizard;

class PWizPrintUI : public QFrame, public Ui::PWizPrint
{
    Q_OBJECT
public:
    PWizPrintUI(PrintingWizard *wizard, QWidget *parent = 0);

private slots:
    void slotPreview();
    void slotPrintPreview(QPrinter *printer);
    void slotPrint();
    void slotExport();

private:
    void printDocument(QPrinter *printer);

    PrintingWizard *m_ParentWizard;
};

#endif // PWIZPRINT_H