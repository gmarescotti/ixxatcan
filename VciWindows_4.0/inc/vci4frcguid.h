/*****************************************************************************
 IXXAT Automation GmbH
******************************************************************************

 File    : VCIUMSAM.H
 Summary : Declaration for the VCI user mode example driver.

 Date    : 2015-09-07

 Compiler: MSVC

******************************************************************************
 all rights reserved
*****************************************************************************/

#ifndef _VCIUMSAM_H_
#define _VCIUMSAM_H_

#include <stdtype.h>

/*****************************************************************************
 * Device Class for VCI4FRC user mode driver 
 *****************************************************************************/

// {AA63AFCF-9D39-4f30-B4D5-AA1ACEE8AB6F}
DEFINE_GUID(GUID_VCI4FRC_DEVICE, 
            0xaa63afcf, 0x9d39, 0x4f30, 0xb4, 0xd5, 0xaa, 0x1a, 0xce, 0xe8, 0xab, 0x6f);


#endif //_VCIUMSAM_H_
