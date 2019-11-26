/****************************************************************************
 ************                                                    ************
 ************                   M34_SIMP                         ************
 ************                                                    ************
 ****************************************************************************
 *  
 *       Author: uf
 *
 *  Description: Simple example program for the M34 driver.
 *               Configure and read all channels from M34 or M35 module.
 *                      
 *     Required: MDIS user interface library
 *     Switches: NO_MAIN_FUNC	(for systems with one namespace)
 *
 *---------------------------------------------------------------------------
 * Copyright 1998-2019, MEN Mikro Elektronik GmbH
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
#include <MEN/mdis_api.h>
#include <MEN/mdis_err.h>

#include <MEN/m34_drv.h>

static const char IdentString[]=MENT_XSTR(MAK_REVISION);

/*--------------------------------------+
|   DEFINES                             |
+--------------------------------------*/
#define OFF 0
#define ON  1

#define T_OK    0
#define T_ERROR 1

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

    printf("\n%s\n\n", IdentString);
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



 
