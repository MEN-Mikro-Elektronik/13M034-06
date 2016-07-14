/****************************************************************************
 ************                                                    ************
 ************                 M 3 4 _ R E A D                    ************
 ************                                                    ************
 ****************************************************************************
 *  
 *       Author: ds
 *        $Date: 2009/10/19 16:21:04 $
 *    $Revision: 1.3 $
 *
 *  Description: Universal tool to read M34/M35 channels
 *
 *     Required: Libraries: mdis_api, usr_oss, usr_utl
 *     Switches: -
 *
 *-------------------------------[ History ]---------------------------------
 *
 * $Log: m34_read.c,v $
 * Revision 1.3  2009/10/19 16:21:04  KSchneider
 * R: driver ported to MDIS5, new MDIS_API and men_typs
 * M: for backward compatibility to MDIS4 optionally define new types
 *
 * Revision 1.2  2003/10/20 11:22:22  kp
 * made internal functions static
 *
 * Revision 1.1  1998/12/15 10:30:28  Schmidt
 * Initial Revision
 *
 *---------------------------------------------------------------------------
 * (c) Copyright 1998 by MEN mikro elektronik GmbH, Nuernberg, Germany 
 ****************************************************************************/
 
static const char RCSid[]="$Header: /dd2/CVSR/COM/DRIVERS/MDIS_LL/M034/TOOLS/M34_READ/COM/m34_read.c,v 1.3 2009/10/19 16:21:04 KSchneider Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MEN/men_typs.h>
#include <MEN/usr_oss.h>
#include <MEN/usr_utl.h>
#include <MEN/mdis_api.h>
#include <MEN/m34_drv.h>

/*--------------------------------------+
|   PROTOTYPES                          |
+--------------------------------------*/
static void usage(void);
static void PrintError(char *info);

/********************************* usage ************************************
 *
 *  Description: Print program usage
 *			   
 *---------------------------------------------------------------------------
 *  Input......: -
 *  Output.....: -
 *  Globals....: -
 ****************************************************************************/
static void usage(void)
{
	printf("Usage: m34_read [<opts>] <device> [<opts>]\n");
	printf("Function: Configure and read M34/M35 channel\n");
	printf("Options:\n");
	printf("    device       device name                 [none]\n");
	printf("    -r=<res>     resolution                  [none]\n");
	printf("                  12 = 12-bit (for M34)\n");
	printf("                  14 = 14-bit (for M35)\n");
	printf("    -c=<chan>    channel number (0..7/15)    [0]\n");
	printf("    -g=<gain>    gain factor                 [x1]\n");			
	printf("                  0 = x1\n");
	printf("                  1 = x2\n");
	printf("                  2 = x4\n");
	printf("                  3 = x8\n");
	printf("                  4 = x16 (on-board jumper must be set !)\n");
	printf("    -m=<mode>    measuring mode              [unipolar]\n");     
	printf("                  0=unipolar\n");
	printf("                  1=bipolar\n");
	printf("    -d=<mode>    display mode                [raw hex]\n");			
	printf("                  0 = raw hex value \n");
	printf("                  1 = hex and volt	\n");
	printf("                  2 = hex and ampere (only for gain factor x8)\n");
	printf("    -l           loop mode                   [no]\n");
	printf("\n");
	printf("(c) 1998 by MEN mikro elektronik GmbH\n\n");
}

/********************************* main *************************************
 *
 *  Description: Program main function
 *			   
 *---------------------------------------------------------------------------
 *  Input......: argc,argv	argument counter, data ..
 *  Output.....: return	    success (0) or error (1)
 *  Globals....: -
 ****************************************************************************/
