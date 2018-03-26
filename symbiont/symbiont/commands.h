/*
 * Copyright 2017  Stacy <stacy@sks.uz>
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

#ifndef COMMANDS_HDR_LOADED_
#define COMMANDS_HDR_LOADED_


#define NO_COMMAND	-1
#define CMD_OK		0


int run_command(char *line, char **outbuf, int *outlen);


#endif /* COMMANDS_HDR_LOADED_ */
