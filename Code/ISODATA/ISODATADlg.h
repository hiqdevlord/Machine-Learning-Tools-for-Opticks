/*
* The information in this file is
* Copyright(c) 2012, Himanshu Singh <91.himanshu@gmail.com>
* and is subject to the terms and conditions of the
* GNU Lesser General Public License Version 2.1
* The license text is available from   
* http://www.gnu.org/licenses/lgpl.html
*/

#ifndef ISODATADLG_H
#define ISODATADLG_H

#include <QtGui/QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

class ISODATADlg : public QDialog
{
    Q_OBJECT

public:
    ISODATADlg(double SAMThreshold, unsigned int MaxIterations, unsigned int NumClus, double Lump, double MaxSTDV, unsigned int SamPrm, QWidget* pParent = NULL);
    virtual ~ISODATADlg();

    double getSAMThreshold() const;
    unsigned int getMaxIterations() const;
    unsigned int getNumClus() const;
    double getLump() const;
    double getMaxSTDV() const;
    unsigned int getSamPrm() const;

    public slots:

private:
    QDoubleSpinBox* mpSAMThreshold;
    QSpinBox* mpMaxIterations;
    QSpinBox* mpNumClus;
    QDoubleSpinBox* mpLump;
    QDoubleSpinBox* mpMaxSTDV;
    QSpinBox* mpSamPrm;
};


#endif