int main(int argc, char *argv[])
{
	MDIS_PATH   path=0;
	int32        res, chan, gain, mode, disp, loopmode, value, n, gainfac;
	char	    *device, *str, *errstr, buf[40];
	double	    volt, curr;

	/*--------------------+
    |  check arguments    |
    +--------------------*/
	if ((errstr = UTL_ILLIOPT("r=c=g=m=d=l?", buf))) {	/* check args */
		printf("*** %s\n", errstr);
		return(1);
	}

	if (UTL_TSTOPT("?")) {						/* help requested ? */
		usage();
		return(1);
	}

	/*--------------------+
    |  get arguments      |
    +--------------------*/
	for (device=NULL, n=1; n<argc; n++)
		if (*argv[n] != '-') {
			device = argv[n];
			break;
		}

	if (!device) {
		usage();
		return(1);
	}

	res      = ((str = UTL_TSTOPT("r=")) ? atoi(str) : 0);
	chan     = ((str = UTL_TSTOPT("c=")) ? atoi(str) : 0);
	gain     = ((str = UTL_TSTOPT("g=")) ? atoi(str) : 0);
	mode     = ((str = UTL_TSTOPT("m=")) ? atoi(str) : 0);
	disp     = ((str = UTL_TSTOPT("d=")) ? atoi(str) : 0);
	loopmode = (UTL_TSTOPT("l") ? 1 : 0);

	/* check for option conflict */
	if ( (res != 12) && (res != 14) ) {
		printf("\n*** option -r= must be 12 (for M34) or 14 (for M35)\n\n");
		usage();
		return(1);
	}
	if ((disp == 2) && (gain != 3)) {
		printf("\n*** option -d=2 only available with option -g=3\n\n");
		usage();
		return(1);
	}

	gainfac = 1 << gain;		/* calculate gain factor */

	/*--------------------+
    |  open path          |
    +--------------------*/
	if ((path = M_open(device)) < 0) {
		PrintError("open");
		return(1);
	}

	/*--------------------+
    |  config             |
    +--------------------*/
	/* set current channel */
	if ((M_setstat(path, M_MK_CH_CURRENT, chan)) < 0) {
		PrintError("setstat M_MK_CH_CURRENT");
		goto abort;
	}
	/* set measuring mode for current channel */
	if ((M_setstat(path, M34_CH_BIPOLAR, mode)) < 0) {
		PrintError("setstat M34_CH_BIPOLAR");
		goto abort;
	}
	/* check for SW gain */
	if (gain != 4) {
		/* set gain */
		if ((M_setstat(path, M34_CH_GAIN, gain)) < 0) {
			PrintError("setstat M34_CH_GAIN");
			goto abort;
		}
	}

    /*--------------------+
    |  print info         |
    +--------------------*/
	printf("resolution          : %d-bit\n", res);
	printf("channel number      : %d\n", chan);
	printf("gain factor         : x%d\n", gainfac);
	printf("measuring mode      : %s\n\n",(mode==0 ? "unipolar":"bipolar"));

	/*--------------------+
    |  read               |
    +--------------------*/
	do {
		if ((M_read(path,&value)) < 0) {
			PrintError("read");
			goto abort;
		}
		switch (disp) {
			/* raw hex value */
			case 0:
				printf("read: 0x%04x (value is %s)\n",
					value, ((value & 0x0001) ? "invalid":"valid"));
				break;
			/* voltage */
			case 1:
				M34_CALC_VOLTAGE( value, gainfac, mode, res, volt );
				printf("read: 0x%04x = %7.3f V (value is %s)\n",
					value, volt, ((value & 0x0001) ? "invalid":"valid"));
				break;
			/* current */
			case 2:
				M34_CALC_CURRENT( value, mode, res, curr );
				printf("read: 0x%04x = %7.3f mA (value is %s)\n",
					value, curr, ((value & 0x0001) ? "invalid":"valid"));
				break;
			/* invalid */
			default:
				printf("*** option -d=%d out of range (-d=0..2)\n", disp);
		}

		UOS_Delay(100);
	} while(loopmode && UOS_KeyPressed() == -1);

	/*--------------------+
    |  cleanup            |
    +--------------------*/
	abort:
	if (M_close(path) < 0)
		PrintError("close");

	return(0);
}

/********************************* PrintError ********************************
 *
 *  Description: Print MDIS error message
 *			   
 *---------------------------------------------------------------------------
 *  Input......: info	info string
 *  Output.....: -
 *  Globals....: -
 ****************************************************************************/

static void PrintError(char *info)
{
	printf("*** can't %s: %s\n", info, M_errstring(UOS_ErrnoGet()));
}



