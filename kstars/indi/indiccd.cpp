/*  INDI CCD
    Copyright (C) 2012 Jasem Mutlaq <mutlaqja@ikarustech.com>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
 */

#include <config-kstars.h>

#include <string.h>

#include <KMessageBox>
#include <QStatusBar>
#include <QImageReader>
#include <KNotifications/KNotification>

#include <basedevice.h>

#ifdef HAVE_CFITSIO
#include "fitsviewer/fitsviewer.h"
#include "fitsviewer/fitscommon.h"
#include "fitsviewer/fitsview.h"
#include "fitsviewer/fitsdata.h"
#endif

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

#include "driverinfo.h"
#include "clientmanager.h"
#include "streamwg.h"
#include "indiccd.h"
#include "guimanager.h"
#include "kstarsdata.h"
#include "fov.h"
#include "kspaths.h"

#include <ekos/ekosmanager.h>

#include "Options.h"

const QStringList RAWFormats = { "cr2", "crw", "nef", "raf", "dng", "arw" };

namespace ISD
{
CCDChip::CCDChip(ISD::CCD *ccd, ChipType cType)
{
    baseDevice    = ccd->getBaseDevice();
    clientManager = ccd->getDriverInfo()->getClientManager();
    parentCCD     = ccd;
    type          = cType;
    batchMode     = false;
    displayFITS   = true;
    CanBin        = false;
    CanSubframe   = false;
    CanAbort      = false;
    imageData     = nullptr;

    captureMode   = FITS_NORMAL;
    captureFilter = FITS_NONE;

    //fx=fy=fw=fh=0;

    normalImage = focusImage = guideImage = calibrationImage = nullptr;
}

FITSView *CCDChip::getImageView(FITSMode imageType)
{
    switch (imageType)
    {
        case FITS_NORMAL:
            return normalImage;
            break;

        case FITS_FOCUS:
            return focusImage;
            break;

        case FITS_GUIDE:
            return guideImage;
            break;

        case FITS_CALIBRATE:
            return calibrationImage;
            break;

        case FITS_ALIGN:
            return alignImage;
            break;

        default:
            break;
    }

    return nullptr;
}

void CCDChip::setImageView(FITSView *image, FITSMode imageType)
{
    switch (imageType)
    {
        case FITS_NORMAL:
            normalImage = image;
            break;

        case FITS_FOCUS:
            focusImage = image;
            break;

        case FITS_GUIDE:
            guideImage = image;
            break;

        case FITS_CALIBRATE:
            calibrationImage = image;
            break;

        case FITS_ALIGN:
            alignImage = image;
            break;

        default:
            break;
    }

    if (image)
        imageData = image->getImageData();
}

bool CCDChip::getFrameMinMax(int *minX, int *maxX, int *minY, int *maxY, int *minW, int *maxW, int *minH, int *maxH)
{
    INumberVectorProperty *frameProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            frameProp = baseDevice->getNumber("CCD_FRAME");
            break;

        case GUIDE_CCD:
            frameProp = baseDevice->getNumber("GUIDER_FRAME");
            break;
    }

    if (frameProp == nullptr)
        return false;

    INumber *arg = IUFindNumber(frameProp, "X");

    if (arg == nullptr)
        return false;

    if (minX)
        *minX = arg->min;
    if (maxX)
        *maxX = arg->max;

    arg = IUFindNumber(frameProp, "Y");

    if (arg == nullptr)
        return false;

    if (minY)
        *minY = arg->min;
    if (maxY)
        *maxY = arg->max;

    arg = IUFindNumber(frameProp, "WIDTH");

    if (arg == nullptr)
        return false;

    if (minW)
        *minW = arg->min;
    if (maxW)
        *maxW = arg->max;

    arg = IUFindNumber(frameProp, "HEIGHT");

    if (arg == nullptr)
        return false;

    if (minH)
        *minH = arg->min;
    if (maxH)
        *maxH = arg->max;

    return true;
}

bool CCDChip::setImageInfo(uint16_t width, uint16_t height, double pixelX, double pixelY, uint8_t bitdepth)
{
    INumberVectorProperty *ccdInfoProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            ccdInfoProp = baseDevice->getNumber("CCD_INFO");
            break;

        case GUIDE_CCD:
            ccdInfoProp = baseDevice->getNumber("GUIDER_INFO");
            break;
    }

    if (ccdInfoProp == nullptr)
        return false;

    ccdInfoProp->np[0].value = width;
    ccdInfoProp->np[1].value = height;
    ccdInfoProp->np[2].value = std::hypotf(pixelX, pixelY);
    ccdInfoProp->np[3].value = pixelX;
    ccdInfoProp->np[4].value = pixelY;
    ccdInfoProp->np[5].value = bitdepth;

    clientManager->sendNewNumber(ccdInfoProp);

    return true;
}

bool CCDChip::getPixelSize(double &x, double &y)
{
    INumberVectorProperty *ccdInfoProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            ccdInfoProp = baseDevice->getNumber("CCD_INFO");
            break;

        case GUIDE_CCD:
            ccdInfoProp = baseDevice->getNumber("GUIDER_INFO");
            break;
    }

    if (ccdInfoProp == nullptr)
        return false;

    INumber *pixelX = IUFindNumber(ccdInfoProp, "CCD_PIXEL_SIZE_X");
    INumber *pixelY = IUFindNumber(ccdInfoProp, "CCD_PIXEL_SIZE_Y");

    if (pixelX == nullptr || pixelY == nullptr)
        return false;

    x = pixelX->value;
    y = pixelY->value;

    return true;
}

bool CCDChip::getFrame(int *x, int *y, int *w, int *h)
{
    INumberVectorProperty *frameProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            frameProp = baseDevice->getNumber("CCD_FRAME");
            break;

        case GUIDE_CCD:
            frameProp = baseDevice->getNumber("GUIDER_FRAME");
            break;
    }

    if (frameProp == nullptr)
        return false;

    INumber *arg = IUFindNumber(frameProp, "X");

    if (arg == nullptr)
        return false;

    *x = arg->value;

    arg = IUFindNumber(frameProp, "Y");
    if (arg == nullptr)
        return false;

    *y = arg->value;

    arg = IUFindNumber(frameProp, "WIDTH");
    if (arg == nullptr)
        return false;

    *w = arg->value;

    arg = IUFindNumber(frameProp, "HEIGHT");
    if (arg == nullptr)
        return false;

    *h = arg->value;

    return true;
}

bool CCDChip::resetFrame()
{
    INumberVectorProperty *frameProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            frameProp = baseDevice->getNumber("CCD_FRAME");
            break;

        case GUIDE_CCD:
            frameProp = baseDevice->getNumber("GUIDER_FRAME");
            break;
    }

    if (frameProp == nullptr)
        return false;

    INumber *xarg = IUFindNumber(frameProp, "X");
    INumber *yarg = IUFindNumber(frameProp, "Y");
    INumber *warg = IUFindNumber(frameProp, "WIDTH");
    INumber *harg = IUFindNumber(frameProp, "HEIGHT");

    if (xarg && yarg && warg && harg)
    {
        if (xarg->value == xarg->min && yarg->value == yarg->min && warg->value == warg->max &&
            harg->value == harg->max)
            return false;

        xarg->value = xarg->min;
        yarg->value = yarg->min;
        warg->value = warg->max;
        harg->value = harg->max;

        clientManager->sendNewNumber(frameProp);
        return true;
    }

    return false;
}

bool CCDChip::setFrame(int x, int y, int w, int h)
{
    INumberVectorProperty *frameProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            frameProp = baseDevice->getNumber("CCD_FRAME");
            break;

        case GUIDE_CCD:
            frameProp = baseDevice->getNumber("GUIDER_FRAME");
            break;
    }

    if (frameProp == nullptr)
        return false;

    INumber *xarg = IUFindNumber(frameProp, "X");
    INumber *yarg = IUFindNumber(frameProp, "Y");
    INumber *warg = IUFindNumber(frameProp, "WIDTH");
    INumber *harg = IUFindNumber(frameProp, "HEIGHT");

    if (xarg && yarg && warg && harg)
    {
        if (xarg->value == x && yarg->value == y && warg->value == w && harg->value == h)
            return true;

        xarg->value = x;
        yarg->value = y;
        warg->value = w;
        harg->value = h;

        clientManager->sendNewNumber(frameProp);
        return true;
    }

    return false;
}

