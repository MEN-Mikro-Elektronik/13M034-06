/****************************************************************************
 ************                                                    ************
 ************                   M34_SIMP                         ************
 ************                                                    ************
 ****************************************************************************
 *  
 *       Author: uf
 *        $Date: 2009/10/19 16:22:03 $
 *    $Revision: 1.9 $
 *
 *  Description: Simple example program for the M34 driver.
 *               Configure and read all channels from M34 or M35 module.
 *                      
 *     Required: MDIS user interface library
 *     Switches: NO_MAIN_FUNC	(for systems with one namespace)
 *
 *-------------------------------[ History ]---------------------------------
 *
 * $Log: m34_simp.c,v $
 * Revision 1.9  2009/10/19 16:22:03  KSchneider
 * R: driver ported to MDIS5, new MDIS_API and men_typs
 * M: for backward compatibility to MDIS4 optionally define new types
 *
 * Revision 1.8  2003/10/20 11:22:18  kp
 * made internal functions static
 *
 * Revision 1.7  1999/07/21 14:32:59  Franke
 * cosmetics
 *
 * Revision 1.6  1998/12/15 10:30:19  Schmidt
 * parameter 'res' for bit resolution added (for M35 support)
 * function showVolt() removed, use macro M34_CALC_VOLTAGE
 * cosmetics
 *
 * Revision 1.5  1998/07/09 15:23:33  see
 * setstat removed (setting debug level 2)
 * error checks were wrong: MDIS calls must be checked for <0
 *
 * Revision 1.4  1998/07/03 14:49:18  Schmidt
 * includes for VxWorks removed, new define NO_MAIN_FUNC,
 * check if the readed values are valid added, cosmetics
 *
 * Revision 1.3  1998/04/14 11:10:29  Franke
 * conflictes due to merge resolved
 *
 * Revision 1.2  1998/02/23 11:53:47  Schmidt
 * showVolt() : float volt -> double volt
 *
 * Revision 1.1  1998/02/19 16:38:28  franke
 * Added by mcvs
 *
 *---------------------------------------------------------------------------
 * (c) Copyright 1998 by MEN mikro elektronik GmbH, Nuernberg, Germany
 ****************************************************************************/

static char *RCSid="$Id: m34_simp.c,v 1.9 2009/10/19 16:22:03 KSchneider Exp $";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MEN/men_typs.h>
#include <MEN/usr_oss.h>
#include <MEN/mdis_api.h>
#include <MEN/mdis_err.h>

#include <MEN/m34_drv.h>

/*--------------------------------------+
|   DEFINES                             |
+--------------------------------------*/
static const int32 OFF = 0;
static const int32 ON  = 1;

static const int32 T_OK = 0;
static const int32 T_ERROR = 1;

/*--------------------------------------+
|   PROTOTYPES                          |
+--------------------------------------*/
static int m34_simple( char *DeviceName, int32 res );

/******************************** errShow ***********************************
 *
 *  Description:  Show MDIS or OS error message.
 *
 *---------------------------------------------------------------------------
 *  Input......:  -
 *
 *  Output.....:  -
 *
 *  Globals....:  errno
 *
 ****************************************************************************/
static void errShow( void )
{
   u_int32 error;

   error = UOS_ErrnoGet();

   printf("*** %s ***\n",M_errstring( error ) );
}

/******************************** main **************************************
 *
 *  Description: MAIN entry (not used in systems with one namespace)
 *
 *---------------------------------------------------------------------------
 *  Input......:  argc			number of arguments
 *				  *argv			pointer to arguments
 *				  argv[1]		device name
 *				  argv[2]		bit resolution	 
 *
 *  Output.....:  return		0	if no error
 *								1	if error
 *
 *  Globals....:  -
 *
 ****************************************************************************/
int main( int argc, char *argv[ ] )
{
	if( argc < 3){
		printf("usage: m34_simp <device name> <resolution>\n");
		printf("       resolution: 12 = 12-bit (for M34)\n");
		printf("                   14 = 14-bit (for M35)\n");
		return 1;
	}
	m34_simple(argv[1], atoi(argv[2]));
	return 0;
}

