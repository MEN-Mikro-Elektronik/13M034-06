/*********************  P r o g r a m  -  M o d u l e ***********************
 *
 *         Name: m34_drv.c
 *      Project: M34 module driver
 *
 *       Author: uf
 *
 *  Description: Low level driver for M34/M35 modules
 *
 *               Dummy conversions are required if channel and/or
 *               amplification were previously changed (m34_read,
 *               m34_block_read, m34_irq_c ) by setting
 *               control register. This is caused by delayed
 *               channel switching of the build-in multiplexer.
 *
 *               If Interrupt is enabled, no manual start of conversion
 *               is allowed. In this case M34_Read() returns an error (ERR_LL_READ).
 *               M34_BlockRead() returns then also an error (ERR_LL_READ) if
 *               in M_BUF_USRCTRL buffer mode.
 *
 *               Additional block read delay time can be defined, to add
 *               dummy conversions at block read and interrupt triggered
 *               read (define in descriptor).
 *
 *               Interrupt Modes
 *               ---------------
 *               The driver supports four interrupt modes.
 *               The default mode (lagacy irq mode) was the first implemented
 *               mode. It is still available to be compatible to existing
 *               application software. However, this mode wastes some CPU time in
 *               the ISR and should not be used for new applications.
 *
 *               The fix irq mode is the most efficient mode and requires no external
 *               trigger signal. However, this mode is not so flexible (reads always
 *               all available channels).
 *               
 *               For further information of the interrupt modes, see M34_Init, M34_SetStat
 *               and M34_BlockRead.
 *
 *     Required: OSS, DESC, DBG, ID, MBUF libraries
 *     Switches: _ONE_NAMESPACE_PER_DRIVER_
 *
 *---------------------------------------------------------------------------
 * Copyright (c) 1995-2019, MEN Mikro Elektronik GmbH
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

#include <MEN/men_typs.h>   /* system dependend definitions   */
#include <MEN/dbg.h>		/* debug defines				  */
#include <MEN/oss.h>        /* os services                    */
#include <MEN/mdis_err.h>   /* mdis error numbers             */
#include <MEN/mbuf.h>       /* buffer lib functions and types */
#include <MEN/maccess.h>    /* hw access macros and types     */
#include <MEN/desc.h>       /* descriptor functions           */
#include <MEN/mdis_api.h>   /* global set/getstat codes       */
#include <MEN/mdis_com.h>   /* info function      codes       */
#include <MEN/modcom.h>     /* id prom access                 */

#include <MEN/ll_defs.h>    /* low level driver definitions   */
#include <MEN/ll_entry.h>   /* low level driver entry struct  */
#include <MEN/m34_drv.h>    /* M34 driver header file         */

static const char IdentString[]=MENT_XSTR(MAK_REVISION);

#ifdef WINNT
	#include <wdm.h>
#endif

/*-----------------------------------------+
|  TYPEDEFS                                |
+------------------------------------------*/
typedef struct
{
	MDIS_IDENT_FUNCT_TBL idFuncTbl;						/* id function table */
    int32           ownMemSize;
    u_int32         dbgLevel;
	DBG_HANDLE*		dbgHdl;
    OSS_HANDLE      *osHdl;         
    MACCESS         ma34;
    MBUF_HANDLE     *inbuf;
    OSS_IRQ_HANDLE  *irqHdl;
    u_int32         irqCount;
    u_int32         useModulId;
    u_int32         singleEnded;
    u_int32         nbrOfChannels;
    u_int32         irqIsEnabled;
    u_int32         nbrCfgCh;
    u_int16         chBlkRd[M34_SINGLE_ENDED_MAX_CH];   /* read ch in irq and blk read */
    u_int16         chCtrl[M34_SINGLE_ENDED_MAX_CH];    /* shadow register */
    u_int32         nbrDummyRd;							/* number of dummy reads in HwBlockRead */
	u_int32         irqMode;
	u_int32         isrCurrCh;
	u_int32         skip;							
	u_int32         nbrReadCh;
	u_int32         blkReadReqWords;
	u_int32         blkReadGotWords;
	u_int16			*buf;
	OSS_SEM_HANDLE  *sem;
#ifdef WINNT
	LARGE_INTEGER	isrTicks; /* high-resolution time stamps  */
#endif
} M34_HANDLE;


/*-----------------------------------------+
|  DEFINES & CONST                         |
+------------------------------------------*/
#define M34_CH_WIDTH		2			/* byte */

#define MOD_ID_MAGIC_WORD   0x5346		/* eeprom identification (magic) */
#define	MOD_ID_SIZE			128
#define M34_MOD_ID          34
#define M34_MOD_ID_M35      35

#define M34_DEFAULT_BUF_SIZE	320		/* byte */
#define M34_DEFAULT_BUF_TIMEOUT 1000	/* ms */

#define M34_HW_ACCESS_NO         0
#define M34_HW_ACCESS_PERMITED   1

/*--------------------- M34 register defs/offsets ------------------------*/
#define M34_DATA_RD           0x00  /* read converted data */
#define M34_CTRL_WR           0x00  /* set control data  */
#define M34_DATA_RD_START     0x02  /* read data, start conv. */
#define M34_CTRL_START_WR     0x02  /* start conv., set control data  */
#define M34_DATA_RD_START_INC 0x06  /* read data, start conv., incr. */
#define M34_DATA_START_RD     0x0A  /* start conv., read data */
#define M34_DATA_START_RD_INC 0x0E  /* start conv., read data */
#define M34_MODID			  0xFE  /* module id register */
#define M34_MODID_BUSERBIT	  0x04  /* bit to prevent bus error */

#define CTRL_IRQ			  4		/* bit shifts */
#define CTRL_GAIN			  5		/* bit shifts */
#define CTRL_BIPOLAR		  7		/* bit shifts */

/* debug setting */
#define DBG_MYLEVEL			  m34Hdl->dbgLevel
#define DBH					  m34Hdl->dbgHdl

/*-----------------------------------------+
|  GLOBALS                                 |
+------------------------------------------*/

/*-----------------------------------------+
|  STATICS                                 |
+------------------------------------------*/

/*-----------------------------------------+
|  PROTOTYPES                              |
+------------------------------------------*/
static char* M34_Ident( void );

/*-------------------------- local prototypes -----------------------------*/
static int32 M34_Init( DESC_SPEC       *descSpec,
                           OSS_HANDLE      *osHdl,
                           MACCESS         *ma,
                           OSS_SEM_HANDLE  *devSemHdl,
                           OSS_IRQ_HANDLE  *irqHdl,
                           LL_HANDLE       **llHdlP );

static int32 M34_Exit(        LL_HANDLE **llHdlP );
static int32 M34_Read(        LL_HANDLE *llHdl,  int32 ch,  int32 *value );
static int32 M34_Write(       LL_HANDLE *llHdl,  int32 ch,  int32 value );
static int32 M34_SetStat(     LL_HANDLE *llHdl,  int32 code,  int32 ch, INT32_OR_64 value32_or_64 );
static int32 M34_GetStat(     LL_HANDLE *llHdl,  int32 code,  int32 ch, INT32_OR_64 *value32_or_64P );
static int32 M34_BlockRead(   LL_HANDLE *llHdl,  int32 ch,  void  *buf, int32 size, int32 *nbrRdBytesP );
static int32 M34_BlockWrite(  LL_HANDLE *llHdl,  int32 ch,  void  *buf, int32 size, int32 *nbrWrBytesP);
static int32 M34_Irq(         LL_HANDLE *llHdl );
static int32 M34_Info(        int32     infoType, ... );

static int32 M34_MemCleanup( M34_HANDLE *m34Hdl );

static int32 getStatBlock
(
    M34_HANDLE         *m34Hdl,
    int32              code,
    M_SETGETSTAT_BLOCK *blockStruct
);

static void setGain( u_int16 gain, u_int16 *chCtrlP );
static void setBipolar( u_int16 bipolar, u_int16 *chCtrlP );
static void setIrqEnable( u_int16 irqEnable, M34_HANDLE *m34Hdl );
static int32 getGain( u_int16 chCtrl );
static int32 getBipolar( u_int16 chCtrl );


/*****************************  M34_Ident  **********************************
 *
 *  Description:  Gets the pointer to ident string.
 *
 *
 *---------------------------------------------------------------------------
 *  Input......:  -
 *
 *  Output.....:  return  pointer to ident string
 *
 *  Globals....:  -
 ****************************************************************************/
static char* M34_Ident( void )
{
    return( (char*)IdentString );
}/*M34_Ident*/