bool CCDChip::capture(double exposure)
{
    INumberVectorProperty *expProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            expProp = baseDevice->getNumber("CCD_EXPOSURE");
            break;

        case GUIDE_CCD:
            expProp = baseDevice->getNumber("GUIDER_EXPOSURE");
            break;
    }

    if (expProp == nullptr)
        return false;

    expProp->np[0].value = exposure;

    clientManager->sendNewNumber(expProp);

    return true;
}

bool CCDChip::abortExposure()
{
    ISwitchVectorProperty *abortProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            abortProp = baseDevice->getSwitch("CCD_ABORT_EXPOSURE");
            break;

        case GUIDE_CCD:
            abortProp = baseDevice->getSwitch("GUIDER_ABORT_EXPOSURE");
            break;
    }

    if (abortProp == nullptr)
        return false;

    ISwitch *abort = IUFindSwitch(abortProp, "ABORT");

    if (abort == nullptr)
        return false;

    abort->s = ISS_ON;

    //captureMode = FITS_NORMAL;

    clientManager->sendNewSwitch(abortProp);

    return true;
}
bool CCDChip::canBin() const
{
    return CanBin;
}

void CCDChip::setCanBin(bool value)
{
    CanBin = value;
}
bool CCDChip::canSubframe() const
{
    return CanSubframe;
}

void CCDChip::setCanSubframe(bool value)
{
    CanSubframe = value;
}
bool CCDChip::canAbort() const
{
    return CanAbort;
}

/*bool CCDChip::getFocusFrame(int *x, int *y, int *w, int *h)
{
    *x = fx;
    *y = fy;
    *w = fw;
    *h = fh;

    return true;
}

bool CCDChip::setFocusFrame(int x, int y, int w, int h)
{
    fx=x;
    fy=y;
    fw=w;
    fh=h;

    return true;
}*/

void CCDChip::setCanAbort(bool value)
{
    CanAbort = value;
}

FITSData *CCDChip::getImageData() const
{
    return imageData;
}

void CCDChip::setImageData(FITSData *value)
{
    imageData = value;
}
int CCDChip::getISOIndex() const
{
    ISwitchVectorProperty *isoProp = baseDevice->getSwitch("CCD_ISO");

    if (isoProp == nullptr)
        return -1;

    return IUFindOnSwitchIndex(isoProp);
}

bool CCDChip::setISOIndex(int value)
{
    ISwitchVectorProperty *isoProp = baseDevice->getSwitch("CCD_ISO");

    if (isoProp == nullptr)
        return false;

    IUResetSwitch(isoProp);
    isoProp->sp[value].s = ISS_ON;

    clientManager->sendNewSwitch(isoProp);

    return true;
}

QStringList CCDChip::getISOList() const
{
    QStringList isoList;

    ISwitchVectorProperty *isoProp = baseDevice->getSwitch("CCD_ISO");

    if (isoProp == nullptr)
        return isoList;

    for (int i = 0; i < isoProp->nsp; i++)
        isoList << isoProp->sp[i].label;

    return isoList;
}

bool CCDChip::isCapturing()
{
    INumberVectorProperty *expProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            expProp = baseDevice->getNumber("CCD_EXPOSURE");
            break;

        case GUIDE_CCD:
            expProp = baseDevice->getNumber("GUIDER_EXPOSURE");
            break;
    }

    if (expProp == nullptr)
        return false;

    return (expProp->s == IPS_BUSY);
}

bool CCDChip::setFrameType(const QString &name)
{
    CCDFrameType fType = FRAME_LIGHT;

    if (name == "FRAME_LIGHT" || name == "Light")
        fType = FRAME_LIGHT;
    else if (name == "FRAME_DARK" || name == "Dark")
        fType = FRAME_DARK;
    else if (name == "FRAME_BIAS" || name == "Bias")
        fType = FRAME_BIAS;
    else if (name == "FRAME_FLAT" || name == "Flat")
        fType = FRAME_FLAT;
    else
    {
        qDebug() << name << " frame type is unknown." << endl;
        return false;
    }

    return setFrameType(fType);
}

bool CCDChip::setFrameType(CCDFrameType fType)
{
    ISwitchVectorProperty *frameProp = nullptr;

    if (type == PRIMARY_CCD)
        frameProp = baseDevice->getSwitch("CCD_FRAME_TYPE");
    else
        frameProp = baseDevice->getSwitch("GUIDER_FRAME_TYPE");
    if (frameProp == nullptr)
        return false;

    ISwitch *ccdFrame = nullptr;

    if (fType == FRAME_LIGHT)
        ccdFrame = IUFindSwitch(frameProp, "FRAME_LIGHT");
    else if (fType == FRAME_DARK)
        ccdFrame = IUFindSwitch(frameProp, "FRAME_DARK");
    else if (fType == FRAME_BIAS)
        ccdFrame = IUFindSwitch(frameProp, "FRAME_BIAS");
    else if (fType == FRAME_FLAT)
        ccdFrame = IUFindSwitch(frameProp, "FRAME_FLAT");

    if (ccdFrame == nullptr)
        return false;

    if (ccdFrame->s == ISS_ON)
        return true;

    if (fType != FRAME_LIGHT)
        captureMode = FITS_CALIBRATE;

    IUResetSwitch(frameProp);
    ccdFrame->s = ISS_ON;

    clientManager->sendNewSwitch(frameProp);

    return true;
}

CCDFrameType CCDChip::getFrameType()
{
    CCDFrameType fType               = FRAME_LIGHT;
    ISwitchVectorProperty *frameProp = nullptr;

    if (type == PRIMARY_CCD)
        frameProp = baseDevice->getSwitch("CCD_FRAME_TYPE");
    else
        frameProp = baseDevice->getSwitch("GUIDER_FRAME_TYPE");

    if (frameProp == nullptr)
        return fType;

    ISwitch *ccdFrame = nullptr;

    ccdFrame = IUFindOnSwitch(frameProp);

    if (ccdFrame == nullptr)
    {
        qDebug() << "ISD:CCD Cannot find active frame in CCD!" << endl;
        return fType;
    }

    if (!strcmp(ccdFrame->name, "FRAME_LIGHT"))
        fType = FRAME_LIGHT;
    else if (!strcmp(ccdFrame->name, "FRAME_DARK"))
        fType = FRAME_DARK;
    else if (!strcmp(ccdFrame->name, "FRAME_FLAT"))
        fType = FRAME_FLAT;
    else if (!strcmp(ccdFrame->name, "FRAME_BIAS"))
        fType = FRAME_BIAS;

    return fType;
}

bool CCDChip::setBinning(CCDBinType binType)
{
    switch (binType)
    {
        case SINGLE_BIN:
            return setBinning(1, 1);
            break;
        case DOUBLE_BIN:
            return setBinning(2, 2);
            break;
        case TRIPLE_BIN:
            return setBinning(3, 3);
            break;
        case QUADRAPLE_BIN:
            return setBinning(4, 4);
            break;
    }

    return false;
}

CCDBinType CCDChip::getBinning()
{
    CCDBinType binType             = SINGLE_BIN;
    INumberVectorProperty *binProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            binProp = baseDevice->getNumber("CCD_BINNING");
            break;

        case GUIDE_CCD:
            binProp = baseDevice->getNumber("GUIDER_BINNING");
            break;
    }

    if (binProp == nullptr)
        return binType;

    INumber *horBin = nullptr, *verBin = nullptr;

    horBin = IUFindNumber(binProp, "HOR_BIN");
    verBin = IUFindNumber(binProp, "VER_BIN");

    if (!horBin || !verBin)
        return binType;

    switch ((int)horBin->value)
    {
        case 2:
            binType = DOUBLE_BIN;
            break;

        case 3:
            binType = TRIPLE_BIN;
            break;

        case 4:
            binType = QUADRAPLE_BIN;
            break;

        default:
            break;
    }

    return binType;
}

bool CCDChip::getBinning(int *bin_x, int *bin_y)
{
    INumberVectorProperty *binProp = nullptr;
    *bin_x = *bin_y = 1;

    switch (type)
    {
        case PRIMARY_CCD:
            binProp = baseDevice->getNumber("CCD_BINNING");
            break;

        case GUIDE_CCD:
            binProp = baseDevice->getNumber("GUIDER_BINNING");
            break;
    }

    if (binProp == nullptr)
        return false;

    INumber *horBin = nullptr, *verBin = nullptr;

    horBin = IUFindNumber(binProp, "HOR_BIN");
    verBin = IUFindNumber(binProp, "VER_BIN");

    if (!horBin || !verBin)
        return false;

    *bin_x = horBin->value;
    *bin_y = verBin->value;

    return true;
}

