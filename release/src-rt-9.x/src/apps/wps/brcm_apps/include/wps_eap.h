/*
 * WPS eap
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
 * $Id: wps_eap.h 525052 2015-01-08 20:18:35Z $
 */

#ifndef __WPS_EAP_H__
#define __WPS_EAP_H__

int wps_eap_init();
void wps_eap_deinit();
int wps_eap_process_msg(char *buf, int buflen);

#endif	/* __WPS_EAP_H__ */