/**************************** M34_MemCleanup *********************************
 *
 *  Description:  Deallocates low-level driver structure and
 *                installed signals and buffers.
 *
 *---------------------------------------------------------------------------
 *  Input......:  m34Hdl   m34 low level driver handle
 *
 *  Output.....:  return   0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_MemCleanup( M34_HANDLE *m34Hdl )
{
int32       retCode;

	if( m34Hdl->sem )
		OSS_SemRemove(m34Hdl->osHdl, &m34Hdl->sem);

    /*--------------------------+
    | remove buffer             |
    +--------------------------*/
    if( m34Hdl->inbuf )
       MBUF_Remove( &m34Hdl->inbuf );

	/* cleanup debug */
	DBGEXIT((&DBH));

    /*-------------------------------------+
    | free ll handle                       |
    +-------------------------------------*/
    /* dealloc lldrv memory */
    retCode = OSS_MemFree( m34Hdl->osHdl, (int8*) m34Hdl, m34Hdl->ownMemSize );

    return( retCode );
}/*M34_MemCleanup*/

/**************************** M34_GetEntry *********************************
 *
 *  Description:  Gets the entry points of the low level driver functions.
 *
 *
 *---------------------------------------------------------------------------
 *  Input......:  ---
 *
 *  Output.....:  drvP  pointer to the initialized structure
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
#ifdef _ONE_NAMESPACE_PER_DRIVER_
    void LL_GetEntry( LL_ENTRY* drvP )
#else
    void __M34_GetEntry( LL_ENTRY* drvP )
#endif /* _ONE_NAMESPACE_PER_DRIVER_ */
{

    drvP->init        = M34_Init;
    drvP->exit        = M34_Exit;
    drvP->read        = M34_Read;
    drvP->write       = M34_Write;
    drvP->blockRead   = M34_BlockRead;
    drvP->blockWrite  = M34_BlockWrite;
    drvP->setStat     = M34_SetStat;
    drvP->getStat     = M34_GetStat;
    drvP->irq         = M34_Irq;
    drvP->info        = M34_Info;

}/*M34_GetEntry*/