bool CCDChip::getMaxBin(int *max_xbin, int *max_ybin)
{
    if (!max_xbin || !max_ybin)
        return false;

    INumberVectorProperty *binProp = nullptr;

    *max_xbin = *max_ybin = 1;

    switch (type)
    {
        case PRIMARY_CCD:
            binProp = baseDevice->getNumber("CCD_BINNING");
            break;

        case GUIDE_CCD:
            binProp = baseDevice->getNumber("GUIDER_BINNING");
            break;
    }

    if (binProp == nullptr)
        return false;

    INumber *horBin = nullptr, *verBin = nullptr;

    horBin = IUFindNumber(binProp, "HOR_BIN");
    verBin = IUFindNumber(binProp, "VER_BIN");

    if (!horBin || !verBin)
        return false;

    *max_xbin = horBin->max;
    *max_ybin = verBin->max;

    return true;
}

bool CCDChip::setBinning(int bin_x, int bin_y)
{
    INumberVectorProperty *binProp = nullptr;

    switch (type)
    {
        case PRIMARY_CCD:
            binProp = baseDevice->getNumber("CCD_BINNING");
            break;

        case GUIDE_CCD:
            binProp = baseDevice->getNumber("GUIDER_BINNING");
            break;
    }

    if (binProp == nullptr)
        return false;

    INumber *horBin = nullptr, *verBin = nullptr;

    horBin = IUFindNumber(binProp, "HOR_BIN");
    verBin = IUFindNumber(binProp, "VER_BIN");

    if (!horBin || !verBin)
        return false;

    if (horBin->value == bin_x && verBin->value == bin_y)
        return true;

    if (bin_x > horBin->max || bin_y > verBin->max)
        return false;

    horBin->value = bin_x;
    verBin->value = bin_y;

    clientManager->sendNewNumber(binProp);

    return true;
}

CCD::CCD(GDInterface *iPtr) : DeviceDecorator(iPtr)
{
    dType            = KSTARS_CCD;
    ISOMode          = true;
    HasGuideHead     = false;
    HasCooler        = false;
    HasCoolerControl = false;
    HasVideoStream   = false;
    fv               = nullptr;
    streamWindow     = nullptr;
    ST4Driver        = nullptr;
    nextSequenceID   = 0;

    primaryChip = new CCDChip(this, CCDChip::PRIMARY_CCD);

    normalTabID = calibrationTabID = focusTabID = guideTabID = alignTabID = -1;
    guideChip                                                             = nullptr;

    transferFormat = targetTransferFormat = FORMAT_FITS;
}

CCD::~CCD()
{
#ifdef HAVE_CFITSIO
    delete (fv);
#endif
    delete (primaryChip);
    delete (guideChip);
    delete (streamWindow);
}

void CCD::registerProperty(INDI::Property *prop)
{
    if (!strcmp(prop->getName(), "GUIDER_EXPOSURE"))
    {
        HasGuideHead = true;
        guideChip    = new CCDChip(this, CCDChip::GUIDE_CCD);
    }
    else if (!strcmp(prop->getName(), "CCD_FRAME_TYPE"))
    {
        ISwitchVectorProperty *ccdFrame = prop->getSwitch();

        primaryChip->clearFrameTypes();

        for (int i = 0; i < ccdFrame->nsp; i++)
            primaryChip->addFrameLabel(ccdFrame->sp[i].label);
    }
    else if (!strcmp(prop->getName(), "CCD_FRAME"))
    {
        INumberVectorProperty *np = prop->getNumber();
        if (np && np->p != IP_RO)
            primaryChip->setCanSubframe(true);
    }
    else if (!strcmp(prop->getName(), "GUIDER_FRAME"))
    {
        INumberVectorProperty *np = prop->getNumber();
        if (np && np->p != IP_RO)
            guideChip->setCanSubframe(true);
    }
    else if (!strcmp(prop->getName(), "CCD_BINNING"))
    {
        INumberVectorProperty *np = prop->getNumber();
        if (np && np->p != IP_RO)
            primaryChip->setCanBin(true);
    }
    else if (!strcmp(prop->getName(), "GUIDER_BINNING"))
    {
        INumberVectorProperty *np = prop->getNumber();
        if (np && np->p != IP_RO)
            guideChip->setCanBin(true);
    }
    else if (!strcmp(prop->getName(), "CCD_ABORT_EXPOSURE"))
    {
        ISwitchVectorProperty *sp = prop->getSwitch();
        if (sp && sp->p != IP_RO)
            primaryChip->setCanAbort(true);
    }
    else if (!strcmp(prop->getName(), "GUIDER_ABORT_EXPOSURE"))
    {
        ISwitchVectorProperty *sp = prop->getSwitch();
        if (sp && sp->p != IP_RO)
            guideChip->setCanAbort(true);
    }
    else if (!strcmp(prop->getName(), "CCD_TEMPERATURE"))
    {
        INumberVectorProperty *np = prop->getNumber();
        HasCooler                 = true;
        if (np)
            emit newTemperatureValue(np->np[0].value);
    }
    else if (!strcmp(prop->getName(), "CCD_COOLER"))
    {
        // Can turn cooling on/off
        HasCoolerControl = true;
    }
    else if (!strcmp(prop->getName(), "CCD_VIDEO_STREAM"))
    {
        // Has Video Stream
        HasVideoStream = true;
    }
    else if (!strcmp(prop->getName(), "CCD_TRANSFER_FORMAT"))
    {
        ISwitchVectorProperty *sp = prop->getSwitch();
        if (sp)
        {
            ISwitch *format = IUFindSwitch(sp, "FORMAT_NATIVE");
            if (format && format->s == ISS_ON)
                transferFormat = FORMAT_NATIVE;
            else
                transferFormat = FORMAT_FITS;
        }
    }
    else if (!strcmp(prop->getName(), "TELESCOPE_TYPE"))
    {
        ISwitchVectorProperty *sp = prop->getSwitch();
        if (sp)
        {
            ISwitch *format = IUFindSwitch(sp, "TELESCOPE_PRIMARY");
            if (format && format->s == ISS_ON)
                telescopeType = TELESCOPE_PRIMARY;
            else
                telescopeType = TELESCOPE_GUIDE;
        }
    }
    // try to find gain property, if any
    else if (gainN == nullptr && prop->getType() == INDI_NUMBER)
    {
        // Since gain is spread among multiple property depending on the camera providing it
        // we need to search in all possible number properties
        INumberVectorProperty *gainNP = prop->getNumber();
        if (gainNP)
        {
            for (int i = 0; i < gainNP->nnp; i++)
            {
                QString name  = QString(gainNP->np[i].name).toLower();
                QString label = QString(gainNP->np[i].label).toLower();

                if (name == "gain" || label == "gain")
                {
                    gainN = gainNP->np + i;
                    break;
                }
            }
        }
    }

    DeviceDecorator::registerProperty(prop);
}

void CCD::processLight(ILightVectorProperty *lvp)
{
    DeviceDecorator::processLight(lvp);
}

void CCD::processNumber(INumberVectorProperty *nvp)
{
    if (!strcmp(nvp->name, "CCD_EXPOSURE"))
    {
        INumber *np = IUFindNumber(nvp, "CCD_EXPOSURE_VALUE");
        if (np)
            emit newExposureValue(primaryChip, np->value, nvp->s);
    }
    else if (!strcmp(nvp->name, "CCD_TEMPERATURE"))
    {
        HasCooler   = true;
        INumber *np = IUFindNumber(nvp, "CCD_TEMPERATURE_VALUE");
        if (np)
            emit newTemperatureValue(np->value);
    }
    else if (!strcmp(nvp->name, "GUIDER_EXPOSURE"))
    {
        INumber *np = IUFindNumber(nvp, "GUIDER_EXPOSURE_VALUE");
        if (np)
            emit newExposureValue(guideChip, np->value, nvp->s);
    }
    else if (!strcmp(nvp->name, "FPS"))
    {
        emit newFPS(nvp->np[0].value, nvp->np[1].value);
    }
    else if (!strcmp(nvp->name, "CCD_RAPID_GUIDE_DATA"))
    {
        double dx = -1, dy = -1, fit = -1;
        INumber *np = nullptr;

        if (nvp->s == IPS_ALERT)
        {
            emit newGuideStarData(primaryChip, -1, -1, -1);
        }
        else
        {
            np = IUFindNumber(nvp, "GUIDESTAR_X");
            if (np)
                dx = np->value;
            np = IUFindNumber(nvp, "GUIDESTAR_Y");
            if (np)
                dy = np->value;
            np = IUFindNumber(nvp, "GUIDESTAR_FIT");
            if (np)
                fit = np->value;

            if (dx >= 0 && dy >= 0 && fit >= 0)
                emit newGuideStarData(primaryChip, dx, dy, fit);
        }
    }
    else if (!strcmp(nvp->name, "GUIDER_RAPID_GUIDE_DATA"))
    {
        double dx = -1, dy = -1, fit = -1;
        INumber *np = nullptr;

        if (nvp->s == IPS_ALERT)
        {
            emit newGuideStarData(guideChip, -1, -1, -1);
        }
        else
        {
            np = IUFindNumber(nvp, "GUIDESTAR_X");
            if (np)
                dx = np->value;
            np = IUFindNumber(nvp, "GUIDESTAR_Y");
            if (np)
                dy = np->value;
            np = IUFindNumber(nvp, "GUIDESTAR_FIT");
            if (np)
                fit = np->value;

            if (dx >= 0 && dy >= 0 && fit >= 0)
                emit newGuideStarData(guideChip, dx, dy, fit);
        }
    }

    DeviceDecorator::processNumber(nvp);
}