/******************************* m34_simple *********************************
 *
 *  Description: Example (directly called in systems with one namespace)
 *					- open path
 *					- config and read bipolar
 *					- config and block-read
 *					- config and read unipolar
 *					- close path
 *
 *---------------------------------------------------------------------------
 *  Input......:  DeviceName	device name
 *                res			bit resolution
 *
 *  Output.....:  return		T_OK or T_ERROR
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int m34_simple( char *DeviceName, int32 res )
{
    MDIS_PATH   fd;
    int32  ch, valL, biPolar, chNbr, i, nbrBytes;
    u_int16 rdVal[20*M34_SINGLE_ENDED_MAX_CH];
    u_int16 *buf;
    u_int16 val;
	double	volt;

    printf("\n%s\n\n", RCSid);
    if( (fd = M_open(DeviceName)) < 0 ) goto M34_TESTERR;

    /*----------------------------+
    |  config and read (bipolar)  |
    +----------------------------*/
    printf("configuration - unipolar\n");
    if( M_setstat( fd, M_MK_IO_MODE, M_IO_EXEC ) < 0 ) goto M34_TESTERR;

    chNbr = 6; /* sample only the first 6 channels */

    for( ch=0; ch< chNbr; ch++)
    {
		M_setstat(fd, M_MK_CH_CURRENT, ch);		/* set current channel */
        if( M_setstat(fd, M34_CH_GAIN, M34_GAIN_1)  < 0 ) goto M34_TESTERR;
        if( M_setstat( fd, M34_CH_BIPOLAR, M34_UNIPOLAR )  < 0 ) goto M34_TESTERR;
    }/*for*/

    biPolar = M34_UNIPOLAR;

    for( i=0; i<3; i++ )
    {
       printf("read\n");
       buf = rdVal;
       for( ch=0; ch< chNbr; ch++)
       {
           M_setstat(fd, M_MK_CH_CURRENT, ch);	/* set current channel */
           if( M_read(fd, &valL )  < 0 ) goto M34_TESTERR;
           *buf = (u_int16) valL;
           buf++;
       }/*for*/

       printf("  channel ");
       for( ch=0; ch< chNbr; ch++) printf("     %2d", (int) ch );
       printf("\n");

       printf("   hex    ");
       buf = rdVal;

       for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
           printf( " 0x%04x", val );
       }/*for*/
       
	   printf("\n");

       printf("   volt   ");
       buf = rdVal;
       
	   for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
		   M34_CALC_VOLTAGE( val, 1, biPolar, res, volt )	
	       printf("   %2.2f", volt);
       }/*for*/
       
	   printf("\n");

       printf("   valid   ");
       buf = rdVal;
       
	   for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
           printf("   %s ", (val & 0x01) ? " NO" : "YES"); 
       }/*for*/
       
	   printf("\n");
    
	}/*for*/

    /*----------------------------+
    |  config and block-read      |
    +----------------------------*/
	printf("\n\nconfigure channels 0,4,5 for M_getblock - M34_CH_RDBLK_IRQ\n");
	
	/* set 3 additional dummy reads for BlkRd/Irq */
    if( M_setstat(fd, M34_DUMMY_READS, 3) < 0 )  goto M34_TESTERR;	

	/* set current chan=0, config chan for blockread */
    if( M_setstat(fd, M_MK_CH_CURRENT, 0) < 0 )  goto M34_TESTERR;	
    if( M_setstat(fd, M34_CH_RDBLK_IRQ, 1)  < 0 ) goto M34_TESTERR;

	/* set current chan=4, config chan for blockread */
    if( M_setstat(fd, M_MK_CH_CURRENT, 4) < 0 )  goto M34_TESTERR;
    if( M_setstat(fd, M34_CH_RDBLK_IRQ, 1)  < 0 ) goto M34_TESTERR;

	/* set current chan=5, config chan for blockread */
    if( M_setstat(fd, M_MK_CH_CURRENT, 5) < 0 )  goto M34_TESTERR;
    if( M_setstat(fd, M34_CH_RDBLK_IRQ, 1)  < 0 ) goto M34_TESTERR;

	/* read 6 byte = 3*2 words */
    nbrBytes = M_getblock(fd, (u_int8*)&rdVal, 6 );	
    if( nbrBytes < 0 ) goto M34_TESTERR;

	printf( "  M_getblock() read %ld byte\n", nbrBytes );
    buf = rdVal;
    val = *buf++;
	printf(" ch0 = 0x%04x\n", val );
    val = *buf++;
	printf(" ch4 = 0x%04x\n", val );
    val = *buf++;
	printf(" ch5 = 0x%04x\n", val );

    /*----------------------------+
    |  config and read (unipolar) |
    +----------------------------*/
    printf("\n\nconfiguration - bipolar\n");
    if( M_setstat( fd, M_MK_IO_MODE, M_IO_EXEC )  < 0 ) goto M34_TESTERR;

    for( ch=0; ch< chNbr; ch++)
    {
        M_setstat(fd, M_MK_CH_CURRENT, ch);			/* set current channel */
        if( M_setstat(fd, M34_CH_GAIN, M34_GAIN_1)  < 0 ) goto M34_TESTERR;
        if( M_setstat( fd, M34_CH_BIPOLAR, M34_BIPOLAR )  < 0 ) goto M34_TESTERR;
    }/*for*/

    biPolar = M34_BIPOLAR;

    for( i=0; i<3; i++ )
    {
       printf("read\n");
       buf = rdVal;

       for( ch=0; ch< chNbr; ch++)
       {
           M_setstat(fd, M_MK_CH_CURRENT, ch);		/* set current channel */
           if( M_read(fd, &valL )  < 0 ) goto M34_TESTERR;
           *buf = (u_int16) valL;
           buf++;
       }/*for*/

       printf("  channel ");
       for( ch=0; ch< chNbr; ch++) printf("     %2d", (int) ch );

       printf("\n   hex    ");
       buf = rdVal;
       
	   for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
           printf( " 0x%04x", val );
       }/*for*/
       
       printf("\n   volt   ");
       buf = rdVal;
       
	   for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
   		   M34_CALC_VOLTAGE( val, 1, biPolar, res, volt )	
	       printf("   %2.2f", volt);
       }/*for*/
       
       printf("\n   valid   ");
       buf = rdVal;

       for( ch=0; ch< chNbr; ch++)
       {
           val = *buf++;
           printf("   %s ", (val & 0x01) ? " NO" : "YES"); 
       }/*for*/
       
	   printf("\n");
    
	}/*for*/

    if( M_close( fd )  < 0 ) goto M34_TESTERR;

    printf("    => OK\n");
    return( 0 );


M34_TESTERR:
    errShow();
    printf("    => Error\n");
    M_close( fd );
    return( 1 );

}/*m34_simple*/



 