/******************************** M34_Init ***********************************
 *
 *  Description:  Decodes descriptor, allocates and initializes the low-level
 *                driver structure and initializes the hardware.
 *                Reads and checks the ID if enabled in descriptor.
 *                Clears and disables the module interrupts.
 *                The driver support 16 analog input channels in single-ended
 *                or 8 analog input channels in differential mode.
 *                The channel gain can be set to 1,2,4 or 8. Gain value 16 is
 *                hardwired and the configuration values and SetStats have no
 *                effect.
 *                Only selected channels will be read at M34_BlockRead and
 *                M34_Irq.
 *                The driver supports one buffer with the width of two bytes
 *                for all channels.
 *
 *                Common Desc. Keys   Default          Range/Unit
 *                -----------------------------------------------------------
 *                DEBUG_LEVEL_DESC	  OSS_DBG_DEFAULT  see dbg.h
 *                DEBUG_LEVEL_MBUF    OSS_DBG_DEFAULT  see dbg.h
 *                DEBUG_LEVEL         OSS_DBG_DEFAULT  see dbg.h
 *                ID_CHECK            1                0 or 1
 *
 *                Special Desc. Keys  Default        Range/Unit
 *                -----------------------------------------------------------
 *                M34_SINGLE_ENDED    1              0,1
 *                                                   0-differential ->  8 ch.
 *                                                   1-single ended -> 16 ch.
 *                                                   this value should meet
 *                                                   the type of the used
 *                                                   input module e.g. AD01
 *
 *                M34_DUMMY_READS     0              0..10 number of additional
 *                                                         dummy reads
 *
 *                M34_PREVENT_BUSERR  0              0,1
 *                                                   0-bus error if no voltage
 *                                                     supply
 *                                                   1-no bus error (BI pin
 *                                                     must be connected to GND) 
 *
 *                M34_IRQ_MODE        0    0..3 (M34_IMODE_XXX defines)
 *                                         0-legacy mode
 *                                           read all enabled ch per irq
 *                                           (wastes cpu time in isr)
 *                                         1-one ch per irq mode
 *                                           read one enabled ch per irq
 *                                           (improved isr processing)
 *                                         2-one ch per irq mode with irq enable/disable
 *                                           same as mode 1 but automatically 
 *                                           enables/disables the module interrupt   
 *                                         3-fix mode
 *                                           read always all ch per irq (ignores
 *                                           M34_CH_RDBLK_IRQ) without buffer
 *                                           (ignores RD_BUF)
 *
 *                RD_BUF/SIZE         320            buffer size in byte
 *                                                   (multiple of 2)
 *                RD_BUF/MODE         MBUF_USR_CTRL  buffer mode
 *                RD_BUF/TIMEOUT      1000           timeout in milli sec.
 *                RD_BUF/HIGHWATER    320            high water mark in bytes
 *
 *                CHANNEL_%d/
 *                 M34_GAIN           0              0..3 amplification 1,2,4,8
 *
 *                CHANNEL_%d/
 *                 M34_BIPOLAR        0              0,1 0-unipolar
 *                                                       1-bipolar
 *
 *                CHANNEL_%d/
 *                 M34_CH_RDBLK_IRQ   0              0,1
 *                                                   0-don't read channel at
 *                                                     M34_BlockRead/Irq
 *                                                   1-read chhannel at
 *                                                     M34_BlockRead/Irq
 *
 *---------------------------------------------------------------------------
 *  Input......:  descSpec   pointer to descriptor specifier
 *                osHdl      pointer to the os specific structure
 *                ma         pointer to access handle
 *                devSem     device semaphore for unblocking in wait
 *                irqHdl     irq handle for mask and unmask interrupts
 *                llHdlP     pointer to the variable where low level driver
 *                           handle will be stored
 *
 *  Output.....:  *llHdlP    low level driver handle
 *                return     0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Init
(
    DESC_SPEC       *descSpec,
    OSS_HANDLE      *osHdl,
    MACCESS         *ma,
    OSS_SEM_HANDLE  *devSem,
    OSS_IRQ_HANDLE  *irqHdl,
    LL_HANDLE       **llHdlP
)
{
    M34_HANDLE  *m34Hdl;
    int32       retCode;
    u_int32     gotsize;
    DESC_HANDLE *descHdl;
    int         hwAccess;
    int         modIdMagic;
    int         modId;
    u_int32     ch;
    u_int32     currCh;
    u_int32     gain;
    u_int32     bipolar;
    u_int32     chBlkRd;
    u_int32     inBufferSize;
    u_int32     inBufferTimeout;
    u_int32     mode;
    u_int32     highWater;
    u_int32     dbgLevelDesc;
    u_int32     dbgLevelMbuf;
	u_int32		preventBusErr;


    hwAccess = M34_HW_ACCESS_NO;

    retCode = DESC_Init( descSpec, osHdl, &descHdl );
    if( retCode )
    {
        return( retCode );
    }/*if*/

    /*-------------------------------------+
    |  LL-HANDLE allocate and initialize   |
    +-------------------------------------*/
    m34Hdl = (M34_HANDLE*) OSS_MemGet( osHdl, sizeof(M34_HANDLE), &gotsize );
    if( m34Hdl == NULL )
    {
       *llHdlP = 0;
       return( ERR_OSS_MEM_ALLOC );
    }/*if*/

    *llHdlP = (LL_HANDLE*) m34Hdl;

    /* fill turkey with 0 */
    OSS_MemFill( osHdl, gotsize, (char*) m34Hdl, 0 );

    /*------------------------------+
    |  init id function table       |
    +------------------------------*/
	/* drivers ident function */
	 m34Hdl->idFuncTbl.idCall[0].identCall = M34_Ident;
	 /* libraries ident functions */
     m34Hdl->idFuncTbl.idCall[1].identCall = MBUF_Ident;
	 m34Hdl->idFuncTbl.idCall[2].identCall = DESC_Ident;
	 m34Hdl->idFuncTbl.idCall[3].identCall = OSS_Ident;
	 /* terminator */
	 m34Hdl->idFuncTbl.idCall[4].identCall = NULL;

	DBG_MYLEVEL = OSS_DBG_DEFAULT;
	DBGINIT((NULL,&DBH));

    /*-------------------------------------+
    |   get DEBUG LEVEL                    |
    +-------------------------------------*/
    /* DEBUG_LEVEL_DESC */
    retCode = DESC_GetUInt32( descHdl,
                              OSS_DBG_DEFAULT,
                              &dbgLevelDesc,
                              "DEBUG_LEVEL_DESC",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
	DESC_DbgLevelSet(descHdl, dbgLevelDesc);
    retCode = 0;

    /* DEBUG_LEVEL_MBUF */
    retCode = DESC_GetUInt32( descHdl,
                              OSS_DBG_DEFAULT,
                              &dbgLevelMbuf,
                              "DEBUG_LEVEL_MBUF",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    /* DEBUG_LEVEL - LL */
    retCode = DESC_GetUInt32( descHdl,
                              OSS_DBG_DEFAULT,
                              &m34Hdl->dbgLevel,
                              "DEBUG_LEVEL",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    DBGWRT_1((DBH, "LL - M34_Init: entered\n"));

    m34Hdl->ownMemSize = gotsize;
    m34Hdl->osHdl      = osHdl;
    m34Hdl->ma34       = *ma;
    m34Hdl->irqHdl     = irqHdl;
    m34Hdl->nbrCfgCh   = 0;


    /*-------------------------------+
    |  descriptor - nbr dummy reads  |
    +-------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              0,
                              &m34Hdl->nbrDummyRd,
                              "M34_DUMMY_READS",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    /*-------------------------------+
    |  descriptor - fast irq mode    |
    +-------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              0,
                              &m34Hdl->irqMode,
                              "M34_IRQ_MODE",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

	/*--------------------------------------------+
    |  descriptor - single ended or differential  |
    +--------------------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              M34_IS_SINGLE_ENDED,
                              &m34Hdl->singleEnded,
                              "M34_SINGLE_ENDED",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    if( m34Hdl->singleEnded == M34_IS_SINGLE_ENDED )
        m34Hdl->nbrOfChannels = M34_SINGLE_ENDED_MAX_CH;
    else
        m34Hdl->nbrOfChannels = M34_DIFFERENTIAL_MAX_CH;

    for( ch=0; ch < m34Hdl->nbrOfChannels; ch++ )
    {
        /* init ch ctrl array - unipolar, irq=disabled, gain=1, ch nbr */
        m34Hdl->chCtrl[ch] = (u_int16) ch;
    }/*for*/

    /*-------------------------------------+
    |  creates read 1 buffer for all ch    |
    +-------------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              M34_DEFAULT_BUF_SIZE,
                              &inBufferSize,
                              "RD_BUF/SIZE",
                              0 );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    retCode = DESC_GetUInt32( descHdl,
                              M_BUF_USRCTRL,
                              &mode,
                              "RD_BUF/MODE",
                              0 );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    retCode = DESC_GetUInt32( descHdl,
                              M34_DEFAULT_BUF_TIMEOUT,
                              &inBufferTimeout,
                              "RD_BUF/TIMEOUT",
                              0 );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    retCode = DESC_GetUInt32( descHdl,
                              M34_DEFAULT_BUF_SIZE,
                              &highWater,
                              "RD_BUF/HIGHWATER",
                              0 );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    retCode = MBUF_Create( osHdl, devSem, m34Hdl, inBufferSize,
                           M34_CH_WIDTH,
                           mode,
                           MBUF_RD, highWater, inBufferTimeout,
                           m34Hdl->irqHdl,
                           &m34Hdl->inbuf );
    if( retCode ) goto CLEANUP;

    /*----------------------------+
    |  set debug level for MBUF   |
    +----------------------------*/
	MBUF_SetStat(
		m34Hdl->inbuf,
		NULL,
		M_BUF_RD_DEBUG_LEVEL,
		dbgLevelMbuf);

    /*-------------------+
    |   current channel  |
    +-------------------*/
    currCh = 0;

    /*----------------------------+
    |  descriptor - blk rd | irq  |
    +----------------------------*/
    for( ch=0; ch < m34Hdl->nbrOfChannels; ch++ )
    {
        retCode = DESC_GetUInt32( descHdl,
                                  0,
                                  &chBlkRd,
                                  "CHANNEL_%d/M34_CH_RDBLK_IRQ",
                                  ch );
        if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
        m34Hdl->chBlkRd[ch] = (u_int16) chBlkRd;
        if( chBlkRd )
            m34Hdl->nbrCfgCh++;
    }/*for*/
    retCode = 0;

    /*--------------------+
    |  descriptor - gain  |
    +--------------------*/
    for( ch=0; ch < m34Hdl->nbrOfChannels; ch++ )
    {
        retCode = DESC_GetUInt32( descHdl,
                                  M34_GAIN_1,
                                  &gain,
                                  "CHANNEL_%d/M34_GAIN",
                                  ch );
        if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;

        if( M34_GAIN_8 < gain ) /* not Valid */
        {
			DBGWRT_ERR((DBH,"*** LL - M34_Init: M34_GAIN for ch %d invalid\n", ch));
            retCode = ERR_LL_DESC_PARAM;
            goto CLEANUP;
        }/*if*/
        setGain( (u_int16)gain, &m34Hdl->chCtrl[ch] );
    }/*for*/
    retCode = 0;

    /*---------------------------+
    |  descriptor - uni/bipolar  |
    +---------------------------*/
    for( ch=0; ch < m34Hdl->nbrOfChannels; ch++ )
    {
        retCode = DESC_GetUInt32( descHdl,
                                  M34_UNIPOLAR,
                                  &bipolar,
                                  "CHANNEL_%d/M34_BIPOLAR",
                                  ch );
        if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
        if( bipolar != M34_BIPOLAR && bipolar != M34_UNIPOLAR  ) /* not Valid */
        {
			DBGWRT_ERR((DBH,"*** LL - M34_Init: M34_BIPOLAR for ch %d invalid\n", ch));
            retCode = ERR_LL_DESC_PARAM;
            goto CLEANUP;
        }/*if*/
        setBipolar( (u_int16)bipolar, &m34Hdl->chCtrl[ch] );
    }/*for*/
    retCode = 0;
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

    /*-------------------------------------+
    |  descriptor - use module id ?        |
    +-------------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              1,
                              &m34Hdl->useModulId,
                              "ID_CHECK",
                              NULL );
    if( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;


    if( m34Hdl->useModulId != 0 )
    {
        modIdMagic = m_read((U_INT32_OR_64)m34Hdl->ma34, 0);
        modId      = m_read((U_INT32_OR_64)m34Hdl->ma34, 1);
        if( modIdMagic != MOD_ID_MAGIC_WORD )
        {
			 DBGWRT_ERR((DBH,"*** LL - M34_Init: illegal magic id\n"));
             retCode = ERR_LL_ILL_ID;
             goto CLEANUP;
        }/*if*/

        if( (modId != M34_MOD_ID) && (modId != M34_MOD_ID_M35 ) )
        {
			 DBGWRT_ERR((DBH,"*** LL - M34_Init: illegal module id\n"));
             retCode = ERR_LL_ILL_ID;
             goto CLEANUP;
        }/*if*/
    }/*if*/

    /*--------------------------------+
    |  descriptor - prevent bus error |
    +--------------------------------*/
    retCode = DESC_GetUInt32( descHdl,
                              0,
                              &preventBusErr,
                              "M34_PREVENT_BUSERR",
                              NULL );
    if ( retCode != 0 && retCode != ERR_DESC_KEY_NOTFOUND ) goto CLEANUP;
    retCode = 0;

	retCode = OSS_SemCreate(osHdl, OSS_SEM_BIN, 0, &m34Hdl->sem);
	if (retCode)
		goto CLEANUP;

    /*--------------------------+
    | default hw config         |
    +--------------------------*/
    hwAccess = M34_HW_ACCESS_PERMITED;
    MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[currCh] );

    if (preventBusErr) { 
		/* set the bit that prevent the bus error */
		MSETMASK_D16( m34Hdl->ma34, M34_MODID, M34_MODID_BUSERBIT );
	}
	else {
		/* clear the bit that prevent the bus error */
		MCLRMASK_D16( m34Hdl->ma34, M34_MODID, M34_MODID_BUSERBIT );
	}

    DESC_Exit( &descHdl );
    return( retCode );

 CLEANUP:
    DESC_Exit( &descHdl );
    if( hwAccess == M34_HW_ACCESS_PERMITED )
        M34_Exit( llHdlP );
    else
    {
        M34_MemCleanup( m34Hdl );
        *llHdlP = 0;
    }/*if*/

    return( retCode );

}/*M34_Init*/

/****************************** M34_Exit *************************************
 *
 *  Description:  Deinits HW, disables interrupts, frees allocated memory.
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdlP     pointer to the variable where low level driver
 *                           handle will be stored
 *
 *  Output.....:  *llHdlP    NULL
 *                return     0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Exit
(
    LL_HANDLE **llHdlP
)
{
    M34_HANDLE  *m34Hdl = (M34_HANDLE*) *llHdlP;
    int32       retCode;

    DBGWRT_1((DBH, "LL - M34_Exit: entered\n"));

    /*--------------------------+
    | def values / disable irq  |
    +--------------------------*/
    MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, 0 );

    retCode = M34_MemCleanup( m34Hdl );
    *llHdlP = 0;

    return( retCode );
}/*M34_Exit*/

