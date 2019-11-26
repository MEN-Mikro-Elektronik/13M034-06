/***********************  I n c l u d e  -  F i l e  ************************
 *
 *         Name: m34_driver.h
 *
 *      Author: DPfeuffer 
 *
 *  Description: Header file for M34 driver modules
 *               - M34 specific status codes
 *               - M34 function prototypes
 *
 *     Switches: _ONE_NAMESPACE_PER_DRIVER_
 *               _LL_DRV_
 *
 *---------------------------------------------------------------------------
 * Copyright 1997-2019, MEN Mikro Elektronik GmbH
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
#ifndef _M34_LLDRV_H
#  define M34_LLDRV_H

#  ifdef __cplusplus
      extern "C" {
#  endif

/*-----------------------------------------+
|  DEFINES & CONST                         |
+------------------------------------------*/
#define M34_SINGLE_ENDED_MAX_CH     16
#define M34_DIFFERENTIAL_MAX_CH      8


/*--------- M34 specific status codes (MCOD_OFFS...MCOD_OFFS+0xff) --------*/
                                          /* S,G: S=setstat, G=getstat code */
#define M34_CH_GAIN               M_DEV_OF+0x01   /* G,S: gain 1,2,4,8 - 16 is hardwired  */
#define M34_CH_BIPOLAR            M_DEV_OF+0x02   /* G,S: bipolar/unipolar  */
#define M34_CH_RDBLK_IRQ          M_DEV_OF+0x03   /* G,S: read ch in blk rd and irq */
#define M34_EXT_PIN               M_DEV_OF+0x04   /* G  : get status of EXT pin */
#define M34_DUMMY_READS           M_DEV_OF+0x05   /* G  : nbr of see M34_HwBlockRead */
#define M34_SINGLE_ENDED          M_DEV_OF+0x06   /* G  : sing. ended/differential  */
#define M34_IRQ_MODE              M_DEV_OF+0x07   /* G,S: irq mode */

#ifdef WINNT
#define M34_ISR_TIME              M_DEV_OF+0x08   /* G  : accumulated isr time */
#endif

/*------ set/getstat and descriptor values --------*/
#define M34_IS_DIFFERENTIAL		0
#define M34_IS_SINGLE_ENDED		1

#define M34_GAIN_1				0x00
#define M34_GAIN_2				0x01
#define M34_GAIN_4				0x02
#define M34_GAIN_8				0x03

										/* buffer|M_BUF_CURRBUF|irq/ch|auto irq|ext-trig */
										/*       |             |      | en/dis |         */
		                                /* ------+-------------+------+--------+-------- */
#define M34_IMODE_LEGACY		0		/*  yes	 |      yes    |  no  |    no  |   yes   */
#define M34_IMODE_CHIRQ			1		/*	yes  |      yes    | yes  |	   no  |   yes   */
#define M34_IMODE_CHIRQ_AUTO	2		/*	yes  |       no    | yes  |	  yes  |   yes   */
#define M34_IMODE_FIX			3		/*	 no  |       no    | yes  |	  yes  |    no   */

#define M34_UNIPOLAR			0
#define M34_BIPOLAR				1

/******************************* M34_CALC_VOLTAGE ***************************
 *
 *  Description:  Macro for calculating voltage
 *
 *---------------------------------------------------------------------------
 *  Input......:  val		value read
 *                gain      gain factor (1=x1, 2=x2, 4=x4, 8=x8)
 *                mode		measuring mode (0=unipolar, 1=bipolar)
 *                res		resolution (12=12bit, 14=14bit)
 *
 *  Output.....:  volt		calculated voltage in volt
 *
 ****************************************************************************/
#define M34_CALC_VOLTAGE( val, gain, mode, res, volt )						\
		/* unipolar */														\
		if ((mode)==0) {													\
			/* 12-bit */													\
			if ((res)==12)													\
				(volt) = (((u_int16)(val))>>4) * 10.0 / (4096.0 * (gain));	\
			/* 14-bit */													\
			else															\
				(volt) = (((u_int16)(val))>>2) * 10.0 / (16384.0 * (gain));	\
		}																	\
		/* bipolar */														\
		else {																\
			/* 12-bit */													\
			if ((res)==12)													\
				(volt) = (((int16)(val))>>4) * 20.0 / (4096.0 * (gain));	\
			/* 14-bit */													\
			else															\
				(volt) = (((int16)(val))>>2) * 20.0 / (16384.0 * (gain));	\
		}

/******************************* M34_CALC_CURRENT ***************************
 *
 *  Description:  Macro for calculating current
 *
 *---------------------------------------------------------------------------
 *  Input......:  val		value read
 *                mode		measuring mode (0=unipolar, 1=bipolar)
 *                res		resolution (12=12bit, 14=14bit)
 *
 *  Output.....:  curr		calculated current in milli ampere
 *
 ****************************************************************************/
#define M34_CALC_CURRENT( val, mode, res, curr )							\
		/* unipolar */														\
		if ((mode)==0) {													\
			/* 12-bit */													\
			if ((res)==12)													\
				(curr) = (((u_int16)(val))>>4) * 20.0 / 4096.0;				\
			/* 14-bit */													\
			else															\
				(curr) = (((u_int16)(val))>>2) * 20.0 / 16384.0;			\
		}																	\
		/* bipolar */														\
		else {																\
			/* 12-bit */													\
			if ((res)==12)													\
				(curr) = (((int16)(val))>>4) * 40.0 / 4096.0;				\
			/* 14-bit */													\
			else															\
				(curr) = (((int16)(val))>>2) * 40.0 / 16384.0;				\
		}


/* old macros (only for M34) */
#define M34_UCALC_BIPOLAR( val, volt )					\
		(volt) = (((int16)(val))>>4) * 20.0 / 4096.0;

#define M34_UCALC_UNIPOLAR(val )   ( (val >> 4) * (10.0 / 4096.0) )

/*-----------------------------------------+
|  PROTOTYPES                              |
+------------------------------------------*/
#ifndef  M34_VARIANT
# define M34_VARIANT M34
#endif

# define _M34_GLOBNAME(var,name) var##_##name
#ifndef _ONE_NAMESPACE_PER_DRIVER_
# define M34_GLOBNAME(var,name) _M34_GLOBNAME(var,name)
#else
# define M34_GLOBNAME(var,name) _M34_GLOBNAME(M34,name)
#endif


/*-----------------------------------------+
|  PROTOTYPES                              |
+-----------------------------------------*/
#ifdef _LL_DRV_
#define __M34_GetEntry	M34_GLOBNAME(M34_VARIANT,GetEntry)
#ifndef _ONE_NAMESPACE_PER_DRIVER_
	extern void __M34_GetEntry(LL_ENTRY* drvP);
#endif
#endif /* _LL_DRV_ */


/*-----------------------------------------+
|  BACKWARD COMPATIBILITY TO MDIS4         |
+-----------------------------------------*/
#ifndef U_INT32_OR_64
 /* we have an MDIS4 men_types.h and mdis_api.h included */
 /* only 32bit compatibility needed!                     */
 #define INT32_OR_64  int32
 #define U_INT32_OR_64 u_int32
 typedef INT32_OR_64  MDIS_PATH;
#endif /* U_INT32_OR_64 */


#  ifdef __cplusplus
      }
#  endif

#endif/*_M34_LLDRV_H*/