void CCD::processSwitch(ISwitchVectorProperty *svp)
{
    if (!strcmp(svp->name, "CCD_COOLER"))
    {
        // Can turn cooling on/off
        HasCoolerControl = true;
    }
    else if (QString(svp->name).endsWith("VIDEO_STREAM"))
    {
        HasVideoStream = true;

        if (streamWindow == nullptr && svp->sp[0].s == ISS_ON)
        {
            streamWindow = new StreamWG(this);

            INumberVectorProperty *streamFrame = baseDevice->getNumber("CCD_STREAM_FRAME");
            INumber *w = nullptr, *h = nullptr;
            if (streamFrame)
            {
                w = IUFindNumber(streamFrame, "WIDTH");
                h = IUFindNumber(streamFrame, "HEIGHT");
            }

            if (w && h)
            {
                streamW = w->value;
                streamH = h->value;
            }
            else
            {
                // Only use CCD dimensions if we are receing raw stream and not stream of images (i.e. mjpeg..etc)
                IBLOBVectorProperty *rawBP = baseDevice->getBLOB("CCD1");
                if (rawBP)
                {
                    int x, y, w, h;
                    int binx, biny;

                    primaryChip->getFrame(&x, &y, &w, &h);
                    primaryChip->getBinning(&binx, &biny);
                    streamW = w / binx;
                    streamH = h / biny;
                }
            }

            streamWindow->setSize(streamW, streamH);
        }

        if (streamWindow)
        {
            connect(streamWindow, SIGNAL(hidden()), this, SLOT(StreamWindowHidden()), Qt::UniqueConnection);

            streamWindow->enableStream(svp->sp[0].s == ISS_ON);
            emit videoStreamToggled(svp->sp[0].s == ISS_ON);
        }
    }
    else if (!strcmp(svp->name, "CCD_TRANSFER_FORMAT"))
    {
        ISwitch *format = IUFindSwitch(svp, "FORMAT_NATIVE");

        if (format && format->s == ISS_ON)
            transferFormat = FORMAT_NATIVE;
        else
            transferFormat = FORMAT_FITS;
    }
    else if (!strcmp(svp->name, "RECORD_STREAM"))
    {
        ISwitch *recordOFF = IUFindSwitch(svp, "RECORD_OFF");

        if (recordOFF && recordOFF->s == ISS_ON)
        {
            emit videoRecordToggled(false);
            KNotification::event(QLatin1String("RecordingStopped"), i18n("Video Recording Stopped"));
        }
        else
        {
            emit videoRecordToggled(true);
            KNotification::event(QLatin1String("RecordingStarted"), i18n("Video Recording Started"));
        }
    }
    else if (!strcmp(svp->name, "TELESCOPE_TYPE"))
    {
        ISwitch *format = IUFindSwitch(svp, "TELESCOPE_PRIMARY");
        if (format && format->s == ISS_ON)
            telescopeType = TELESCOPE_PRIMARY;
        else
            telescopeType = TELESCOPE_GUIDE;
    }
    else if (!strcmp(svp->name, "CONNECTION"))
    {
        ISwitch *dSwitch = IUFindSwitch(svp, "DISCONNECT");

        if (dSwitch && dSwitch->s == ISS_ON && streamWindow != nullptr)
        {
            streamWindow->enableStream(false);
            emit videoStreamToggled(false);
            streamWindow->close();
            delete (streamWindow);
            streamWindow = nullptr;
        }

        //emit switchUpdated(svp);
        //return;
    }

    DeviceDecorator::processSwitch(svp);
}

void CCD::processText(ITextVectorProperty *tvp)
{
    if (!strcmp(tvp->name, "CCD_FILE_PATH"))
    {
        IText *filepath = IUFindText(tvp, "FILE_PATH");
        if (filepath)
            emit newRemoteFile(QString(filepath->text));
    }

    DeviceDecorator::processText(tvp);
}