/****************************** M34_Read *************************************
 *
 *  Description:  Reads input state from current channel.
 *                Set the current channel gain and mode, then  starts a dummy
 *                conversion.
 *                Starts a second conversion and reads the sampled value.
 *
 *                byte structure of value
 *
 *                bit           15   14..5    4    3..2   1     0
 *                           +-----+- - - -+-----+- - -+-----+-----+
 *                meaning    | msb |       | lsb |  no |  E  |  S  |
 *                           +-----+- - - -+-----+- - -+-----+-----+
 *
 *                The converted value is stored in bit 15..4.
 *                Bit 1 shows the status of the external pin.
 *                                         ( 0 connected to ground )
 *                Bit 0 is zero if the measuring value is valid.
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl   pointer to low-level driver data structure
 *                ch      current channel 0..15 (7-differential)
 *                valueP  pointer to variable where read value stored
 *
 *  Output.....:  *valueP  read value 0=low, 1=high
 *                return   0 | error code
 *                             ERR_LL_READ - if irq enabled
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Read
(
    LL_HANDLE   *llHdl,
    int32       ch,
    int32       *valueP
)
{
    M34_HANDLE*       m34Hdl = (M34_HANDLE*) llHdl;
    u_int16 dummy;
    u_int16 dummy2;


    DBGWRT_1((DBH, "LL - M34_Read: from ch=%d\n",ch));

    if( m34Hdl->irqIsEnabled )
       return( ERR_LL_READ );        /* can't read ! */

    /*--------------------+
    |  set current ch     |
    +--------------------*/
    MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch] );

    /*----------------------+
    |  star conversion & rd |
    +----------------------*/
    dummy   = MREAD_D16( m34Hdl->ma34, M34_DATA_START_RD ); /* dummy conversion */
    dummy2  = MREAD_D16( m34Hdl->ma34, M34_DATA_START_RD ); /* conversion */
    *valueP = dummy2;

    return(0);
}/*M34_Read*/

/****************************** M34_Write ************************************
 *
 *  Description:  Write value to device
 *
 *                The function is not supported and returns always an
 *                ERR_LL_ILL_FUNC error.
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl  pointer to low-level driver data structure
 *                ch     current channel
 *                value  value to write
 *
 *  Output.....:  return ERR_LL_ILL_FUNC
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Write
(
    LL_HANDLE *llHdl,
    int32  ch,
    int32  value
)
{
    DBGCMD( M34_HANDLE*       m34Hdl = (M34_HANDLE*) llHdl);
    DBGWRT_1((DBH, "LL - M34_Read: M34_Write: to ch=%d\n",ch));

    return( ERR_LL_ILL_FUNC );
}/*M34_Write*/

/****************************** M34_SetStat **********************************
 *
 *  Description:  Set the device state.
 *
 *  COMMON CODES      CH       VALUES      MEANING
 *  -------------------------------------------------------------------------
 *  M_LL_DEBUG_LEVEL  all      see dbg.h   debug level of the low level driver
 *
 *  M_LL_CH_DIR       all      M_CH_IN     direction of current channel
 *
 *  M_LL_IRQ_COUNT    all      0..max      interrupt counter
 *
 *  M_MK_IRQ_ENABLE   all      0,1         irq mode M34_IMODE_LEGACY/_CHIRQ:
 *                                          0 - disables module interrupt
 *                                          1 - enables module interrupt
 *                                         irq mode M34_IMODE_CHIRQ_AUTO/_FIX:
 *                                          no operation
 *                                          
 *
 *  SPECIAL CODES     CH       VALUES      MEANING
 *  -------------------------------------------------------------------------
 *  M34_CH_GAIN       current  0..3        set gain of current channel
 *                                         see M34_drv.h
 *                                         M34_GAIN_1, M34_GAIN_2
 *                                         M34_GAIN_4, M34_GAIN_8
 *
 *
 *  M34_CH_BIPOLAR    current  0,1         set mode of current channel
 *                                         0 (M34_UNIPOLAR) - unipolar mode
 *                                         1 (M34_BIPOLAR)  - bipolar mode
 *
 *  M34_CH_RDBLK_IRQ  current  0,1         0 - don't read channel in BlkRd/Irq
 *                                         1 - read channel in BlkRd/Irq
 *
 *  M34_DUMMY_READS   all      0..10       additional dummy reads in BlkRd/Irq
 *
 *  M34_IRQ_MODE      all      0..3       interrupt mode (see M34_Init)
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl             pointer to low-level driver data structure
 *                code              setstat code
 *                ch                current channel 0..15 (7-differential)
 *                                  ( sometimes ignored )
 *                value32_or_64     setstat value or pointer to blocksetstat data
 *
 *  Output.....:  return  0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_SetStat
(
    LL_HANDLE *llHdl,
    int32  code,
    int32  ch,
    INT32_OR_64 value32_or_64 
)
{
    M34_HANDLE *m34Hdl = (M34_HANDLE*) llHdl;
    int32       value  = (int32) value32_or_64;

    DBGWRT_1((DBH, "LL - M34_SetStat: code=$%04lx, ch=%d, data=%ld\n",code,ch,value));

    switch(code)
    {
        /* ------ common setstat codes --------- */
        /*--------------------+
        |  irq enable         |
        +--------------------*/
        case M_MK_IRQ_ENABLE:

			/* irq mode without auto irq enable/disable? */
			if ((m34Hdl->irqMode == M34_IMODE_LEGACY) ||
				(m34Hdl->irqMode == M34_IMODE_CHIRQ)) {

				setIrqEnable( (u_int16)(value == 0 ? 0 : 1), m34Hdl );

				m34Hdl->isrCurrCh = 0;
				m34Hdl->nbrReadCh = 0;
				MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch] );
			}
          break;

        /*------------------+
        |  ch direction     |
        +------------------*/
        case M_LL_CH_DIR:
            if( value != M_CH_IN )
                return( ERR_LL_ILL_DIR );
            break;

        /*------------------+
        |  irq count        |
        +------------------*/
        case M_LL_IRQ_COUNT:
            m34Hdl->irqCount = value;
            break;

        /*------------------+
        |  debug level      |
        +------------------*/
        case M_LL_DEBUG_LEVEL:
            m34Hdl->dbgLevel = value;
            break;



        /* ------ special setstat codes --------- */
        /*------------------+
        |   gain            |
        +------------------*/
        case M34_CH_GAIN:
          if( value < M34_GAIN_1 || M34_GAIN_8 < value ) /* not Valid */
          {
                return( ERR_LL_ILL_PARAM );
          }
          else            /* valid */
          {
              setGain( (u_int16) value, &m34Hdl->chCtrl[ch] );
          }/*if*/

          MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch] );
          break;


        /*------------------+
        |  uni/bipolar      |
        +------------------*/
        case M34_CH_BIPOLAR:
          if( value != M34_BIPOLAR &&
              value != M34_UNIPOLAR ) /* not Valid */
          {
                return( ERR_LL_ILL_PARAM );
          }
          else            /* valid */
          {
              setBipolar( (u_int16) value, &m34Hdl->chCtrl[ch] );
          }/*if*/

          MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch] );
          break;

        /*------------------+
        |  blk rd irq       |
        +------------------*/
        case M34_CH_RDBLK_IRQ:
		  if ( (value < 0) || (value > 1) )
		  {
			return( ERR_LL_ILL_PARAM );
		  }
		  if ( m34Hdl->chBlkRd[ch] != (u_int16)value )
		  {
			/* update number of configured channels */
			value ? m34Hdl->nbrCfgCh++ : m34Hdl->nbrCfgCh--;
			m34Hdl->chBlkRd[ch] = (u_int16)value;
		  }
          break;

        /*------------------+
        |  dummy reads      |
        +------------------*/
        case M34_DUMMY_READS:
          if( value < 0 || 10 < value ) /* not Valid */
          {
              return( ERR_LL_ILL_PARAM );
          }
          else            /* valid */
          {
              m34Hdl->nbrDummyRd = value;
          }/*if*/
          break;

		/*------------------+
		|  fast irq mode    |
		+------------------*/
		case M34_IRQ_MODE:
			if ((value < 0) || (value > 3))
			{
				return(ERR_LL_ILL_PARAM);
			}
			else            /* valid */
			{
				m34Hdl->irqMode = value;
			}/*if*/
			break;

        /*--------------------+
        |  (unknown)          |
        +--------------------*/
        default:
            if(    ( M_RDBUF_OF <= code && code <= (M_WRBUF_OF+0x0f) )
                || ( M_RDBUF_BLK_OF <= code && code <= (M_RDBUF_BLK_OF+0x0f) )
              )
                return( MBUF_SetStat( m34Hdl->inbuf,
                                      NULL,
                                      code,
                                      value ) );

            return(ERR_LL_UNK_CODE);
    }/*switch*/

    return(0);
}/*M34_SetStat*/

