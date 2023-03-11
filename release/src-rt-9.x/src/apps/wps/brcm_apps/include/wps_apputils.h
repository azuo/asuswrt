/*
 * WPS app utilies
 *
 * Copyright (C) 2015, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: $
 */

#ifndef __WPS_APPUTILS_H__
#define __WPS_APPUTILS_H__

int wpsapp_utils_update_custom_cred(char *ssid, char *key, char *akm, char *crypto, int oob_addenr,
	bool b_wps_Version2);

#endif	/* __WPS_APPUTILS_H__ */