void CCD::processBLOB(IBLOB *bp)
{
    // Ignore write-only BLOBs since we only receive it for state-change
    if (bp->bvp->p == IP_WO)
        return;

    BType = BLOB_OTHER;

    QString format(bp->format);

    // If stream, process it first
    if (format.contains("stream") && streamWindow)
    {
        if (streamWindow->isStreamEnabled() == false)
            return;

        INumberVectorProperty *streamFrame = baseDevice->getNumber("CCD_STREAM_FRAME");
        INumber *w = nullptr, *h = nullptr;

        if (streamFrame)
        {
            w = IUFindNumber(streamFrame, "WIDTH");
            h = IUFindNumber(streamFrame, "HEIGHT");
        }

        if (w && h)
        {
            streamW = w->value;
            streamH = h->value;
        }
        else
        {
            int x, y, w, h;
            int binx, biny;

            primaryChip->getFrame(&x, &y, &w, &h);
            primaryChip->getBinning(&binx, &biny);
            streamW = w / binx;
            streamH = h / biny;

            /*IBLOBVectorProperty *rawBP = baseDevice->getBLOB("CCD1");
            if (rawBP)
            {
                rawBP->bp[0].aux0 = &(streamW);
                rawBP->bp[0].aux1 = &(streamH);
            }*/
        }

        //if (streamWindow->getStreamWidth() != streamW || streamWindow->getStreamHeight() != streamH)

        streamWindow->setSize(streamW, streamH);

        streamWindow->show();
        streamWindow->newFrame(bp);
        return;
    }

    QByteArray fmt = QString(bp->format).toLower().remove(".").toUtf8();

    // If it's not FITS or an image, don't process it.
    if ((QImageReader::supportedImageFormats().contains(fmt)))
        BType = BLOB_IMAGE;
    else if (format.contains("fits"))
        BType = BLOB_FITS;
    else if (RAWFormats.contains(fmt))
        BType = BLOB_RAW;

    if (BType == BLOB_OTHER)
    {
        DeviceDecorator::processBLOB(bp);
        return;
    }

    CCDChip *targetChip = nullptr;

    if (!strcmp(bp->name, "CCD2"))
        targetChip = guideChip;
    else
        targetChip = primaryChip;

    QString currentDir;

    if (targetChip->isBatchMode() == false)
        currentDir = KSPaths::writableLocation(QStandardPaths::TempLocation);
    else
        currentDir = fitsDir.isEmpty() ? Options::fitsDir() : fitsDir;

    int nr, n = 0;
    QTemporaryFile tmpFile(QDir::tempPath() + "/fitsXXXXXX");

    //if (currentDir.endsWith('/'))
    //currentDir.truncate(currentDir.size()-1);

    if (QDir(currentDir).exists() == false)
        QDir().mkpath(currentDir);

    QString filename(currentDir);
    if (filename.endsWith('/') == false)
        filename.append('/');

    // Create temporary name if ANY of the following conditions are met:
    // 1. file is preview or batch mode is not enabled
    // 2. file type is not FITS_NORMAL (focus, guide..etc)
    if (targetChip->isBatchMode() == false || targetChip->getCaptureMode() != FITS_NORMAL)
    {
        //tmpFile.setPrefix("fits");
        tmpFile.setAutoRemove(false);

        if (!tmpFile.open())
        {
            qDebug() << "ISD:CCD Error: Unable to open " << filename << endl;
            emit BLOBUpdated(nullptr);
            return;
        }

        QDataStream out(&tmpFile);

        for (nr = 0; nr < (int)bp->size; nr += n)
            n = out.writeRawData(static_cast<char *>(bp->blob) + nr, bp->size - nr);

        tmpFile.close();

        filename = tmpFile.fileName();
    }
    // Create file name for others
    else
    {
        // IS8601 contains colons but they are illegal under Windows OS, so replacing them with '-'
        // The timestamp is no longer ISO8601 but it should solve interoperality issues between different OS hosts
        QString ts = QDateTime::currentDateTime().toString("yyyy-MM-ddThh-mm-ss");

        if (seqPrefix.contains("_ISO8601"))
        {
            QString finalPrefix = seqPrefix;
            finalPrefix.replace("ISO8601", ts);
            filename += finalPrefix + QString("_%1.%2").arg(QString().sprintf("%03d", nextSequenceID), QString(fmt));
        }
        else
            filename += seqPrefix + (seqPrefix.isEmpty() ? "" : "_") +
                        QString("%1.%2").arg(QString().sprintf("%03d", nextSequenceID)).arg(QString(fmt));

        QFile fits_temp_file(filename);
        if (!fits_temp_file.open(QIODevice::WriteOnly))
        {
            qDebug() << "ISD:CCD Error: Unable to open " << fits_temp_file.fileName() << endl;
            emit BLOBUpdated(nullptr);
            return;
        }

        QDataStream out(&fits_temp_file);

        for (nr = 0; nr < (int)bp->size; nr += n)
            n = out.writeRawData(static_cast<char *>(bp->blob) + nr, bp->size - nr);

        fits_temp_file.close();
    }

    if (BType == BLOB_FITS)
        addFITSKeywords(filename);

    // store file name
    strncpy(BLOBFilename, filename.toLatin1(), MAXINDIFILENAME);
    bp->aux1 = &BType;
    bp->aux2 = BLOBFilename;

    if (targetChip->getCaptureMode() == FITS_NORMAL && targetChip->isBatchMode() == true)
        KStars::Instance()->statusBar()->showMessage(i18n("%1 file saved to %2", QString(fmt).toUpper(), filename), 0);

    // FIXME: Why is this leaking memory in Valgrind??!
    KNotification::event(QLatin1String("FITSReceived"), i18n("Image file is received"));

    /*if (targetChip->showFITS() == false && targetChip->getCaptureMode() == FITS_NORMAL)
    {
        emit BLOBUpdated(bp);
        return;
    }*/

    if (BType == BLOB_IMAGE || BType == BLOB_RAW)
    {
        if (BType == BLOB_RAW)
        {
#ifdef HAVE_LIBRAW

            QString rawFileName  = filename;
            rawFileName          = rawFileName.remove(0, rawFileName.lastIndexOf(QLatin1Literal("/")));
            QString templateName = QString("%1/%2.XXXXXX").arg(QDir::tempPath()).arg(rawFileName);
            QTemporaryFile imgPreview(templateName);
            imgPreview.setAutoRemove(false);
            imgPreview.open();
            imgPreview.close();
            QString preview_filename = imgPreview.fileName();

            int ret = 0;
            // Creation of image processing object
            LibRaw RawProcessor;

            // Let us open the file
            if ((ret = RawProcessor.open_file(filename.toLatin1().data())) != LIBRAW_SUCCESS)
            {
                KStars::Instance()->statusBar()->showMessage(
                    i18n("Cannot open %1: %2", rawFileName, libraw_strerror(ret)));
                RawProcessor.recycle();
                emit BLOBUpdated(bp);
                return;
            }

            // Let us unpack the image
            /*if( (ret = RawProcessor.unpack() ) != LIBRAW_SUCCESS)
            {
                KStars::Instance()->statusBar()->showMessage(i18n("Cannot unpack_thumb %1: %2", rawFileName, libraw_strerror(ret)));
                if(LIBRAW_FATAL_ERROR(ret))
                {
                    RawProcessor.recycle();
                    emit BLOBUpdated(bp);
                    return;
                }
                // if there has been a non-fatal error, we will try to continue
            }*/

            // Let us unpack the thumbnail
            if ((ret = RawProcessor.unpack_thumb()) != LIBRAW_SUCCESS)
            {
                KStars::Instance()->statusBar()->showMessage(
                    i18n("Cannot unpack_thumb %1: %2", rawFileName, libraw_strerror(ret)));
                RawProcessor.recycle();
                emit BLOBUpdated(bp);
                return;
            }
            else
            // We have successfully unpacked the thumbnail, now let us write it to a file
            {
                //snprintf(thumbfn,sizeof(thumbfn),"%s.%s",av[i],T.tformat == LIBRAW_THUMBNAIL_JPEG ? "thumb.jpg" : "thumb.ppm");
                if (LIBRAW_SUCCESS != (ret = RawProcessor.dcraw_thumb_writer(preview_filename.toLatin1().data())))
                {
                    KStars::Instance()->statusBar()->showMessage(
                        i18n("Cannot write %s %1: %2", preview_filename, libraw_strerror(ret)));
                    RawProcessor.recycle();
                    emit BLOBUpdated(bp);
                    return;
                }
            }

            filename = preview_filename;

#else
            // Silenty fail if KStars was not compiled with libraw
            //KStars::Instance()->statusBar()->showMessage(i18n("Unable to find dcraw and cjpeg. Please install the required tools to convert CR2/NEF to JPEG."));
            emit BLOBUpdated(bp);
            return;
#endif
        }

        // store file name in
        strncpy(BLOBFilename, filename.toLatin1(), MAXINDIFILENAME);
        bp->aux1 = &BType;
        bp->aux2 = BLOBFilename;

        if (imageViewer.isNull())
            imageViewer = new ImageViewer(getDeviceName(), KStars::Instance());

        imageViewer->loadImage(filename);
    }
// Unless we have cfitsio, we're done.
#ifdef HAVE_CFITSIO
    if (BType == BLOB_FITS)
    {
        QUrl fileURL = QUrl::fromLocalFile(filename);

        // If there is no FITSViewer, create it. Unless it is a dedicated Focus or Guide frame
        // then no need for a FITS Viewer as they get displayed inside Ekos
        if (Options::useFITSViewerInCapture() || !targetChip->isBatchMode())
        {
            if (fv.isNull() && targetChip->getCaptureMode() != FITS_GUIDE &&
                targetChip->getCaptureMode() != FITS_FOCUS && targetChip->getCaptureMode() != FITS_ALIGN)
            {
                normalTabID = calibrationTabID = focusTabID = guideTabID = alignTabID = -1;

                if (Options::singleWindowCapturedFITS())
                    fv = KStars::Instance()->genericFITSViewer();
                else
                {
                    fv = new FITSViewer(Options::independentWindowFITS() ? nullptr : KStars::Instance());
                    KStars::Instance()->getFITSViewersList().append(fv);
                }

                //connect(fv, SIGNAL(destroyed()), this, SLOT(FITSViewerDestroyed()));
                //connect(fv, SIGNAL(destroyed()), this, SIGNAL(FITSViewerClosed()));
            }
        }

        FITSScale captureFilter = targetChip->getCaptureFilter();

        QString previewTitle;

        bool preview = !targetChip->isBatchMode() && Options::singlePreviewFITS();
        if (preview)
        {
            if (Options::singleWindowCapturedFITS())
                previewTitle = i18n("%1 Preview", getDeviceName());
            else
                previewTitle = i18n("Preview");
        }

        int tabRC = -1;

        switch (targetChip->getCaptureMode())
        {
            case FITS_NORMAL:
            {
                //This is how to get the image to show up in the EkosManger preview FitsView.
                FITSView *previewView = KStars::Instance()->ekosManager()->getPreviewView();
                if (previewView)
                {
                    previewView->setFilter(captureFilter);
                    bool imageLoad = previewView->loadFITS(filename, true);
                    if (imageLoad)
                        previewView->updateFrame();
                }
                if (Options::useFITSViewerInCapture() || !targetChip->isBatchMode())
                {
                    if (normalTabID == -1 || Options::singlePreviewFITS() == false)
                        tabRC = fv->addFITS(&fileURL, FITS_NORMAL, captureFilter, previewTitle);
                    else if (fv->updateFITS(&fileURL, normalTabID, captureFilter) == false)
                    {
                        fv->removeFITS(normalTabID);
                        tabRC = fv->addFITS(&fileURL, FITS_NORMAL, captureFilter, previewTitle);
                    }
                    else
                        tabRC = normalTabID;

                    if (tabRC >= 0)
                    {
                        normalTabID = tabRC;
                        targetChip->setImageView(fv->getView(normalTabID), FITS_NORMAL);

                        emit newImage(fv->getView(normalTabID)->getDisplayImage(), targetChip);
                    }
                    else
                    {
                        // If opening file fails, we treat it the same as exposure failure and recapture again if possible
                        emit newExposureValue(targetChip, 0, IPS_ALERT);
                        return;
                    }
                }
                else
                {
                    if (previewView)
                    {
                        targetChip->setImageView(previewView, FITS_NORMAL);
                        emit newImage(previewView->getDisplayImage(), targetChip);
                    }
                }
            }
            break;

            case FITS_FOCUS:
                /*
                if (focusTabID == -1)
                    tabRC = fv->addFITS(&fileURL, FITS_FOCUS, captureFilter);
                else if (fv->updateFITS(&fileURL, focusTabID, captureFilter) == false)
                {
                    fv->removeFITS(focusTabID);
                    tabRC = fv->addFITS(&fileURL, FITS_FOCUS, captureFilter);
                }
                else
                    tabRC = focusTabID;

                if (tabRC >= 0)
                {
                    focusTabID = tabRC;
                    targetChip->setImageView(fv->getView(focusTabID), FITS_FOCUS);

                    emit newImage(fv->getView(focusTabID)->getDisplayImage(), targetChip);
                }
                else
                {
                    emit newExposureValue(targetChip, 0, IPS_ALERT);
                    // If there is problem loading image then BLOB is not valid so let's return
                    return;
                }
                */

                {
                    FITSView *focusView = targetChip->getImageView(FITS_FOCUS);
                    if (focusView)
                    {
                        focusView->setFilter(captureFilter);
                        bool imageLoad = focusView->loadFITS(filename, true);
                        if (imageLoad)
                        {
                            //focusView->rescale(ZOOM_FIT_WINDOW);
                            focusView->updateFrame();
                            emit newImage(focusView->getDisplayImage(), targetChip);
                        }
                        else
                        {
                            emit newExposureValue(targetChip, 0, IPS_ALERT);
                            return;
                        }
                    }
                }
                break;

            case FITS_GUIDE:
                /*
                if (guideTabID == -1)
                    tabRC = fv->addFITS(&fileURL, FITS_GUIDE, captureFilter);
                else if (fv->updateFITS(&fileURL, guideTabID, captureFilter) == false)
                {
                    fv->removeFITS(guideTabID);
                    tabRC = fv->addFITS(&fileURL, FITS_GUIDE, captureFilter);
                }
                else
                    tabRC = guideTabID;

                if (tabRC >= 0)
                {
                    guideTabID = tabRC;
                    targetChip->setImageView(fv->getView(guideTabID), FITS_GUIDE);

                    emit newImage(fv->getView(guideTabID)->getDisplayImage(), targetChip);
                }
                else
                {
                    emit newExposureValue(targetChip, 0, IPS_ALERT);
                    return;
                }*/
                {
                    FITSView *guideView = targetChip->getImageView(FITS_GUIDE);
                    if (guideView)
                    {
                        guideView->setFilter(captureFilter);
                        bool imageLoad = guideView->loadFITS(filename, true);
                        if (imageLoad)
                        {
                            //guideView->rescale(ZOOM_FIT_WINDOW);
                            guideView->updateFrame();
                            emit newImage(guideView->getDisplayImage(), targetChip);
                        }
                        else
                        {
                            emit newExposureValue(targetChip, 0, IPS_ALERT);
                            return;
                        }
                    }
                }
                break;

            case FITS_CALIBRATE:
                if (Options::useFITSViewerInCapture() || !targetChip->isBatchMode())
                {
                    if (calibrationTabID == -1)
                        tabRC = fv->addFITS(&fileURL, FITS_CALIBRATE, captureFilter);
                    else if (fv->updateFITS(&fileURL, calibrationTabID, captureFilter) == false)
                    {
                        fv->removeFITS(calibrationTabID);
                        tabRC = fv->addFITS(&fileURL, FITS_CALIBRATE, captureFilter);
                    }
                    else
                        tabRC = calibrationTabID;

                    if (tabRC >= 0)
                    {
                        calibrationTabID = tabRC;
                        targetChip->setImageView(fv->getView(calibrationTabID), FITS_CALIBRATE);
                    }
                    else
                    {
                        emit newExposureValue(targetChip, 0, IPS_ALERT);
                        return;
                    }
                }
                break;

            case FITS_ALIGN:
                /*
                if (alignTabID == -1)
                    tabRC = fv->addFITS(&fileURL, FITS_ALIGN, captureFilter);
                else if (fv->updateFITS(&fileURL, alignTabID, captureFilter) == false)
                {
                    fv->removeFITS(alignTabID);
                    tabRC = fv->addFITS(&fileURL, FITS_ALIGN, captureFilter);
                }
                else
                    tabRC = alignTabID;

                if (tabRC >= 0)
                {
                    alignTabID = tabRC;
                    targetChip->setImageView(fv->getView(alignTabID), FITS_ALIGN);
                }
                else
                {
                    emit newExposureValue(targetChip, 0, IPS_ALERT);
                    return;
                }*/

                {
                    FITSView *alignView = targetChip->getImageView(FITS_ALIGN);
                    if (alignView)
                    {
                        alignView->setFilter(captureFilter);
                        bool imageLoad = alignView->loadFITS(filename, true);
                        if (imageLoad)
                        {
                            //alignView->rescale(ZOOM_FIT_WINDOW);
                            alignView->updateFrame();
                            emit newImage(alignView->getDisplayImage(), targetChip);
                        }
                        else
                        {
                            emit newExposureValue(targetChip, 0, IPS_ALERT);
                            return;
                        }
                    }
                }
                break;

            default:
                break;
        }

        if (Options::useFITSViewerInCapture() || !targetChip->isBatchMode())
        {
            if (targetChip->getCaptureMode() == FITS_NORMAL || targetChip->getCaptureMode() == FITS_CALIBRATE)
                fv->show();
        }
    }
#endif

    emit BLOBUpdated(bp);
}