/****************************** M34_GetStat **********************************
 *
 *  Description:  Changes the device state.
 *
 *  COMMON CODES       CH       VALUES       MEANING
 *  -------------------------------------------------------------------------
 *  M_LL_CH_NUMBER      -       16/8         number of channels
 *
 *  M_LL_CH_DIR         -       M_CH_IN      direction of curr ch
 *
 *  M_LL_CH_LEN         -       16           channel length in bit
 *
 *  M_LL_CH_TYP         -       M_CH_ANALOG  channel type
 *
 *  M_LL_IRQ_COUNT      -       0..max       interrupt counter
 *
 *  M_LL_ID_CHECK       -       0,1          1 - check EEPROM-Id from module
 *                                               in init
 *
 *  M_LL_DEBUG_LEVEL    -       see dbg.h    current debug level of the
 *                                           low-level driver
 *
 *  M_LL_ID_SIZE		-		128			 eeprom size [bytes]	
 *
 *  M_LL_BLK_ID_DATA    -                    read the M-Module ID from EEPROM
 *   ((M_SETGETSTAT_BLOCK*)valueP)->size     number of bytes to read
 *   ((M_SETGETSTAT_BLOCK*)valueP)->data     user buffer for the ID data
 *
 *  M_MK_BLK_REV_ID     -       -            pointer to ident function table
 *
 *  M_BUF_RD_MODE       -     M_BUF_USRCTRL  mode of read buffer
 *                            M_BUF_CURRBUF
 *                            M_BUF_RINGBUF
 *                            M_BUF_RINGBUF_OVERWR
 *
 *
 *  SPECIAL CODES       CH       VALUES      MEANING
 *  -------------------------------------------------------------------------
 *  M34_CH_GAIN         current  0..3        set gain of current channel
 *                                           see M34_drv.h
 *                                           M34_GAIN_1, M34_GAIN_2
 *                                           M34_GAIN_4, M34_GAIN_8
 *
 *  M34_CH_BIPOLAR      current  0,1         set mode of current channel
 *                                           0 (M34_UNIPOLAR) - unipolar mode
 *                                           1 (M34_BIPOLAR)  - bipolar mode
 *
 *  M34_CH_RDBLK_IRQ    current  0,1         0 - channel isn't read at
 *                                               M34_BlockRead/Irq
 *                                           1 - channel is read at
 *                                               M34_BlockRead/Irq
 *
 *  M34_SINGLE_ENDED    -        0,1         0 - M34_IS_DIFFERENTIAL
 *                                           1 - M34_IS_SINGLE_ENDED
 *
 *  M34_EXT_PIN         -        0,1         0 - extern pin is
 *                                               connected to ground
 *                                           1 - extern pin is open or high
 *
 *  M34_DUMMY_READS     all      0..10       additional dummy reads in
 *                                           M34_BlockRead/Irq
 *
 *  M34_IRQ_MODE        all      0..3        interrupt mode (see M34_Init)                                           
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl             pointer to low-level driver data structure
 *                code              setstat code
 *                ch                current channel 0..15 (7-differential)
 *                                  ( sometimes ignored )
 *                value32_or_64P    pointer to variable where value stored or
 *                                  pointer to blocksetstat data
 *
 *  Output.....:  *value32_or_64P   getstat value or pointer to blocksetstat data
 *                return            0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_GetStat
(
    LL_HANDLE *llHdl,
    int32  code,
    int32  ch,
    INT32_OR_64 *value32_or_64P 
)
{
    M34_HANDLE  *m34Hdl    = (M34_HANDLE*) llHdl;
    int32       *valueP    = (int32 *) value32_or_64P;
    INT32_OR_64 *value64P  = value32_or_64P;
    u_int16 dummy;

    DBGWRT_1((DBH, "LL - M34_GetStat: code=$%04lx, ch=%d\n",code,ch));

    switch(code)
    {
        /* ------ common getstat codes --------- */
        /*------------------+
        |  get ch count     |
        +------------------*/
        case M_LL_CH_NUMBER:
            *valueP = m34Hdl->nbrOfChannels;
            break;

        /*------------------+
        |  ch direction     |
        +------------------*/
        case M_LL_CH_DIR:
             *valueP = M_CH_IN;
            break;

        /*--------------------+
        |  ch length (bit)    |
        +--------------------*/
        case M_LL_CH_LEN:
             *valueP = 16;
             break;

        /*------------------+
        |  ch typ           |
        +------------------*/
        case M_LL_CH_TYP:
             *valueP = M_CH_ANALOG;
             break;

        /*------------------+
        |  id check enabled |
        +------------------*/
        case M_LL_ID_CHECK:
            *valueP = m34Hdl->useModulId;
            break;

        /*------------------+
        |  irq count        |
        +------------------*/
        case M_LL_IRQ_COUNT:
            *valueP = m34Hdl->irqCount;
            break;

        /*------------------+
        |  debug level      |
        +------------------*/
        case M_LL_DEBUG_LEVEL:
            *valueP = m34Hdl->dbgLevel;
            break;

        /*--------------------+
        |  ident table        |
        +--------------------*/
        case M_MK_BLK_REV_ID:
           *value64P = (INT32_OR_64) &m34Hdl->idFuncTbl;
           break;

        /*--------------------------+
        |   id prom size            |
        +--------------------------*/
        case M_LL_ID_SIZE:
            *valueP = MOD_ID_SIZE;
            break;

        /* ------ special getstat codes --------- */
        /*------------------+
        |   gain            |
        +------------------*/
        case M34_CH_GAIN:
          *valueP = getGain( m34Hdl->chCtrl[ch] );
          break;


        /*------------------+
        |  uni/bipolar      |
        +------------------*/
        case M34_CH_BIPOLAR:
          *valueP = getBipolar( m34Hdl->chCtrl[ch] );
          break;

        /*--------------------+
        |  single ended /diff |
        +--------------------*/
        case M34_SINGLE_ENDED:
          *valueP = m34Hdl->singleEnded;
          break;

        /*------------------+
        |  extern pin read  |
        +------------------*/
        case M34_EXT_PIN:
          dummy   = MREAD_D16( m34Hdl->ma34, M34_DATA_RD ); /* read no conversion */
          *valueP = (dummy & 0x02 ? 1 : 0);    /* read data (bit 1) */
          break;

        /*------------------+
        |  blk rd irq       |
        +------------------*/
        case M34_CH_RDBLK_IRQ:
          *valueP = (int32) m34Hdl->chBlkRd[ch];
          break;

        /*------------------+
        |  dummy reads      |
        +------------------*/
        case M34_DUMMY_READS:
          *valueP = m34Hdl->nbrDummyRd;
          break;

		/*------------------+
		|   fast irq mode    |
		+------------------*/
		case M34_IRQ_MODE:
			*valueP = m34Hdl->irqMode;
			break;

#ifdef WINNT
		  /*------------------+
		  |  isr time         |
		  +------------------*/
		case M34_ISR_TIME:
		{
			LARGE_INTEGER	freq, us; 
			KeQueryPerformanceCounter(&freq);

			us.QuadPart = m34Hdl->isrTicks.QuadPart;
			us.QuadPart *= 1000000;
			us.QuadPart /= freq.QuadPart;

			*valueP = (int32)us.LowPart;
			m34Hdl->isrTicks.QuadPart = 0;
			break;
		}
#endif

        /*--------------------+
        |  (unknown)          |
        +--------------------*/
        default:
            if(    ( M_LL_BLK_OF <= code && code <= (M_LL_BLK_OF+0xff) )
                || ( M_DEV_BLK_OF <= code && code <= (M_DEV_BLK_OF+0xff) )
              )
                return( getStatBlock( m34Hdl, code, (M_SETGETSTAT_BLOCK*) value64P ) );

            if(    ( M_RDBUF_OF <= code && code <= (M_WRBUF_OF+0x0f) )
                || ( M_RDBUF_BLK_OF <= code && code <= (M_RDBUF_BLK_OF+0x0f) )
              )
                return( MBUF_GetStat( m34Hdl->inbuf,
                                      NULL,
                                      code,
                                      valueP ) );

            return(ERR_LL_UNK_CODE);

    }/*switch*/

    return(0);
}/*M34_GetStat*/

