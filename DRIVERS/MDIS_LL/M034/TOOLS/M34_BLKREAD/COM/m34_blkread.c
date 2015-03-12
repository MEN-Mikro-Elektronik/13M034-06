/****************************************************************************
 ************                                                    ************
 ************              M 3 4 _ B L K R E A D                 ************
 ************                                                    ************
 ****************************************************************************
 *  
 *       Author: ds
 *        $Date: 2009/10/19 16:20:15 $
 *    $Revision: 1.4 $
 *
 *  Description: Universal tool to read M34/M35 channels (blockwise) 
 *
 *     Required: Libraries: mdis_api, usr_oss, usr_utl
 *     Switches: -
 *
 *-------------------------------[ History ]---------------------------------
 *
 * $Log: m34_blkread.c,v $
 * Revision 1.4  2009/10/19 16:20:15  KSchneider
 * R: driver ported to MDIS5, new MDIS_API and men_typs
 * M: for backward compatibility to MDIS4 optionally define new types
 *
 * Revision 1.3  2003/10/20 11:22:26  kp
 * made internal functions static
 *
 * Revision 1.2  1998/12/21 14:10:08  see
 * install always signals (for handling deadly signals)
 * print always signal statistics
 *
 * Revision 1.1  1998/12/15 10:30:35  Schmidt
 * Initial Revision
 *
 *---------------------------------------------------------------------------
 * (c) Copyright 1998 by MEN mikro elektronik GmbH, Nuernberg, Germany 
 ****************************************************************************/
 
static const char RCSid[]="$Header: /dd2/CVSR/COM/DRIVERS/MDIS_LL/M034/TOOLS/M34_BLKREAD/COM/m34_blkread.c,v 1.4 2009/10/19 16:20:15 KSchneider Exp $";

#include <stdio.h>
#include <stdlib.h>

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
	printf("Usage: m34_blkread [<opts>] <device> [<opts>]\n");
	printf("Function: Configure and read M34/M35 channels (blockwise) \n");
	printf("Options:\n");
	printf("    device       device name                          [none]\n");
	printf("    -r=<res>     resolution                           [none]\n");
	printf("                  12 = 12-bit (for M34)\n");
	printf("                  14 = 14-bit (for M35)\n");
	printf("    -a=<ch>      first channel to read (0..7/15)      [0]\n");
	printf("    -z=<ch>      last channel to read (0..7/15)       [7/15]\n");
	printf("    -b=<mode>    block i/o mode                       [USRCTRL]\n");
	printf("                  0 = M_BUF_USRCTRL\n");
	printf("                  1 = M_BUF_CURRBUF\n");
	printf("                  2 = M_BUF_RINGBUF\n");
	printf("                  3 = M_BUF_RINGBUF_OVERWR\n");
	printf("    -s=<size>    block size in bytes                  [128]\n");
	printf("    -o=<msec>    block read timeout [msec] (0=none)   [0]\n");
	printf("    -g=<gain>    gain factor for all channels         [x1]\n");			
	printf("                  0 = x1\n");
	printf("                  1 = x2\n");
	printf("                  2 = x4\n");
	printf("                  3 = x8\n");
	printf("                  4 = x16 (on-board jumper must be set !)\n");
	printf("    -m=<mode>    measuring mode                       [unipolar]\n");     
	printf("                  0=unipolar\n");
	printf("                  1=bipolar\n");
	printf("    -d=<mode>    display mode                         [raw hex]\n");			
	printf("                  0 = raw hex value \n");
	printf("                  1 = volt	\n");
	printf("                  2 = ampere (only for gain factor x8)\n");
	printf("    -h           install buffer highwater signal      [no]\n");
	printf("    -l           loop mode                            [no]\n");
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
int main( int argc, char *argv[])
{
	MDIS_PATH	path=0;
	int32       firstCh, lastCh, blkmode, blksize, tout, gainfac;
	int32   	res, gain, mode, disp, signal, loopmode, n, ch, chNbr, gotsize;
	u_int8	    *blkbuf = NULL;
	u_int8	    *bp = NULL;
	u_int8	    *bp0 = NULL;
	u_int8	    *bmax = NULL;
	char	    *device,*str,*errstr,buf[40];
	double	    volt, curr;

	/*--------------------+
    |  check arguments    |
    +--------------------*/
	if ((errstr = UTL_ILLIOPT("a=-r=z=b=s=o=g=m=t=d=hl?", buf))) {	/* check args */
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
	blksize  = ((str = UTL_TSTOPT("s=")) ? atoi(str) : 128);
	tout     = ((str = UTL_TSTOPT("o=")) ? atoi(str) : 0);
	gain     = ((str = UTL_TSTOPT("g=")) ? atoi(str) : 0);
	mode     = ((str = UTL_TSTOPT("m=")) ? atoi(str) : 0);
	disp     = ((str = UTL_TSTOPT("d=")) ? atoi(str) : 0);
	signal   = (UTL_TSTOPT("h") ? 1 : 0);
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

	/* check for valid display mode */
	if ( (disp<0) || (disp>2) ) {
		printf("*** option -d=%d out of range (-d=0..2)\n", disp);
		return(1);
	}

	gainfac = 1 << gain;		/* calculate gain factor */

	/*--------------------+
    |  create buffer      |
    +--------------------*/
	if ((blkbuf = (u_int8*)malloc(blksize)) == NULL) {
		printf("*** can't alloc %d bytes\n",blksize);
		return(1);
	}

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

	/*--------------------+
    |  config             |
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
	/* get number of channels */
	if ((M_getstat(path, M_LL_CH_NUMBER, &chNbr)) < 0) {
		PrintMdisError("getstat M_LL_CH_NUMBER");
		goto abort;
	}

	if (lastCh == -1)
		lastCh = chNbr - 1;

	/* channel specific settings */
	for (ch=0; ch<chNbr; ch++) { 
		/* set current channel */
		if ((M_setstat(path, M_MK_CH_CURRENT, ch)) < 0) {
			PrintMdisError("setstat M_MK_CH_CURRENT");
			goto abort;
		}
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
	printf("first channel       : %d\n",firstCh);
	printf("last channel        : %d\n",lastCh);
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
	printf("gain factor         : x%d\n", gainfac);
	printf("measuring mode      : %s\n",(mode==0 ? "unipolar":"bipolar"));
	printf("buf highwater signal: %s\n",(signal==0 ? "no":"yes"));
	printf("loop mode           : %s\n\n",(loopmode==0 ? "no":"yes"));

    /*--------------------+
    |  enable interrupt   |
    +--------------------*/
	if (blkmode)
		/* enable interrupt */
		if ((M_setstat(path, M_MK_IRQ_ENABLE, 1)) < 0) {
					PrintMdisError("setstat M_MK_IRQ_ENABLE");
			goto abort;
		}

    /*--------------------+
    |  read block         |
    +--------------------*/
	do {
		printf("\nwaiting for data ..\n\n");

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
				printf("%08x+%04x: ",(int32)blkbuf, (int16)(bp-blkbuf));
				
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
	if (blkmode)
		/* disable interrupt */
		if ((M_setstat(path, M_MK_IRQ_ENABLE, 0)) < 0) {
				PrintMdisError("setstat M_MK_IRQ_ENABLE");
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




