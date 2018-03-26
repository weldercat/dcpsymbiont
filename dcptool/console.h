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

#ifndef CONSOLE_HDR_LOADED_
#define CONSOLE_HDR_LOADED_
#include <stdio.h>
#include <ncurses.h>

#define CONSOLE_OK	0
#define CONSOLE_FAIL	-1

#define STATUS_LINE_SIZE 	79

/* init everything */
int console_init(int (*cmd_handler)(char *line));
/* print to message window */
int console_mprintf(const char *format, ...);
int console_vmprintf(const char *format, va_list ap);
int console_mputc(int c);
int console_mrefresh(void);
/* print to status line */
int console_sprintf(const char *format, ...);
int console_srefresh(void);
/* wind down everything */
void console_rundown(void);
void console_set_status(const char *line);
/* process a character */
int console_input(int c);
/*input a character from the cmd window */
int console_getch(void);

#endif /* CONSOLE_HDR_LOADED_ */