void CCD::addFITSKeywords(QString filename)
{
#ifdef HAVE_CFITSIO
    int status = 0;

    if (filter.isEmpty() == false)
    {
        QString key_comment("Filter name");
        filter.replace(" ", "_");

        fitsfile *fptr = nullptr;

        if (fits_open_image(&fptr, filename.toLatin1(), READWRITE, &status))
        {
            fits_report_error(stderr, status);
            return;
        }

        if (fits_update_key_str(fptr, "FILTER", filter.toLatin1().data(), key_comment.toLatin1().data(), &status))
        {
            fits_report_error(stderr, status);
            return;
        }

        fits_close_file(fptr, &status);

        filter = "";
    }
#endif
}

CCD::TransferFormat CCD::getTargetTransferFormat() const
{
    return targetTransferFormat;
}

void CCD::setTargetTransferFormat(const TransferFormat &value)
{
    targetTransferFormat = value;
}

void CCD::FITSViewerDestroyed()
{
    fv          = nullptr;
    normalTabID = calibrationTabID = focusTabID = guideTabID = alignTabID = -1;
}

void CCD::StreamWindowHidden()
{
    if (baseDevice->isConnected())
    {
        // We can have more than one *_VIDEO_STREAM property active so disable them all
        ISwitchVectorProperty *streamSP = baseDevice->getSwitch("CCD_VIDEO_STREAM");
        if (streamSP)
        {
            IUResetSwitch(streamSP);
            streamSP->sp[0].s = ISS_OFF;
            streamSP->sp[1].s = ISS_ON;
            streamSP->s       = IPS_IDLE;
            clientManager->sendNewSwitch(streamSP);
        }

        streamSP = baseDevice->getSwitch("VIDEO_STREAM");
        if (streamSP)
        {
            IUResetSwitch(streamSP);
            streamSP->sp[0].s = ISS_OFF;
            streamSP->sp[1].s = ISS_ON;
            streamSP->s       = IPS_IDLE;
            clientManager->sendNewSwitch(streamSP);
        }

        streamSP = baseDevice->getSwitch("AUX_VIDEO_STREAM");
        if (streamSP)
        {
            IUResetSwitch(streamSP);
            streamSP->sp[0].s = ISS_OFF;
            streamSP->sp[1].s = ISS_ON;
            streamSP->s       = IPS_IDLE;
            clientManager->sendNewSwitch(streamSP);
        }
    }

    if (streamWindow)
        streamWindow->disconnect();
}

bool CCD::hasGuideHead()
{
    return HasGuideHead;
}

bool CCD::hasCooler()
{
    return HasCooler;
}

bool CCD::hasCoolerControl()
{
    return HasCoolerControl;
}

bool CCD::setCoolerControl(bool enable)
{
    if (HasCoolerControl == false)
        return false;

    ISwitchVectorProperty *coolerSP = baseDevice->getSwitch("CCD_COOLER");

    if (coolerSP == nullptr)
        return false;

    // Cooler ON/OFF
    coolerSP->sp[0].s = enable ? ISS_ON : ISS_OFF;
    coolerSP->sp[1].s = enable ? ISS_OFF : ISS_ON;

    clientManager->sendNewSwitch(coolerSP);

    return true;
}