/******************************* M34_BlockRead *******************************
 *
 *  Description:  Reads all configured channels to buffer.
 *                The read sequence is from lowest configured channel number to
 *                the highest.
 *                Supported buffer modes:
 *                  M_BUF_USRCTRL         reads from hw
 *                  M_BUF_RINGBUF         reads from buffer (uses irq)
 *                  M_BUF_RINGBUF_OVERWR    "     "     "       "
 *                  M_BUF_CURRBUF           "     "     "       "
 *
 *                buffer structure
 *
 *                word          0     ...      N
 *                           +-----+- - - -+-------+
 *                meaning    | CC1 |  ...  | CCN+1 |
 *                           +-----+- - - -+-------+
 *
 *                             CC1   - first configured channel (see M34_CH_RDBLK_IRQ)
 *                             CCN+1 - last configured channel
 *
 *                byte structure of value
 *
 *                bit           15   14..5    4    3..2   1     0
 *                           +-----+- - - -+-----+- - -+-----+-----+
 *                meaning    | msb |       | lsb |  no |  E  |  S  |
 *                           +-----+- - - -+-----+- - -+-----+-----+
 *
 *                  The converted value is stored in bit 15..4.
 *                  Bit 1 shows the status of the external pin.
 *                                         ( 0 connected to ground )
 *                  Bit 0 is zero if the measuring value is valid.
 *
 *                The behaviour of this function depends on the interrupt mode:
 *                M34_IMODE_LEGACY (=0): legacy mode
 *                  - read all enabled ch per irq (wastes cpu time in isr)
 *                  - the module interrupt is enabled/disabled with M_MK_IRQ_ENABLE
 *                    setstat call
 *                  - supported for compatibility with existing applications
 *                  - not recommended for new applications
 *
 *                M34_IMODE_CHIRQ (=1): one ch per irq mode
 *                  - read one enabled ch per irq (improved isr processing)
 *                  - the module interrupt is enabled/disabled with M_MK_IRQ_ENABLE
 *                    setstat call
 *                  - size must be multiple of enabled channels to read x2
 *
 *                M34_IMODE_CHIRQ_AUTO (=2): one ch per irq mode with irq enable/disable
 *                  - read one enabled ch per irq (improved isr processing)
 *                  - the module interrupt is automatically enabled/disabled
 *                    (this reduces the irq amount)
 *                  - size must be multiple of enabled channels to read x2
 *                  - buffer mode M_BUF_CURRBUF is not supported
 *
 *                M34_IMODE_FIX (=3): fix mode
 *                  - read always all available ch per irq (ignores M34_CH_RDBLK_IRQ config)
 *                  - the module interrupt is automatically enabled/disabled
 *                  - size must be the number of available channels x2
 *                  - without buffer usage (ignores RD_BUF/.. config)
 *                  - this is the only irq mode that operates without an external trigger
 *                     (the other irq modes with buffer mode CURRBUF or RINGBUF requires
 *                      an external trigger signal to generate irqs)
 *                  - this is the fastest and less cpu time consuming mode
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl  pointer to low-level driver data structure
 *                ch     current channel (always ignored)
 *                buf    buffer to store read values
 *                size   byte size to read (should be multiple of 2)
 *
 *  Output.....:  nbrRdBytesP  number of read bytes
 *                return       0 | error code
 *                             ERR_LL_READ - if irq enabled and M_BUF_USRCTRL
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_BlockRead
(
    LL_HANDLE *llHdl,
    int32     ch,
    void      *buf,
    int32     size,
    int32     *nbrRdBytesP
)
{
    M34_HANDLE *m34Hdl = (M34_HANDLE*) llHdl;
    u_int32    t;
    u_int32    nbrOfReads;
    u_int16    dummy, dummy2;
    u_int16    *bufP;
    u_int32    haveRead;
    int32      fktRetCode;
    int32      bufMode;

    DBGWRT_1((DBH, "LL - M34_BlockRead: entered\n"));

	*nbrRdBytesP = 0;

	/*---------------------------------------------------------------------------------+
	|  F I X   I R Q   M O D E                                                         |
	+---------------------------------------------------------------------------------*/
	if (m34Hdl->irqMode == M34_IMODE_FIX) {

		/* one block read store the data for all available channels */
		if ( size != 2 * m34Hdl->nbrOfChannels ){
			DBGWRT_ERR((DBH,
				"*** LL - M34_BlockRead: illegal byte size (M34_IMODE_FIX)\n"));
			return ERR_LL_ILL_PARAM;
		}

		m34Hdl->isrCurrCh = 0;
		m34Hdl->buf = (u_int16*)buf;
		m34Hdl->skip = 1;
		
		/* enable irq */
		DBGWRT_2((DBH, " enable irq\n"));
		setIrqEnable(1, m34Hdl);

		/* start, set control data */
		MWRITE_D16(m34Hdl->ma34, M34_CTRL_START_WR, m34Hdl->chCtrl[m34Hdl->isrCurrCh]);

		/* wait for data */
		fktRetCode = OSS_SemWait(m34Hdl->osHdl, m34Hdl->sem, 100);
		if (fktRetCode){
			DBGWRT_ERR((DBH,
				"*** LL - M34_BlockRead: no data gotten (fktRetCode=0x%x)\n"));
			return fktRetCode;
		}

		*nbrRdBytesP = 2 * m34Hdl->nbrOfChannels;
	}
	/*---------------------------------------------------------------------------------+
	|  I R Q   M O D E   W I T H   B U F F E R                                         |
	+---------------------------------------------------------------------------------*/
	else{
		bufP     = (u_int16*) buf;
		haveRead = 0;
		ch       = 0;

		nbrOfReads = size / M34_CH_WIDTH;


		fktRetCode = MBUF_GetBufferMode( m34Hdl->inbuf , &bufMode );
		if( fktRetCode != 0 && fktRetCode != ERR_MBUF_NO_BUF )
		{
		   return( fktRetCode );
		}/*if*/

		switch( bufMode )
		{

			case M_BUF_USRCTRL:
			   if( m34Hdl->irqIsEnabled )
				  return( ERR_LL_READ );        /* can't read ! */

			   while( nbrOfReads > 0 )
			   {
				   /* ch scheduling */
				   if( m34Hdl->chBlkRd[ch] )
				   {
					   /*--------------------+
					   |  set current ch     |
					   +--------------------*/
					   MWRITE_D16( m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch] );

					   /*----------------------------------+
					   |  star conversion & rd with delay  |
					   +----------------------------------*/
					   for (t=0; t<=m34Hdl->nbrDummyRd; t++)						/* dummy reads min 1 */
						   dummy   = MREAD_D16( m34Hdl->ma34, M34_DATA_START_RD );	/* dummy conversion */
					   dummy2  = MREAD_D16( m34Hdl->ma34, M34_DATA_START_RD );		/* conversion */
					   *bufP++ = dummy2;

					   nbrOfReads--;
					   haveRead = 1;
				   }/*if*/
				   ch++;

				   /*-----------------+
				   |  ch wrap around  |
				   +-----------------*/
				   if( (u_int32)ch == m34Hdl->nbrOfChannels )
				   {
					   ch = 0;
					   if( !haveRead )
					   {
						   DBGWRT_ERR((DBH,
							   "*** LL - M34_BlockRead: no ch configured M34_BLK_RD_IRQ\n"));
						   break;
					   }/*if*/
				   }/*if*/
			   }/*while*/

			   /* This cast to int32 is OK, because (bufP - buf) constitutes the buffer size  *
				* requested by the (32 bit wide) size parameter.                              */
			   *nbrRdBytesP = (int32) ((char*)bufP - (char*)buf);
			   fktRetCode   = 0; /* ovrwr ERR_MBUF_NO_BUFFER */
			   break;

			case M_BUF_RINGBUF:
			case M_BUF_RINGBUF_OVERWR:
			case M_BUF_CURRBUF:
			default:

				/* buffer irq mode with irq/ch? */
				if ((m34Hdl->irqMode == M34_IMODE_CHIRQ) ||
					(m34Hdl->irqMode == M34_IMODE_CHIRQ_AUTO)) {

					/* the requestet byte size must be a multiple
					   of the number of block read configured channels */
					if (size % (2 * m34Hdl->nbrCfgCh)) {
						DBGWRT_ERR((DBH,
							"*** LL - M34_BlockRead: illegal byte size (M34_IMODE_CHIRQ[_AUTO])\n"));
						return ERR_LL_ILL_PARAM;
					}

					m34Hdl->blkReadGotWords = 0;
					m34Hdl->blkReadReqWords = size/2;

					/* buffer irq mode with auto irq enable/disable? */
					if (m34Hdl->irqMode == M34_IMODE_CHIRQ_AUTO) {

						m34Hdl->isrCurrCh = 0;
						m34Hdl->nbrReadCh = 0;

						/* at least one channel must be configured for block read */
						if (m34Hdl->nbrCfgCh == 0) {
							DBGWRT_ERR((DBH,
								"*** LL - M34_BlockRead: no ch configured\n"));
							return ERR_LL_ILL_PARAM;
						}

						/* M_BUF_CURRBUF makes no sense */
						if (bufMode == M_BUF_CURRBUF) {
							DBGWRT_ERR((DBH,
								"*** LL - M34_BlockRead: M_BUF_CURRBUF not supported (IMODE_CHIRQ_AUTO)\n"));
							return ERR_LL_ILL_PARAM;
						}

						/* enable irq */
						DBGWRT_2((DBH, " enable irq\n"));
						setIrqEnable(1, m34Hdl);
						MWRITE_D16(m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[m34Hdl->isrCurrCh]);
					}
				}

			   fktRetCode = MBUF_Read( m34Hdl->inbuf, (u_int8*) buf, size,
									   nbrRdBytesP );

		}/*switch*/
	}/*  I R Q   M O D E   W I T H   B U F F E R */

    return( fktRetCode );
}/*M34_BlockRead*/

