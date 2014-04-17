#include <stdarg.h>

#include "core.h"
#include "lib_weatherfile.h"
#include "lib_irradproc.h";
#include "lib_util.h"

static var_info _cm_vtab_wfcheck[] = 
{	
/*   VARTYPE           DATATYPE         NAME                         LABEL                              UNITS     META                      GROUP                     REQUIRED_IF                 CONSTRAINTS                      UI_HINTS*/
	{ SSC_INPUT,        SSC_STRING,      "input_file",               "Input weather file name",         "",       "wfcsv format",      "Weather File Checker", "*",                       "",                     "" },
	
var_info_invalid };
class cm_wfcheck : public compute_module
{
private:
	int nwarnings, nerrors;
public:
	cm_wfcheck()
	{
		nwarnings = nerrors = 0;
		add_var_info( _cm_vtab_wfcheck );
	}

	void warn( const char *fmt, ... )
	{
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
#if defined(_MSC_VER)||defined(_WIN32)
		_vsnprintf(buf, 1023, fmt, ap);
#else
		vsnprintf(buf, 1023, fmt, ap);
#endif
		va_end(ap);

		assign( util::format( "warning%d", nwarnings ), var_data(std::string(buf)) );
		nwarnings++;
	}

	void exec() throw( general_error )
	{
		weatherfile wf( as_string("input_file") );
		if ( !wf.ok() ) throw general_error( "could not open weather file for read: " + wf.filename() );

		nwarnings = nerrors = 0;

		double T = 60; // threshold on temp

		double zenith, hextra;
		double sunn[9];
		for( size_t i = 0; i<wf.nrecords; i++ )
		{
			if ( !wf.read() )
			{
				warn("error reading record %d, stopping", i );
				break;
			}			

			solarpos( wf.year, wf.month, wf.day, wf.hour, wf.minute, wf.lat, wf.lon, wf.tz, sunn );						
			zenith = sunn[1]; // zenith angle, radians
			hextra = sunn[8];
			
			// check irradiance values for consistency if all three components exist
			double gh_est = wf.dn*cos(zenith) + wf.df;
			if ( std::isnan( gh_est ) )
				gh_est = wf.gh;

			if ( !std::isnan( wf.dn ) && !isnan( wf.df ) && !isnan( wf.gh ) )
			{
				if ( gh_est > 500 && (gh_est-wf.gh)/wf.gh > 0.2 )
					warn( "beam+diffuse (%lg) inconsistent with global (%lg) at record %d by greater than 20 percent", gh_est, wf.gh, i );
				else if ( gh_est > 200 && (gh_est-wf.gh)/wf.gh > 0.5 )
					warn( "beam+diffuse (%lg) inconsistent with global (%lg) at record %d by greater than 50 percent", gh_est, wf.gh, i );

				// don't do checks on less than 200 W/m2, errors are too big to really check
			}



			if ( !std::isnan( wf.dn ) && wf.dn > 1500 ) warn( "beam irradiance (%lg) at record %d is greater than 1500", wf.dn, i );
			if ( !std::isnan( wf.dn ) && wf.dn < 0 ) warn( "beam irradiance (%lg) at record %d is negative", wf.dn, i );
			
			// cap for global and diffuse irradiance 
			double irrmax = 1.5*(hextra+150);			
			if ( irrmax > 1500 ) irrmax = 1500;

			if ( !std::isnan( wf.df ) && wf.df > irrmax ) warn( "diffuse irradiance (%lg) at record %d is greater than threshold (%lg)", wf.dn, i, irrmax );
			if ( !std::isnan( wf.df ) && wf.df < 0 ) warn( "diffuse irradiance (%lg) at record %d is negative", wf.df, i );
			
			if ( !std::isnan( wf.gh ) && wf.gh > irrmax ) warn( "global irradiance (%lg) at record %d is greater than threshold (%lg)", wf.gh, i, irrmax );
			if ( !std::isnan( wf.gh ) && wf.gh < 0 ) warn( "global irradiance (%lg) at record %d is negative", wf.gh, i );


			int nirrnans = 0;
			if ( std::isnan( wf.dn ) ) nirrnans++;
			if ( std::isnan( wf.gh ) ) nirrnans++;
			if ( std::isnan( wf.df ) ) nirrnans++;
			if ( nirrnans > 1 )
				warn( "[%lg %lg %lg] only 1 component of irradiance specified at record %d", wf.gh, wf.dn, wf.df, i );

			if ( wf.wspd > 30 ) warn( "wind speed (%lg) greater than 30 m/s at record %d", wf.wspd, i );
			if ( wf.wspd < 0 ) warn("wind speed (%lg) less than 0 m/s at record %d",wf.wspd, i );
			
			if ( wf.wdir > 360 ) warn("wind direction angle (%lg) greater than 360 degrees at record %d", wf.wdir, i );
			if ( wf.wdir < 0 ) warn( "wind direction angle (%lg) less than 0 degrees at record %d", wf.wdir, i );
			
			if ( wf.tdry > T ) warn( "dry bulb temperature (%lg) greater than %lg C at record %d", wf.tdry, T, i );
			if ( wf.tdry < -T ) warn( "dry bulb temperature (%lg) less than -%lg C at record %d", wf.tdry, T, i );
			
			if ( wf.twet > T ) warn( "wet bulb temperature (%lg) greater than %lg C at record %d", wf.twet, T, i );
			if ( wf.twet < -T ) warn( "wet bulb temperature (%lg) less than -%lg C at record %d", wf.twet, T, i );
			
			if ( wf.tdew > T ) warn( "dew point temperature (%lg) greater than %lg C at record %d", wf.tdew, T, i );
			if ( wf.tdew < -T ) warn( "dew point temperature (%lg) less than -%lg C at record %d", wf.tdew, T, i );

			if ( wf.rhum < 2 ) warn("relative humidity (%lg) less than 2 percent at record %d", wf.rhum, i );
			if ( wf.rhum > 100 ) warn("relative humidity (%lg) greater than 100 percent at record %d", wf.rhum, i );

			if ( wf.pres < 700 ) warn("pressure (%lg) less than 700 millibar at record %d", wf.pres, i );
			if ( wf.pres > 1100 ) warn("pressure greater than 1100 millibar at record %d", wf.pres, i );


			if ( nwarnings >= 99 )
			{
				warn( "bailing... too many warnings.");
				break;
			}

		}



		assign( "nwarnings", var_data( nwarnings ) );
	}
};

DEFINE_MODULE_ENTRY( wfcheck, "Weather file checker.", 1 );