CCDChip *CCD::getChip(CCDChip::ChipType cType)
{
    switch (cType)
    {
        case CCDChip::PRIMARY_CCD:
            return primaryChip;
            break;

        case CCDChip::GUIDE_CCD:
            return guideChip;
            break;
    }

    return nullptr;
}

bool CCD::setRapidGuide(CCDChip *targetChip, bool enable)
{
    ISwitchVectorProperty *rapidSP = nullptr;
    ISwitch *enableS               = nullptr;

    if (targetChip == primaryChip)
        rapidSP = baseDevice->getSwitch("CCD_RAPID_GUIDE");
    else
        rapidSP = baseDevice->getSwitch("GUIDER_RAPID_GUIDE");

    if (rapidSP == nullptr)
        return false;

    enableS = IUFindSwitch(rapidSP, "ENABLE");

    if (enableS == nullptr)
        return false;

    // Already updated, return OK
    if ((enable && enableS->s == ISS_ON) || (!enable && enableS->s == ISS_OFF))
        return true;

    IUResetSwitch(rapidSP);
    rapidSP->sp[0].s = enable ? ISS_ON : ISS_OFF;
    rapidSP->sp[1].s = enable ? ISS_OFF : ISS_ON;

    clientManager->sendNewSwitch(rapidSP);

    return true;
}

bool CCD::configureRapidGuide(CCDChip *targetChip, bool autoLoop, bool sendImage, bool showMarker)
{
    ISwitchVectorProperty *rapidSP = nullptr;
    ISwitch *autoLoopS = nullptr, *sendImageS = nullptr, *showMarkerS = nullptr;

    if (targetChip == primaryChip)
        rapidSP = baseDevice->getSwitch("CCD_RAPID_GUIDE_SETUP");
    else
        rapidSP = baseDevice->getSwitch("GUIDER_RAPID_GUIDE_SETUP");

    if (rapidSP == nullptr)
        return false;

    autoLoopS   = IUFindSwitch(rapidSP, "AUTO_LOOP");
    sendImageS  = IUFindSwitch(rapidSP, "SEND_IMAGE");
    showMarkerS = IUFindSwitch(rapidSP, "SHOW_MARKER");

    if (!autoLoopS || !sendImageS || !showMarkerS)
        return false;

    // If everything is already set, let's return.
    if (((autoLoop && autoLoopS->s == ISS_ON) || (!autoLoop && autoLoopS->s == ISS_OFF)) &&
        ((sendImage && sendImageS->s == ISS_ON) || (!sendImage && sendImageS->s == ISS_OFF)) &&
        ((showMarker && showMarkerS->s == ISS_ON) || (!showMarker && showMarkerS->s == ISS_OFF)))
        return true;

    autoLoopS->s   = autoLoop ? ISS_ON : ISS_OFF;
    sendImageS->s  = sendImage ? ISS_ON : ISS_OFF;
    showMarkerS->s = showMarker ? ISS_ON : ISS_OFF;

    clientManager->sendNewSwitch(rapidSP);

    return true;
}

void CCD::updateUploadSettings(const QString &remoteDir)
{
    QString filename = seqPrefix + (seqPrefix.isEmpty() ? "" : "_") + QString("XXX");

    ITextVectorProperty *uploadSettingsTP = nullptr;
    IText *uploadT                        = nullptr;

    uploadSettingsTP = baseDevice->getText("UPLOAD_SETTINGS");
    if (uploadSettingsTP)
    {
        uploadT = IUFindText(uploadSettingsTP, "UPLOAD_DIR");
        if (uploadT && remoteDir.isEmpty() == false)
            IUSaveText(uploadT, remoteDir.toLatin1().constData());

        uploadT = IUFindText(uploadSettingsTP, "UPLOAD_PREFIX");
        if (uploadT)
            IUSaveText(uploadT, filename.toLatin1().constData());

        clientManager->sendNewText(uploadSettingsTP);
    }
}

CCD::UploadMode CCD::getUploadMode()
{
    ISwitchVectorProperty *uploadModeSP = nullptr;

    uploadModeSP = baseDevice->getSwitch("UPLOAD_MODE");

    if (uploadModeSP == nullptr)
    {
        qWarning() << "No UPLOAD_MODE in CCD driver. Please update driver to INDI compliant CCD driver.";
        return UPLOAD_CLIENT;
    }

    if (uploadModeSP)
    {
        ISwitch *modeS = nullptr;

        modeS = IUFindSwitch(uploadModeSP, "UPLOAD_CLIENT");
        if (modeS && modeS->s == ISS_ON)
            return UPLOAD_CLIENT;
        modeS = IUFindSwitch(uploadModeSP, "UPLOAD_LOCAL");
        if (modeS && modeS->s == ISS_ON)
            return UPLOAD_LOCAL;
        modeS = IUFindSwitch(uploadModeSP, "UPLOAD_BOTH");
        if (modeS && modeS->s == ISS_ON)
            return UPLOAD_BOTH;
    }

    // Default
    return UPLOAD_CLIENT;
}

bool CCD::setUploadMode(UploadMode mode)
{
    ISwitchVectorProperty *uploadModeSP = nullptr;
    ISwitch *modeS                      = nullptr;

    uploadModeSP = baseDevice->getSwitch("UPLOAD_MODE");

    if (uploadModeSP == nullptr)
    {
        qWarning() << "No UPLOAD_MODE in CCD driver. Please update driver to INDI compliant CCD driver.";
        return false;
    }

    switch (mode)
    {
        case UPLOAD_CLIENT:
            modeS = IUFindSwitch(uploadModeSP, "UPLOAD_CLIENT");
            if (modeS == nullptr)
                return false;
            if (modeS->s == ISS_ON)
                return true;
            break;

        case UPLOAD_BOTH:
            modeS = IUFindSwitch(uploadModeSP, "UPLOAD_BOTH");
            if (modeS == nullptr)
                return false;
            if (modeS->s == ISS_ON)
                return true;
            break;

        case UPLOAD_LOCAL:
            modeS = IUFindSwitch(uploadModeSP, "UPLOAD_LOCAL");
            if (modeS == nullptr)
                return false;
            if (modeS->s == ISS_ON)
                return true;
            break;
    }

    IUResetSwitch(uploadModeSP);
    modeS->s = ISS_ON;

    clientManager->sendNewSwitch(uploadModeSP);

    return true;
}

bool CCD::getTemperature(double *value)
{
    if (HasCooler == false)
        return false;

    INumberVectorProperty *temperatureNP = baseDevice->getNumber("CCD_TEMPERATURE");

    if (temperatureNP == nullptr)
        return false;

    *value = temperatureNP->np[0].value;

    return true;
}

bool CCD::setTemperature(double value)
{
    INumberVectorProperty *nvp = baseDevice->getNumber("CCD_TEMPERATURE");

    if (nvp == nullptr)
        return false;

    INumber *np = IUFindNumber(nvp, "CCD_TEMPERATURE_VALUE");

    if (np == nullptr)
        return false;

    np->value = value;

    clientManager->sendNewNumber(nvp);

    return true;
}