/****************************** M34_BlockWrite *******************************
 *
 *  Description:  Write data block to device
 *
 *                The function is not supported and returns always an
 *                ERR_LL_ILL_FUNC error.
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl        pointer to low-level driver data structure
 *                ch           current channel  (always ignored)
 *                buf          buffer to store read values
 *                size         should be multiple of width
 *
 *  Output.....:  nbrWrBytesP  number of written bytes
 *                return       ERR_LL_ILL_FUNC
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_BlockWrite
(
    LL_HANDLE *llHdl,
    int32     ch,
    void      *buf,
    int32     size,
    int32     *nbrWrBytesP
)
{
    DBGCMD(M34_HANDLE  *m34Hdl = (M34_HANDLE*) llHdl);

    DBGWRT_1((DBH, "LL - M34_BlockWrite: size=%ld\n",size));

    *nbrWrBytesP = 0;


    return( ERR_LL_ILL_FUNC );
}

/****************************** M34_Irq *************************************
 *
 *  Description:  Reads all configured ( M34_CH_RDBLK_IRQ ) to buffer or
 *                current channel (dummy) to reset irq cause.
 *                Stores the values to buffer in M_BUF_CURRBUF, M_BUF_RINGBUF
 *                and M_BUF_RINGBUF_OVERWR buffer mode.
 *
 *---------------------------------------------------------------------------
 *  Input......:  llHdl  pointer to low-level driver data structure
 *
 *  Output.....:  return LL_IRQ_UNKNOWN
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Irq
(
    LL_HANDLE *llHdl
)
{
	M34_HANDLE	*m34Hdl = (M34_HANDLE*) llHdl;
	u_int16		dummy;
	u_int32		ch;
	u_int32		t;
	u_int16		*buf;
	u_int32		nbrRdCh = 0;	/* number of read channels */
	int32		nbrOfBlocks;
	int32		gotsize;

#ifdef WINNT
	LARGE_INTEGER	t1, t2;
	t1 = KeQueryPerformanceCounter(NULL);
#endif

	/*---------------------------------------------------------------------------------+
	| F I X   I R Q   M O D E                                                          |
	+---------------------------------------------------------------------------------*/
	if (m34Hdl->irqMode == M34_IMODE_FIX) {

		IDBGWRT_1((DBH, "LL - M34_Irq: M34_IMODE_FIX\n"));

		if( m34Hdl->skip ){
			/* read data and start next conversion */
			dummy = MREAD_D16(m34Hdl->ma34, M34_DATA_RD_START_INC);
			m34Hdl->skip--;
		}
		else {
			/* read data and start next conversion */
			m34Hdl->buf[m34Hdl->isrCurrCh] = MREAD_D16(m34Hdl->ma34, M34_DATA_RD_START_INC);
			IDBGWRT_2((DBH, " buf[%d] = 0x%x\n",
				m34Hdl->isrCurrCh, m34Hdl->buf[m34Hdl->isrCurrCh]));

			/* not all data read? */
			if (m34Hdl->isrCurrCh < (m34Hdl->nbrOfChannels - 1)) {
				m34Hdl->isrCurrCh++;
			}
			/* all data read */
			else {
				IDBGWRT_2((DBH, " all data read\n"));
				OSS_SemSignal(m34Hdl->osHdl, m34Hdl->sem);
				goto DISABLE_CLEANUP;
			}
		}

		goto CLEANUP;
	}

	/*---------------------------------------------------------------------------------+
	|  I R Q   M O D E   W I T H   B U F F E R                                         |
	+---------------------------------------------------------------------------------*/

	/* buffer irq mode without auto irq enable/disable? */
	if ((m34Hdl->irqMode == M34_IMODE_LEGACY) ||
		(m34Hdl->irqMode == M34_IMODE_CHIRQ)) {
		/* at least one channel must be configured for block read */
		if (m34Hdl->nbrCfgCh == 0) {
			IDBGWRT_ERR((DBH,
				"*** LL - M34_Irq: no ch configured: disable irq\n"));
			setIrqEnable(0, m34Hdl);
			MWRITE_D16(m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[m34Hdl->isrCurrCh]);
		}
	}
	
	/* -------------------- buffer irq mode with irq/ch? -------------------- */
	if ((m34Hdl->irqMode == M34_IMODE_CHIRQ) ||
		(m34Hdl->irqMode == M34_IMODE_CHIRQ_AUTO)) {

		IDBGWRT_1((DBH, "LL - M34_Irq: M34_IMODE_CHIRQ[_AUTO]\n"));

		/* get space for one channel */
		if ((buf = (u_int16*)MBUF_GetNextBuf(m34Hdl->inbuf, 1, &gotsize)) != 0)
		{
			IDBGWRT_2((DBH, " buffer space available\n"));

			/* skip unused channels */
			while ((m34Hdl->isrCurrCh < m34Hdl->nbrOfChannels) &&
				(m34Hdl->chBlkRd[m34Hdl->isrCurrCh] == 0)) {
				m34Hdl->isrCurrCh++;

				/* wrap around ch number */
				if (m34Hdl->isrCurrCh == m34Hdl->nbrOfChannels)
					m34Hdl->isrCurrCh = 0;
			}

			IDBGWRT_3((DBH, " read ch=%d\n", m34Hdl->isrCurrCh));

			/* set current ch */
			MWRITE_D16(m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[m34Hdl->isrCurrCh]);

			/* dummy reads */
			for (t = 0; t <= m34Hdl->nbrDummyRd; t++)
				dummy = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD);

			/* conversion */
			*buf++ = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD);
			m34Hdl->nbrReadCh++;
			m34Hdl->isrCurrCh++;

			/* all configured ch read? */
			if (m34Hdl->nbrReadCh == m34Hdl->nbrCfgCh) {
				IDBGWRT_3((DBH, " all configured ch read\n"));
				/*
				* let MBUF_Read():
				* 1. copy data
				* 2. wait for more data if requested
				*/
				MBUF_ReadyBuf(m34Hdl->inbuf);
				m34Hdl->nbrReadCh = 0;
				m34Hdl->isrCurrCh = 0;
				m34Hdl->blkReadGotWords += m34Hdl->nbrCfgCh;

				/* buffer irq mode with auto irq enable/disable? */
				if (m34Hdl->irqMode == M34_IMODE_CHIRQ_AUTO) {
					/* all requested data read? */
					if (m34Hdl->blkReadGotWords == m34Hdl->blkReadReqWords)
						goto DISABLE_CLEANUP;
				}
			}
		}
		/* no more buffer space (all requested date read) */
		else
		{
			IDBGWRT_2((DBH, " no buffer space\n"));
			/* reset irq cause */
			dummy = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD); /* dummy conversion */
		}
		goto CLEANUP;
	}

	/* -------------------- legacy irq mode -------------------- */
	IDBGWRT_1((DBH, "LL - M34_Irq: M34_IMODE_LEGACY\n"));

	/*-------------------------------------+
	|  input values (configured channels)  |
	+-------------------------------------*/
	/* if m34Hdl->nbrCfgCh == 0, also buf is NULL and a dummy read is performed */
	if ((buf = (u_int16*)MBUF_GetNextBuf(
		m34Hdl->inbuf, m34Hdl->nbrCfgCh, &gotsize)) != 0)
	{
		IDBGWRT_2((DBH, " buffer space available\n"));

		for (ch = 0; ch<m34Hdl->nbrOfChannels; ch++)
		{
			/* ch scheduling */
			if (m34Hdl->chBlkRd[ch])
			{
				/*--------------------+
				|  set current ch     |
				+--------------------*/
				MWRITE_D16(m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[ch]);

				/*----------------------------------+
				|  start conversion & rd with delay |
				+----------------------------------*/
				for (t = 0; t <= m34Hdl->nbrDummyRd; t++)						/* dummy reads min 1 */
					dummy = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD); /* dummy conversion */

				*buf++ = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD);		/* conversion */
				nbrRdCh++;

				if ((nbrRdCh < m34Hdl->nbrCfgCh)		/* read another channel ? */
					&& ((int32)nbrRdCh == gotsize))	/* got space full ? */
				{
					/* calculate missing buffer space */
					nbrOfBlocks = m34Hdl->nbrCfgCh - nbrRdCh;
					/* not enough bytes gotten - wrap around buffer */
					if ((buf = (u_int16*)MBUF_GetNextBuf
						(m34Hdl->inbuf,
							nbrOfBlocks,
							&gotsize)) == 0)
					{
						/* wrap around failed */
						IDBGWRT_ERR((DBH, "*** LL - M34_Irq: wrap around failed\n"));
						break;
					}/*if*/
				}/*if*/
			}/*if*/
		}/*for*/

		MBUF_ReadyBuf(m34Hdl->inbuf);  /* blockread ready */
	}
	else
	{
		IDBGWRT_2((DBH, " no buffer space\n"));
		/* reset irq cause */
		dummy = MREAD_D16(m34Hdl->ma34, M34_DATA_START_RD); /* dummy conversion */
	}/*if*/

	goto CLEANUP;

