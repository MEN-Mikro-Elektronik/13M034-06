/****************************************************************************
 ************                                                    ************
 ************              M 3 4 _ B L K R E A D                 ************
 ************                                                    ************
 ****************************************************************************
 *  
 *       Author: ds
 *        $Date: 2018/06/11 15:53:17 $
 *    $Revision: 1.6 $
 *
 *  Description: Universal tool to read M34/M35 channels (blockwise) 
 *
 *     Required: Libraries: mdis_api, usr_oss, usr_utl
 *     Switches: -
 *
 *---------------------------------------------------------------------------
 * Copyright (c) 1998-2019, MEN Mikro Elektronik GmbH
 ****************************************************************************/
/*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MEN/men_typs.h>
#include <MEN/usr_oss.h>
#include <MEN/usr_utl.h>
#include <MEN/mdis_api.h>
#include <MEN/m34_drv.h>

/*--------------------------------------+
|   GLOBALS                             |
+--------------------------------------*/
static u_int32 G_SigCountBufHigh;	/* count of buffer highwater signals */
static u_int32 G_SigCountOthers;	/* count of other signals */

/*--------------------------------------+
|   PROTOTYPES                          |
+--------------------------------------*/
static void usage(void);
static void PrintMdisError(char *info);
static void PrintUosError(char *info);
static void __MAPILIB SigHandler(u_int32 sigCode);

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
	printf("Usage: m34_blkread [<opts>] <device> [<opts>]                   \n");
	printf("Function: Configure and read M34/M35 channels (blockwise)       \n");
	printf("Options:                                                        \n");
	printf("    device       device name                          [none]    \n");
	printf("    -r=<res>     resolution                           [none]    \n");
	printf("                   12 = 12-bit (for M34)                        \n");
	printf("                   14 = 14-bit (for M35)                        \n");
	printf("    _____________channel selection______________________________\n");
	printf("    -x           channel selection from descriptor    [no]      \n");
	printf("                   (ignores -a=<ch> and  -z=<ch>)               \n");
	printf("    -a=<ch>      first channel to read (0..7/15)      [0]       \n");
	printf("    -z=<ch>      last channel to read (0..7/15)       [7/15]    \n");
	printf("    _____________measurement settings___________________________\n");
	printf("    -g=<gain>    gain factor for all channels         [x1]      \n");
	printf("                   0 = x1                                       \n");
	printf("                   1 = x2                                       \n");
	printf("                   2 = x4                                       \n");
	printf("                   3 = x8                                       \n");
	printf("                   4 = x16 (on-board jumper must be set !)      \n");
	printf("    -m=<mode>    measuring mode                       [unipolar]\n");     
	printf("                   0=unipolar                                   \n");
	printf("                   1=bipolar                                    \n");
	printf("    -d=<mode>    display mode                         [raw hex] \n");			
	printf("                   0 = raw hex value                            \n");
	printf("                   1 = volt	                                    \n");
	printf("                   2 = ampere (only for gain factor x8)         \n");
	printf("    _____________block/buffer/irq specific settings_____________\n");
	printf("    -b=<mode>    block i/o mode                       [USRCTRL] \n");
	printf("                   0 = M_BUF_USRCTRL                            \n");
	printf("                   1 = M_BUF_CURRBUF (uses irq, not for -i=2)   \n");
	printf("                   2 = M_BUF_RINGBUF (uses irq)                 \n");
	printf("                   3 = M_BUF_RINGBUF_OVERWR (uses irq)          \n");
	printf("    -i=<mode>    irq mode                             [0]       \n");
	printf("                   0 = Legacy mode: read all enabled ch per irq \n");
	printf("                       (wastes cpu time in isr)                 \n");
	printf("                   1 = One ch per irq mode: read one enabled ch \n");
	printf("                       per irq (improved isr processing)        \n");
	printf("                   2 = One ch per irq mode with irq en/disable: \n");
	printf("                       same as mode 1 but automatically         \n");
	printf("                       enables/disables the module interrupt    \n");
	printf("                   3 = Fix mode: read always all ch per irq     \n");
	printf("                       (ignores ch selection and buffer config) \n");
	printf("    -s=<size>    block size to read in bytes          [128]     \n");
	printf("                   -i=1/2: must be multiple of ch to read x2    \n");
	printf("                   -i=3  : automatically set (-s= ignored)      \n");
	printf("    -o=<msec>    block read timeout [msec] (0=none)   [0]       \n");
	printf("    -h           install buffer highwater signal      [no]      \n");
	printf("    _____________miscellaneous settings_________________________\n");
	printf("    -l           loop mode                            [no]      \n");
	printf("                                                                \n");
	printf("(c) 1998-2015 by MEN mikro elektronik GmbH                    \n\n");
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
int main( int argc, char *argv[])
{
	MDIS_PATH	path=0;
	int32       firstCh, lastCh, blkmode, blksize, tout, gainfac, irqCount;
	int32   	res, gain, mode, disp, signal, loopmode, n, ch, chNbr, gotsize, irqMode, nosel;
	u_int8	    *blkbuf = NULL;
	u_int8	    *bp = NULL;
	u_int8	    *bp0 = NULL;
	u_int8	    *bmax = NULL;
	char	    *device,*str,*errstr,buf[40];
	double	    volt, curr;
#ifdef WINNT
	int32       isrTime;
#endif	

	/*--------------------+
    |  check arguments    |
    +--------------------*/
	if ((errstr = UTL_ILLIOPT("a=-r=z=b=i=s=o=g=m=t=d=hlx?", buf))) {	/* check args */
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
	firstCh  = ((str = UTL_TSTOPT("a=")) ? atoi(str) : 0);
	lastCh   = ((str = UTL_TSTOPT("z=")) ? atoi(str) : -1);
	blkmode  = ((str = UTL_TSTOPT("b=")) ? atoi(str) : M_BUF_USRCTRL);
	irqMode  = ((str = UTL_TSTOPT("i=")) ? atoi(str) : 0);
	blksize  = ((str = UTL_TSTOPT("s=")) ? atoi(str) : 128);
	tout     = ((str = UTL_TSTOPT("o=")) ? atoi(str) : 0);
	gain     = ((str = UTL_TSTOPT("g=")) ? atoi(str) : 0);
	mode     = ((str = UTL_TSTOPT("m=")) ? atoi(str) : 0);
	disp     = ((str = UTL_TSTOPT("d=")) ? atoi(str) : 0);
	signal   = (UTL_TSTOPT("h") ? 1 : 0);
	loopmode = (UTL_TSTOPT("l") ? 1 : 0);
	nosel    = (UTL_TSTOPT("x") ? 1 : 0);

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

	/* check for valid display mode */
	if ( (disp<0) || (disp>2) ) {
		printf("*** option -d=%d out of range (-d=0..2)\n", disp);
		return(1);
	}

	/* check for valid irq mode */
	if ((irqMode<0) || (irqMode>3)) {
		printf("*** option -i=%d out of range (-d=0..3)\n", irqMode);
		return(1);
	}

	gainfac = 1 << gain;		/* calculate gain factor */

	/*--------------------+
	|  install signal     |
	+--------------------*/
	/* install signal handler */
	if (UOS_SigInit(SigHandler)) {
		PrintUosError("SigInit");
		return(1);
	}

	if (signal) {
		/* install signal */
		if (UOS_SigInstall(UOS_SIG_USR1)) {
			PrintUosError("SigInstall");
			goto abort;
		}
	}

	/* clear signal counters */
	G_SigCountBufHigh = 0;
	G_SigCountOthers = 0;

	/*--------------------+
    |  open path          |
    +--------------------*/
	if ((path = M_open(device)) < 0) {
		PrintMdisError("open");
		return(1);
	}

	/* get number of channels */
	if ((M_getstat(path, M_LL_CH_NUMBER, &chNbr)) < 0) {
		PrintMdisError("getstat M_LL_CH_NUMBER");
		goto abort;
	}

	/* fix irq mode: block size must store the data for all available channels */
	if (irqMode == M34_IMODE_FIX)
		blksize = 2 * chNbr;

	/*--------------------+
	|  create buffer      |
	+--------------------*/
	if ((blkbuf = (u_int8*)malloc(blksize)) == NULL) {
		printf("*** can't alloc %d bytes\n", blksize);
		return(1);
	}
	
	/*--------------------+
    |  global config      |
    +--------------------*/
	/* set block i/o mode */
	if ((M_setstat(path, M_BUF_RD_MODE, blkmode)) < 0) {
			PrintMdisError("setstat M_BUF_RD_MODE");
		goto abort;
	}
	/* set block read timeout */
	if ((M_setstat(path, M_BUF_RD_TIMEOUT, tout)) < 0) {
			PrintMdisError("setstat M_BUF_RD_TIMEOUT");
		goto abort;
	}

	/*--------------------+
	|  ch config          |
	+--------------------*/
	if (lastCh == -1)
		lastCh = chNbr - 1;

	/* channel specific settings */
	for (ch=0; ch<chNbr; ch++) { 

		/* set current channel */
		if ((M_setstat(path, M_MK_CH_CURRENT, ch)) < 0) {
			PrintMdisError("setstat M_MK_CH_CURRENT");
			goto abort;
		}

		/* set measuring mode */
		if ((M_setstat(path, M34_CH_BIPOLAR, mode)) < 0) {
			PrintMdisError("setstat M34_CH_BIPOLAR");
			goto abort;
		}

		/* check for SW gain */
		if (gain != 4) {
			/* set gain */
			if ((M_setstat(path, M34_CH_GAIN, gain)) < 0) {
				PrintMdisError("setstat M34_CH_GAIN");
				goto abort;
			}
		}

		/* channel selection */
		if (!nosel) {
			if ( (ch < firstCh) || (ch > lastCh) ) {
				/* disable channel */
				if ((M_setstat(path, M34_CH_RDBLK_IRQ, 0)) < 0) {
					PrintMdisError("setstat M34_CH_RDBLK_IRQ");
					goto abort;
				}
			}
			else {
				/* enable channel */
				if ((M_setstat(path, M34_CH_RDBLK_IRQ, 1)) < 0) {
					PrintMdisError("setstat M34_CH_RDBLK_IRQ");
					goto abort;
				}
			}
		}
	}

	if (signal) {
		/* enable buffer highwater signal */
		if ((M_setstat(path, M_BUF_RD_SIGSET_HIGH, UOS_SIG_USR1)) < 0) {
				PrintMdisError("setstat M_BUF_RD_SIGSET_HIGH");
			goto abort;
		}
	}

    /*--------------------+
    |  print info         |
    +--------------------*/
	printf("resolution          : %d-bit\n", res);
	if (nosel) {
		printf("channel selection   : from descriptor\n");
	}
	else {
		printf("first channel       : %d\n",firstCh);
		printf("last channel        : %d\n",lastCh);
	}
	printf("gain factor         : x%d\n", gainfac);
	printf("measuring mode      : %s\n",(mode==0 ? "unipolar":"bipolar"));
	printf("block i/o mode      : ");
	switch (blkmode)
	{
		case 0: printf("M_BUF_USRCTRL\n");
			break;
		case 1: printf("M_BUF_CURRBUF\n");
			break;
		case 2: printf("M_BUF_RINGBUF\n");
			break;
		case 3: printf("M_BUF_RINGBUF_OVERWR\n");
			break;
	}
	printf("block size          : %d bytes\n",blksize);
	printf("block read timeout  : %d msec\n",tout);
	printf("buf highwater signal: %s\n",(signal==0 ? "no":"yes"));
	printf("loop mode           : %s\n\n",(loopmode==0 ? "no":"yes"));

    /*-------------------------------------+
    |  block mode with IRQ or fix irq mode |
    +-------------------------------------*/
	if (blkmode || (irqMode == M34_IMODE_FIX)){
		if ((M_setstat(path, M_LL_IRQ_COUNT, 0)) < 0) {
			PrintMdisError("setstat M_LL_IRQ_COUNT");
			goto abort;
		}

		/* set irq mode */
		if ((M_setstat(path, M34_IRQ_MODE, irqMode)) < 0) {
			PrintMdisError("setstat M34_IRQ_MODE");
			goto abort;
		}

		/*
		 * irq mode M34_IMODE_LEGACY / M34_IMODE_CHIRQ:
		 *  - Enable interrupt at carrier and M-Module (measurement starts here)
		 * irq mode M34_IMODE_CHIRQ_AUTO / M34_IMODE_FIX:
		 *  - Enable interrupt at carrier only (measurement during M_getblock only)
		 */
		if ((M_setstat(path, M_MK_IRQ_ENABLE, 1)) < 0) {
			PrintMdisError("setstat M_MK_IRQ_ENABLE");
			goto abort;
		}
	}

    /*--------------------+
    |  read block         |
    +--------------------*/
	do {
		printf("\nwaiting for data ..\n\n");

		/*
		* irq mode M34_IMODE_CHIRQ_AUTO / M34_IMODE_FIX:
		*  - Enable/disable interrupt at M-Module (measurement starts here)
		*/
		if ((gotsize = M_getblock(path,(u_int8*)blkbuf,blksize)) < 0) {
			PrintMdisError("getblock");
			break;
		}

		/* raw hex value */
		if (disp == 0) {
				UTL_Memdump("raw hex value:",(char*)blkbuf,gotsize,2);
		}
		/* voltage or current */
		else {
			bmax = blkbuf + gotsize;

			if (disp==1)
				printf("voltage: (%ld bytes)\n",gotsize);
			else
				printf("current: (%ld bytes)\n",gotsize);
			
			for (bp=bp0=blkbuf; bp0<bmax; bp0+=16) {
#ifndef MENTYPS_64BIT
				printf("%08x+%04x: ",(int32)blkbuf, (int16)(bp-blkbuf));
#else
				printf("%016llx+%08x: ",(int64)blkbuf, (int32)(bp-blkbuf));
#endif
				
				for (bp=bp0,n=0; n<16; n+=2, bp+=2) {	/* word aligned */
					/* voltage */
					if (disp==1) {
						M34_CALC_VOLTAGE( *(int16*)bp, gainfac, mode, res, volt );
						if (bp<bmax)  printf("%7.3fV",volt);
							else      printf("      ");
					}
					/* current */
					else {
						M34_CALC_CURRENT( *(int16*)bp, mode, res, curr );
						if (bp<bmax)  printf("%7.3fmA",curr);
							else      printf("       ");
					}
				}
				printf("\n");
			}
		} /* voltage or current */

	} while(loopmode && UOS_KeyPressed() == -1);

	/*--------------------+
    |  cleanup            |
    +--------------------*/
	abort:

	/* block mode with IRQ or fix irq mode */
	if (blkmode || (irqMode == M34_IMODE_FIX)) {

		/*
		* Lagacy irq mode: - Disable interrupt at carrier and M-Module.
		*                  - Measurement ends here!
		* Fast irq mode  : Disable interrupt at carrier only.
		*/
		if ((M_setstat(path, M_MK_IRQ_ENABLE, 0)) < 0) {
			PrintMdisError("setstat M_MK_IRQ_ENABLE");
		}

		if ((M_getstat(path, M_LL_IRQ_COUNT, &irqCount)) < 0)
			PrintMdisError("getstat M_LL_IRQ_COUNT");

		printf("\n");
		printf("irq mode                    : %d\n", irqMode);
		printf("irq calls                   : %d\n", irqCount);

#ifdef WINNT
		if ((M_getstat(path, M34_ISR_TIME, &isrTime)) < 0)
			PrintMdisError("getstat M34_ISR_TIME");

		printf("accumulated isr time        : %dus\n", isrTime);

		printf("average time/isr call       : %dus\n",
			irqCount ? isrTime / irqCount : 0);
#endif

	}

	/* terminate signal handling */
	UOS_SigExit();
	printf("\n");
	printf("Count of buffer highwater signals : %d \n", G_SigCountBufHigh);
	printf("Count of other signals            : %d \n", G_SigCountOthers);

	if (M_close(path) < 0)
		PrintMdisError("close");

	return(0);
}

/********************************* PrintMdisError ***************************
 *
 *  Description: Print MDIS error message
 *			   
 *---------------------------------------------------------------------------
 *  Input......: info	info string
 *  Output.....: -
 *  Globals....: -
 ****************************************************************************/
static void PrintMdisError(char *info)
{
	printf("*** can't %s: %s\n", info, M_errstring(UOS_ErrnoGet()));
}

/********************************* PrintUosError ****************************
 *
 *  Description: Print MDIS error message
 *			   
 *---------------------------------------------------------------------------
 *  Input......: info	info string
 *  Output.....: -
 *  Globals....: -
 ****************************************************************************/
static void PrintUosError(char *info)
{
	printf("*** can't %s: %s\n", info, UOS_ErrString(UOS_ErrnoGet()));
}

/********************************* SigHandler *******************************
 *
 *  Description: Signal handler
 *			   
 *---------------------------------------------------------------------------
 *  Input......: sigCode	signal code received
 *  Output.....: -
 *  Globals....: -
 ****************************************************************************/
static void __MAPILIB SigHandler(u_int32 sigCode)
{
	if ( sigCode == UOS_SIG_USR1)
		G_SigCountBufHigh++;
	else
		G_SigCountOthers++;
}





