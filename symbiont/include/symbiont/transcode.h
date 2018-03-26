#ifndef TRANSCODE_HDR_LOADED_
#define TRANSCODE_HDR_LOADED_

/*
 * Copyright 2018 Stacy <stacy@sks.uz> 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <stdint.h>

#include <symbiont/mmi.h>
#include <symbiont/dcphdlc.h>

#define DBLK_BUFLEN	(HDLC_DATA_LEN + 2)
#define DBLK_MAXLEN	(HDLC_DATA_LEN)

/* a structure to hold dcp data blocks train */

struct dcp_dblk;

struct dcp_dblk {
	int	length;
	uint8_t	data[DBLK_BUFLEN];
	struct dcp_dblk *next;
};

struct dcp_dblk *mmi2dcp(struct mmi_command *cmd);

/* returned struct mmi_event is malloc'd */
struct mmi_event *dcp2mmi(uint8_t *data, int length);

/* mallocs & returns MMI_EVT_LOST */
struct mmi_event *mmi_lost(void);

/* mallocs & returns MMI_EVT_UP */
struct mmi_event *mmi_up(void);

void free_dblks(struct dcp_dblk *dtrain);



#endif /* TRANSCODE_HDR_LOADED_ */

