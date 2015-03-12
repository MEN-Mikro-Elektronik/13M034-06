#***************************  M a k e f i l e  *******************************
#
#         Author: uf
#          $Date: 2003/10/20 11:21:59 $
#      $Revision: 1.5 $
#
#    Description: makefile descriptor file for common
#                 modules MDIS 4.0   e.g. low level driver
#
#---------------------------------[ History ]---------------------------------
#
#   $Log: driver.mak,v $
#   Revision 1.5  2003/10/20 11:21:59  kp
#   cosmetics
#
#   Revision 1.4  1998/07/09 15:17:27  see
#   MAK_LIBS were in wrong order
#   Header keyword removed
#
#   Revision 1.3  1998/06/30 16:44:41  Schmidt
#   dbg.h and dbg library added, cosmetics
#
#   Revision 1.2  1998/02/23 11:53:40  Schmidt
#   id.lib instead modcom.lib
#
#   Revision 1.1  1998/02/19 16:37:48  franke
#   Added by mcvs
#
#-----------------------------------------------------------------------------
#   (c) Copyright 1997 by MEN mikro elektronik GmbH, Nuernberg, Germany
#*****************************************************************************

MAK_NAME=m34

MAK_SWITCH=$(SW_PREFIX)MAC_MEM_MAPPED

MAK_LIBS=$(LIB_PREFIX)$(MEN_LIB_DIR)/desc$(LIB_SUFFIX)  \
         $(LIB_PREFIX)$(MEN_LIB_DIR)/id$(LIB_SUFFIX)   	\
         $(LIB_PREFIX)$(MEN_LIB_DIR)/mbuf$(LIB_SUFFIX)  \
         $(LIB_PREFIX)$(MEN_LIB_DIR)/oss$(LIB_SUFFIX)   \
         $(LIB_PREFIX)$(MEN_LIB_DIR)/dbg$(LIB_SUFFIX)  \


MAK_INCL=$(MEN_INC_DIR)/m34_drv.h     \
         $(MEN_INC_DIR)/men_typs.h    \
         $(MEN_INC_DIR)/oss.h         \
         $(MEN_INC_DIR)/mbuf.h        \
         $(MEN_INC_DIR)/mdis_err.h    \
         $(MEN_INC_DIR)/maccess.h     \
         $(MEN_INC_DIR)/desc.h        \
         $(MEN_INC_DIR)/mdis_api.h    \
         $(MEN_INC_DIR)/mdis_com.h    \
         $(MEN_INC_DIR)/modcom.h      \
         $(MEN_INC_DIR)/ll_defs.h     \
         $(MEN_INC_DIR)/ll_entry.h    \
         $(MEN_INC_DIR)/dbg.h    \


MAK_INP1=m34_drv$(INP_SUFFIX)

MAK_INP=$(MAK_INP1)