bool CCD::setTransformFormat(CCD::TransferFormat format)
{
    if (format == transferFormat)
        return true;

    ISwitchVectorProperty *svp = baseDevice->getSwitch("CCD_TRANSFER_FORMAT");

    if (svp == nullptr)
        return false;

    ISwitch *formatFITS   = IUFindSwitch(svp, "FORMAT_FITS");
    ISwitch *formatNative = IUFindSwitch(svp, "FORMAT_NATIVE");

    if (formatFITS == nullptr || formatNative == nullptr)
        return false;

    transferFormat = format;

    formatFITS->s   = (transferFormat == FORMAT_FITS) ? ISS_ON : ISS_OFF;
    formatNative->s = (transferFormat == FORMAT_FITS) ? ISS_OFF : ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::setTelescopeType(TelescopeType type)
{
    if (type == telescopeType)
        return true;

    ISwitchVectorProperty *svp = baseDevice->getSwitch("TELESCOPE_TYPE");

    if (svp == nullptr)
        return false;

    ISwitch *typePrimary = IUFindSwitch(svp, "TELESCOPE_PRIMARY");
    ISwitch *typeGuide   = IUFindSwitch(svp, "TELESCOPE_GUIDE");

    if (typePrimary == nullptr || typeGuide == nullptr)
        return false;

    telescopeType = type;

    typePrimary->s = (telescopeType == TELESCOPE_PRIMARY) ? ISS_ON : ISS_OFF;
    typeGuide->s   = (telescopeType == TELESCOPE_PRIMARY) ? ISS_OFF : ISS_ON;

    clientManager->sendNewSwitch(svp);

    setConfig(SAVE_CONFIG);

    return true;
}

bool CCD::setVideoStreamEnabled(bool enable)
{
    if (HasVideoStream == false)
        return false;

    ISwitchVectorProperty *svp = baseDevice->getSwitch("CCD_VIDEO_STREAM");

    if (svp == nullptr)
        return false;

    // If already on and enable is set or vice versa no need to change anything we return true
    if ((enable && svp->sp[0].s == ISS_ON) || (!enable && svp->sp[1].s == ISS_ON))
        return true;

    svp->sp[0].s = enable ? ISS_ON : ISS_OFF;
    svp->sp[1].s = enable ? ISS_OFF : ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::resetStreamingFrame()
{
    INumberVectorProperty *frameProp = baseDevice->getNumber("CCD_STREAM_FRAME");

    if (frameProp == nullptr)
        return false;

    INumber *xarg = IUFindNumber(frameProp, "X");
    INumber *yarg = IUFindNumber(frameProp, "Y");
    INumber *warg = IUFindNumber(frameProp, "WIDTH");
    INumber *harg = IUFindNumber(frameProp, "HEIGHT");

    if (xarg && yarg && warg && harg)
    {
        if (xarg->value == xarg->min && yarg->value == yarg->min && warg->value == warg->max &&
            harg->value == harg->max)
            return false;

        xarg->value = xarg->min;
        yarg->value = yarg->min;
        warg->value = warg->max;
        harg->value = harg->max;

        clientManager->sendNewNumber(frameProp);
        return true;
    }

    return false;
}

bool CCD::setStreamingFrame(int x, int y, int w, int h)
{
    INumberVectorProperty *frameProp = baseDevice->getNumber("CCD_STREAM_FRAME");

    if (frameProp == nullptr)
        return false;

    INumber *xarg = IUFindNumber(frameProp, "X");
    INumber *yarg = IUFindNumber(frameProp, "Y");
    INumber *warg = IUFindNumber(frameProp, "WIDTH");
    INumber *harg = IUFindNumber(frameProp, "HEIGHT");

    if (xarg && yarg && warg && harg)
    {
        if (xarg->value == x && yarg->value == y && warg->value == w && harg->value == h)
            return true;

        // N.B. We add offset since the X, Y are relative to whatever streaming frame is currently active
        xarg->value = qBound(xarg->min, static_cast<double>(x) + xarg->value, xarg->max);
        yarg->value = qBound(yarg->min, static_cast<double>(y) + yarg->value, yarg->max);
        warg->value = qBound(warg->min, static_cast<double>(w), warg->max);
        harg->value = qBound(harg->min, static_cast<double>(h), harg->max);

        clientManager->sendNewNumber(frameProp);
        return true;
    }

    return false;
}

bool CCD::isStreamingEnabled()
{
    if (HasVideoStream == false || streamWindow == nullptr)
        return false;

    return streamWindow->isStreamEnabled();
}

bool CCD::setSERNameDirectory(const QString &filename, const QString &directory)
{
    ITextVectorProperty *tvp = baseDevice->getText("RECORD_FILE");

    if (tvp == nullptr)
        return false;

    IText *filenameT = IUFindText(tvp, "RECORD_FILE_NAME");
    IText *dirT      = IUFindText(tvp, "RECORD_FILE_DIR");

    if (filenameT == nullptr || dirT == nullptr)
        return false;

    IUSaveText(filenameT, filename.toLatin1().data());
    IUSaveText(dirT, directory.toLatin1().data());

    clientManager->sendNewText(tvp);

    return true;
}

bool CCD::getSERNameDirectory(QString &filename, QString &directory)
{
    ITextVectorProperty *tvp = baseDevice->getText("RECORD_FILE");

    if (tvp == nullptr)
        return false;

    IText *filenameT = IUFindText(tvp, "RECORD_FILE_NAME");
    IText *dirT      = IUFindText(tvp, "RECORD_FILE_DIR");

    if (filenameT == nullptr || dirT == nullptr)
        return false;

    filename  = QString(filenameT->text);
    directory = QString(dirT->text);

    return true;
}

bool CCD::startRecording()
{
    ISwitchVectorProperty *svp = baseDevice->getSwitch("RECORD_STREAM");

    if (svp == nullptr)
        return false;

    ISwitch *recordON = IUFindSwitch(svp, "RECORD_ON");

    if (recordON == nullptr)
        return false;

    if (recordON->s == ISS_ON)
        return true;

    IUResetSwitch(svp);
    recordON->s = ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::startDurationRecording(double duration)
{
    INumberVectorProperty *nvp = baseDevice->getNumber("RECORD_OPTIONS");

    if (nvp == nullptr)
        return false;

    INumber *durationN = IUFindNumber(nvp, "RECORD_DURATION");

    if (durationN == nullptr)
        return false;

    ISwitchVectorProperty *svp = baseDevice->getSwitch("RECORD_STREAM");

    if (svp == nullptr)
        return false;

    ISwitch *recordON = IUFindSwitch(svp, "RECORD_DURATION_ON");

    if (recordON == nullptr)
        return false;

    if (recordON->s == ISS_ON)
        return true;

    durationN->value = duration;
    clientManager->sendNewNumber(nvp);

    IUResetSwitch(svp);
    recordON->s = ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::startFramesRecording(uint32_t frames)
{
    INumberVectorProperty *nvp = baseDevice->getNumber("RECORD_OPTIONS");

    if (nvp == nullptr)
        return false;

    INumber *frameN            = IUFindNumber(nvp, "RECORD_FRAME_TOTAL");
    ISwitchVectorProperty *svp = baseDevice->getSwitch("RECORD_STREAM");

    if (frameN == nullptr || svp == nullptr)
        return false;

    ISwitch *recordON = IUFindSwitch(svp, "RECORD_FRAME_ON");

    if (recordON == nullptr)
        return false;

    if (recordON->s == ISS_ON)
        return true;

    frameN->value = frames;
    clientManager->sendNewNumber(nvp);

    IUResetSwitch(svp);
    recordON->s = ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::stopRecording()
{
    ISwitchVectorProperty *svp = baseDevice->getSwitch("RECORD_STREAM");

    if (svp == nullptr)
        return false;

    ISwitch *recordOFF = IUFindSwitch(svp, "RECORD_OFF");

    if (recordOFF == nullptr)
        return false;

    // If already set
    if (recordOFF->s == ISS_ON)
        return true;

    IUResetSwitch(svp);
    recordOFF->s = ISS_ON;

    clientManager->sendNewSwitch(svp);

    return true;
}

bool CCD::setFITSHeader(const QMap<QString, QString> &values)
{
    ITextVectorProperty *tvp = baseDevice->getText("FITS_HEADER");

    if (tvp == nullptr)
        return false;

    QMapIterator<QString, QString> i(values);

    while (i.hasNext())
    {
        i.next();

        IText *headerT = IUFindText(tvp, i.key().toLatin1().data());

        if (headerT == nullptr)
            continue;

        IUSaveText(headerT, i.value().toLatin1().data());
    }

    clientManager->sendNewText(tvp);

    return true;
}

bool CCD::setGain(double value)
{
    if (gainN == nullptr)
        return false;

    gainN->value = value;
    clientManager->sendNewNumber(gainN->nvp);
    return true;
}

bool CCD::getGain(double *value)
{
    if (gainN == nullptr)
        return false;

    *value = gainN->value;

    return true;
}

bool CCD::getGainMinMaxStep(double *min, double *max, double *step)
{
    if (gainN == nullptr)
        return false;

    *min  = gainN->min;
    *max  = gainN->max;
    *step = gainN->step;

    return true;
}

bool CCD::isBLOBEnabled()
{
    return (clientManager->getBLOBMode(getDeviceName(), "CCD1") != B_NEVER);
}

bool CCD::setBLOBEnabled(bool enable)
{
    if (enable)
    {
        clientManager->setBLOBMode(B_ALSO, getDeviceName(), "CCD1");
        clientManager->setBLOBMode(B_ALSO, getDeviceName(), "CCD2");
    }
    else
    {
        clientManager->setBLOBMode(B_NEVER, getDeviceName(), "CCD1");
        clientManager->setBLOBMode(B_NEVER, getDeviceName(), "CCD2");
    }

    return true;
}

}