/* -------------------- disable irq and cleanup -------------------- */
DISABLE_CLEANUP:
	/* disable irq */
	IDBGWRT_2((DBH, " disable irq\n"));
	setIrqEnable(0, m34Hdl);
	MWRITE_D16(m34Hdl->ma34, M34_CTRL_WR, m34Hdl->chCtrl[m34Hdl->isrCurrCh]);

/* -------------------- cleanup -------------------- */
CLEANUP:
    m34Hdl->irqCount++;

#ifdef WINNT
	t2 = KeQueryPerformanceCounter(NULL);
	m34Hdl->isrTicks.QuadPart += t2.QuadPart - t1.QuadPart;
#endif

    return( LL_IRQ_UNKNOWN );
}/*M34_Irq*/

/****************************** M34_Info ************************************
 *
 *  Description:  Gets low level driver info.
 *
 *                NOTE: can be called before MXX_Init().
 *
 *  supported info type codes       value          meaning
 *  ------------------------------------------------------------------------
 *  LL_INFO_HW_CHARACTER
 *     arg2  u_int32 *addrModeCharP    MDIS_MA08      M-Module characteristic
 *     arg3  u_int32 *dataModeCharP    MDIS_MD16      M-Module characteristic
 *
 *  LL_INFO_ADDRSPACE_COUNT
 *     arg2  u_int32 *nbrOfAddrSpaceP  1              number of used address
 *                                                    spaces
 *
 *  LL_INFO_ADDRSPACE
 *     arg2  u_int32  addrSpaceIndex   0              current address space
 *     arg3  u_int32 *addrModeP        MDIS_MA08      not used
 *     arg4  u_int32 *dataModeP        MDIS_MD16      driver use D16 access
 *     arg5  u_int32 *addrSizeP        0x100          needed size
 *
 *  LL_INFO_IRQ
 *     arg2  u_int32 *useIrqP          1              module use interrupts
 *
 *  LL_INFO_LOCKMODE
 *     arg2  u_int32 *lockModeP        LL_LOCK_CALL   process lock mode
 *
 *--------------------------------------------------------------------------
 *  Input......:  infoType          desired information
 *                ...               variable argument list
 *
 *  Output.....:  return            0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 M34_Info
(
   int32  infoType,
   ...
)
{
    int32   error;
    va_list argptr;
    u_int32 *nbrOfAddrSpaceP;
    u_int32 *addrModeP;
    u_int32 *dataModeP;
    u_int32 *addrSizeP;
    u_int32 *useIrqP;
    u_int32 addrSpaceIndex;

    error = 0;
    va_start( argptr, infoType );

    switch( infoType )
    {
        case LL_INFO_HW_CHARACTER:
          addrModeP  = va_arg( argptr, u_int32* );
          dataModeP  = va_arg( argptr, u_int32* );
          *addrModeP = MDIS_MA08;
          *dataModeP = MDIS_MD16;
          break;

        case LL_INFO_ADDRSPACE_COUNT:
          nbrOfAddrSpaceP  = va_arg( argptr, u_int32* );
          *nbrOfAddrSpaceP = 1;
          break;

        case LL_INFO_ADDRSPACE:
          addrSpaceIndex = va_arg( argptr, u_int32 );
          addrModeP  = va_arg( argptr, u_int32* );
          dataModeP  = va_arg( argptr, u_int32* );
          addrSizeP  = va_arg( argptr, u_int32* );

          switch( addrSpaceIndex )
          {
              case 0:
                *addrModeP = MDIS_MA08;
                *dataModeP = MDIS_MD16;
                *addrSizeP = 0x100;
                break;

              default:
                 error = ERR_LL_ILL_PARAM;
          }/*switch*/
          break;

        case LL_INFO_IRQ:
          useIrqP  = va_arg( argptr, u_int32* );
          *useIrqP = 1;
          break;

		case LL_INFO_LOCKMODE:
		{
			u_int32 *lockModeP = va_arg(argptr, u_int32*);
			*lockModeP = LL_LOCK_CALL;
			break;
		}

        default:
          error = ERR_LL_ILL_PARAM;
    }/*switch*/

    va_end( argptr );
    return( error );
}/*M34_Info*/


/************************** getStatBlock *************************************
 *
 *  Description:  Decodes the M_SETGETSTAT_BLOCK code and executes them.
 *
 *    supported codes      values        meaning
 *
 *    M_LL_BLK_ID_DATA                   read the M-Module ID from SROM
 *      blockStruct->size  0..0xff       number of bytes to read
 *      blockStruct->data  pointer       user buffer where ID data stored
 *
 *---------------------------------------------------------------------------
 *  Input......:  m34Hdl         m34 handle
 *                code           getstat code
 *                blockStruct    the struct with code size and data buffer
 *
 *  Output.....:  blockStruct->data  filled data buffer
 *                return - 0 | error code
 *
 *  Globals....:  ---
 *
 ****************************************************************************/
static int32 getStatBlock /* nodoc */
(
    M34_HANDLE         *m34Hdl,
    int32              code,
    M_SETGETSTAT_BLOCK *blockStruct
)
{
   int32   error;
   u_int8  i;
   u_int32 maxWords;
   u_int16 *dataP;

   error = 0;
   switch( code )
   {
       case M_LL_BLK_ID_DATA:
          maxWords = blockStruct->size / 2;
          dataP = (u_int16*)(blockStruct->data);
          for( i=0; i<maxWords; i++ )
          {
              *dataP = (u_int16) m_read((U_INT32_OR_64)m34Hdl->ma34, i);
              dataP++;
          }/*for*/
          break;

       default:
          error = ERR_LL_UNK_CODE;
   }/*switch*/

   return( error );
}/*getStatBlock*/

static void setGain( u_int16 gain, u_int16 *chCtrlP )
{
    *chCtrlP &= ~( M34_GAIN_8 << CTRL_GAIN);
    *chCtrlP |= (gain << CTRL_GAIN);
}/*setGain*/

static int32 getGain( u_int16 chCtrl )
{
    return( (int32)((chCtrl >> CTRL_GAIN)&M34_GAIN_8) );
}/*getGain*/

static void setBipolar( u_int16 bipolar, u_int16 *chCtrlP )
{
    *chCtrlP &= ~( M34_BIPOLAR << CTRL_BIPOLAR );
    *chCtrlP |= ( bipolar << CTRL_BIPOLAR );
}/*setBipolar*/

static int32 getBipolar( u_int16 chCtrl )
{
    return( (int32)((chCtrl >> CTRL_BIPOLAR)&M34_BIPOLAR) );
}/*getBipolar*/

static void setIrqEnable( u_int16 irqEnable, M34_HANDLE *m34Hdl )
{
u_int32 ch;

	m34Hdl->irqIsEnabled = irqEnable;

    /* set all ch */
    for( ch = 0; ch < m34Hdl->nbrOfChannels; ch++ )
    {
         m34Hdl->chCtrl[ch] &= ~( 1 << CTRL_IRQ );
         m34Hdl->chCtrl[ch] |= ( irqEnable << CTRL_IRQ );
    }/*for*/
}/*setIrqEnable*/






