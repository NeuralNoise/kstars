/***************************************************************************
                          skyobject.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : Sun Feb 11 2001
    copyright            : (C) 2001 by Jason Harris
    email                : jharris@30doradus.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "skyobject.h"
#include "dms.h"

SkyObject::SkyObject(){
	Type = 0;
	Position.set( 0.0, 0.0 );
  Magnitude = 0.0;
  Name = "unnamed";
  Name2 = "";
  LongName = "";
}

SkyObject::SkyObject( SkyObject &o ) {
	Type = o.type();
	Position = *o.pos();
  Magnitude = o.mag();
  Name = o.name();
	Name2 = o.name2();
	LongName = o.longname();
	ImageList = o.ImageList;
	ImageTitle = o.ImageTitle;
	InfoList = o.InfoList;
	InfoTitle = o.InfoTitle;
}

SkyObject::SkyObject( int t, dms r, dms d, double m, QString n,
                      QString n2, QString lname ) {
	Type = t;
	Position.setRA0( r );
	Position.setDec0( d );
	Position.setRA( r );
	Position.setDec( d );
  Magnitude = m;
  Name = n;
  Name2 = n2;
  LongName = lname;
}

SkyObject::SkyObject( int t, double r, double d, double m, QString n,
                      QString n2, QString lname ) {
	Type = t;
	Position.setRA0( r );
	Position.setDec0( d );
	Position.setRA( r );
	Position.setDec( d );
  Magnitude = m;
  Name = n;
  Name2 = n2;
  LongName = lname;
}

QTime SkyObject::riseTime( long double jd, GeoLocation *geo ) {
	double r = -1.0 * tan( geo->lat().radians() ) * tan( dec().radians() );
	if ( r < -1.0 || r > 1.0 )
		return QTime( 0, 0, 0 );  //this object does not rise or set; return an invalid time
		
	double H = acos( r )*180./acos(-1.0); //180/Pi converts radians to degrees
	dms LST;
	LST.setH( 24.0 + ra().Hours() - H/15.0 );
	LST = LST.reduce();

	//convert LST to Greenwich ST
	dms GST = dms( LST.Degrees() - geo->lng().Degrees() ).reduce();

	//convert GST to UT
	double T = ( jd - J2000 )/36525.0;
	dms T0, dT, UT;
	T0.setH( 6.697374558 + (2400.051336*T) + (0.000025862*T*T) );
	T0 = T0.reduce();
	dT.setH( GST.Hours() - T0.Hours() );
	dT = dT.reduce();
	UT.setH( 0.9972695663 * dT.Hours() );
	UT = UT.reduce();

	//convert UT to LT;
	dms LT = dms( UT.Degrees() + 15.0*geo->TZ() ).reduce();

	//If LT is 00:00:00.0, add a second (because 0 is a signal that the object is circumpolar)
	if ( LT.hour()==0 && LT.minute()==0 && LT.second()==0 ) LT.setHSec( 1 );

	return QTime( LT.hour(), LT.minute(), LT.second() );
}

QTime SkyObject::setTime( long double jd, GeoLocation *geo ) {
	double r = -1.0 * tan( geo->lat().radians() ) * tan( dec().radians() );
	if ( r < -1.0 || r > 1.0 )
		return QTime( 0, 0, 0 );  //this object does not rise or set; return an invalid time
		
	double H = acos( r )*180./acos(-1.0);
	dms LST;

	//the following line is the only difference between riseTime() and setTime()
	LST.setH( ra().Hours() + H/15.0 );
	LST = LST.reduce();

	//convert LST to Greenwich ST
	dms GST = dms( LST.Degrees() - geo->lng().Degrees() ).reduce();

	//convert GST to UT
	double T = ( jd - J2000 )/36525.0;
	dms T0, dT, UT;
	T0.setH( 6.697374558 + (2400.051336*T) + (0.000025862*T*T) );
	T0 = T0.reduce();
	dT.setH( GST.Hours() - T0.Hours() );
	dT = dT.reduce();
	UT.setH( 0.9972695663 * dT.Hours() );
  UT = UT.reduce();

	//convert UT to LT;
	dms LT = dms( UT.Degrees() + 15.0*geo->TZ() ).reduce();

	//If LT is 00:00:00.0, add a second (because 0 is a signal that the object is circumpolar)
	if ( LT.hour()==0 && LT.minute()==0 && LT.second()==0 ) LT.setHSec( 1 );

	return QTime( LT.hour(), LT.minute(), LT.second() );
}
