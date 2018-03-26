/*
 *	(C) ulfalizer. License unknown
 *
 *
 */
#define _GNU_SOURCE	1
#define _XOPEN_SOURCE 700	// For strnlen()
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include <assert.h>
#include <stdarg.h>
#include "console.h"


#define max(a, b)         \
  ({ typeof(a) _a = a;    \
     typeof(b) _b = b;    \
     _a > _b ? _a : _b; })

#define CMDWIN_LINES	4
#define MIN_MSGWIN_LINES	10

// Flag to see if we need to reset the terminal on errors
static bool visual_mode = false;


static void readline_redisplay(void);
static void cmd_win_redisplay(bool for_resize);
static char status_line[STATUS_LINE_SIZE+1];

static void fail_exit(const char *msg)
{
	// This is safe here, but it's generally a good idea to check !isendwin()
	// too before calling endwin(), as calling it twice can mess with the
	// cursor position
	if (visual_mode)
		endwin();
	fprintf(stderr, "%s\n", msg);
	exit(EXIT_FAILURE);
}

// Checks errors for (most) ncurses functions. CHECK(fn, x, y, z) is a checked
// version of fn(x, y, z).
#define CHECK(fn, ...)               \
  do                                 \
      if (fn(__VA_ARGS__) == ERR)    \
          fail_exit(#fn"() failed"); \
  while (false)



// Message window
static WINDOW *msg_win;
// Separator line above the command (readline) window
static WINDOW *sep_win;
// Command (readline) window
static WINDOW *cmd_win;

// String displayed in the message window
//static char *msg_win_str = NULL;

// Input character for readline
static unsigned char input;

static int (*cmdh)(char *line) = NULL;

// Used to signal "no more input" after feeding a character to readline
static bool input_avail = false;

void console_set_status(const char *line)
{
	assert(line);
	strncpy(status_line, line, STATUS_LINE_SIZE);
}

// Calculates the cursor position for the readline window in a way that
// supports multibyte, multi-column and combining characters. readline itself
// calculates this as part of its default redisplay function and does not
// export the cursor position.
//
// Returns the total width (in columns) of the characters in the 'n'-byte
// prefix of the null-terminated multibyte string 's'. If 'n' is larger than
// 's', returns the total width of the string. Tries to emulate how readline
// prints some special characters.
//
// 'offset' is the current horizontal offset within the line. This is used to
// get tabstops right.
//
// Makes a guess for malformed strings.
static size_t strnwidth(const char *s, size_t n, size_t offset)
{
	mbstate_t shift_state;
	wchar_t wc;
	size_t wc_len;
	size_t width = 0;
	size_t	i;

	// Start in the initial shift state
	memset(&shift_state, '\0', sizeof shift_state);

	for (i = 0; i < n; i += wc_len) {
		// Extract the next multibyte character
		wc_len = mbrtowc(&wc, s + i, MB_CUR_MAX, &shift_state);
		switch (wc_len) {
		case 0:
			// Reached the end of the string
			goto done;

		case (size_t) - 1:
		case (size_t) - 2:
			// Failed to extract character. Guess that the remaining characters
			// are one byte/column wide each.
			width += strnlen(s, n - i);
			goto done;
		}

		if (wc == '\t') width = ((width + offset + 8) & ~7) - offset;
		else
			// TODO: readline also outputs ~<letter> and the like for some
			// non-printable characters
			width += iswcntrl(wc) ? 2 : max(0, wcwidth(wc));
	}
 done:
	return width;
}

// Like strnwidth, but calculates the width of the entire string
static size_t strwidth(const char *s, size_t offset)
{
	return strnwidth(s, SIZE_MAX, offset);
}

// Not bothering with 'input_avail' and just returning 0 here seems to do the
// right thing too, but this might be safer across readline versions
static int readline_input_avail(void)
{
	return input_avail;
}

static int readline_getc(FILE * dummy)
{
	input_avail = false;
	return input;
}


static void forward_to_readline(char c)
{
	input = c;
	input_avail = true;
	rl_callback_read_char();
	
}

int console_input(int c)
{
	forward_to_readline(c & 0xff);
	return CONSOLE_OK;
}

int console_mprintf(const char *format, ...)
{
	int	res;
	va_list	ap;

	assert(format);
	va_start(ap, format);
	res = vwprintw(msg_win, format, ap);
	va_end(ap); 
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}

int console_vmprintf(const char *format, va_list ap)
{
	int	res;
	assert(format);
	res = vwprintw(msg_win, format, ap);
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}

int console_sprintf(const char *format, ...)
{
	int	res;
	va_list	ap;

	assert(format);
	va_start(ap, format);
	res = mvwprintw(sep_win, 0, 0, format, ap);
	va_end(ap); 
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}

int console_mputc(int c)
{
	int res;
	
	res = waddch(msg_win, c);
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}

int console_getch(void)
{
	return wgetch(cmd_win);
}

int console_mrefresh(void)
{
	int	res;
	
	res = wrefresh(msg_win);
	cmd_win_redisplay(false);
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}

int console_srefresh(void)
{
	int	res;
	
	res = wrefresh(sep_win);
	cmd_win_redisplay(false);
	if (res == ERR) {
		return CONSOLE_FAIL;
	} else {
		return CONSOLE_OK;
	}
}


static void got_command(char *line)
{
	if (line != NULL) {
		if (*line != '\0') add_history(line);
	}
	(*cmdh)(line);
	if (line) free(line);
}

static void cmd_win_redisplay(bool for_resize)
{
	size_t prompt_width = strwidth(rl_display_prompt, 0);
	size_t rlpos = prompt_width +
	    strnwidth(rl_line_buffer, rl_point, prompt_width);
	int	cursor_line = 0;
	int	cursor_col = 0;
	int maxchars = COLS * CMDWIN_LINES;

	CHECK(werase, cmd_win);
	// This might write a string wider than the terminal currently, so don't
	// check for errors
//	mvwprintw(cmd_win, 0, 0, "%s%s", rl_display_prompt, rl_line_buffer);
	
	if (rlpos >= maxchars) {
		mvwaddstr(cmd_win, 0, 0, rl_line_buffer + (rl_point - ((maxchars / 2) - (COLS / 2))));
		rlpos = (maxchars / 2) - (COLS / 2);
	} else {
		mvwaddstr(cmd_win, 0, 0, rl_display_prompt);
		waddstr(cmd_win, rl_line_buffer);
	}
	if (rlpos >= COLS) {
		// Hide the cursor if it lies outside the window. Otherwise it'll
		// appear on the very right.
		cursor_line = rlpos / COLS;
		cursor_col = rlpos % COLS;
	} else {
		cursor_line = 0;
		cursor_col = rlpos;
	}
	if (cursor_line >= CMDWIN_LINES) cursor_line = CMDWIN_LINES - 1;

	mvwprintw(sep_win, 0, 0, "%s", status_line);
//		maxchars, rlpos, rl_point, cursor_line, cursor_col);
	wrefresh(sep_win);

	CHECK(wmove, cmd_win, cursor_line, cursor_col);
	curs_set(1);
	// We batch window updates when resizing
	if (for_resize)	CHECK(wnoutrefresh, cmd_win);
	else CHECK(wrefresh, cmd_win);
}

static void readline_redisplay(void)
{
	cmd_win_redisplay(false);
}

static void init_ncurses(void)
{
	if (initscr() == NULL)
		fail_exit("Failed to initialize ncurses");
	visual_mode = true;

	if (can_change_color()) {
		CHECK(start_color);
		CHECK(use_default_colors);
	}
	CHECK(cbreak);
	CHECK(noecho);
	CHECK(nonl);
	CHECK(intrflush, NULL, FALSE);
	// Do not enable keypad() since we want to pass unadultered input to
	// readline

	// Explicitly specify a "very visible" cursor to make sure it's at least
	// consistent when we turn the cursor on and off (maybe it would make sense
	// to query it and use the value we get back too). "normal" vs. "very
	// visible" makes no difference in gnome-terminal or xterm. Let this fail
	// for terminals that do not support cursor visibility adjustments.
	curs_set(1);

	if (LINES >= (MIN_MSGWIN_LINES + 1 + CMDWIN_LINES )) {
		msg_win = newwin(LINES - (CMDWIN_LINES + 1), COLS, 0, 0);
		sep_win = newwin(1, COLS, LINES - (CMDWIN_LINES + 1), 0);
		cmd_win = newwin(CMDWIN_LINES, COLS, LINES - CMDWIN_LINES, 0);
	} else {
		fail_exit("Terminal size is too small");
	}
	if (msg_win == NULL || sep_win == NULL || cmd_win == NULL)
		fail_exit("Failed to allocate windows");

	// Allow strings longer than the message window and show only the last part
	// if the string doesn't fit
	CHECK(scrollok, msg_win, TRUE);
	CHECK(scrollok, cmd_win, FALSE);
	CHECK(nodelay, msg_win, FALSE);
	CHECK(nodelay, cmd_win, FALSE);

	if (can_change_color()) {
		// Use white-on-blue cells for the separator window...
		CHECK(init_pair, 1, COLOR_WHITE, COLOR_BLUE);
		CHECK(wbkgd, sep_win, COLOR_PAIR(1));
	} else
		// ...or the "best highlighting mode of the terminal" if it doesn't
		// support colors
		CHECK(wbkgd, sep_win, A_STANDOUT);
	CHECK(wrefresh, sep_win);
}

static void deinit_ncurses(void)
{
	CHECK(delwin, msg_win);
	CHECK(delwin, sep_win);
	CHECK(delwin, cmd_win);
	CHECK(endwin);
	visual_mode = false;
}

static void init_readline(void)
{
	// Disable completion. TODO: Is there a more robust way to do this?
	if (rl_bind_key('\t', rl_insert) != 0)
		fail_exit("Invalid key passed to rl_bind_key()");

	// Let ncurses do all terminal and signal handling
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;

	// Prevent readline from setting the LINES and COLUMNS environment
	// variables, which override dynamic size adjustments in ncurses. When
	// using the alternate readline interface (as we do here), LINES and
	// COLUMNS are not updated if the terminal is resized between two calls to
	// rl_callback_read_char() (which is almost always the case).
	rl_change_environment = 0;

	// Handle input by manually feeding characters to readline
	rl_getc_function = readline_getc;
	rl_input_available_hook = readline_input_avail;
	rl_redisplay_function = readline_redisplay;

	rl_callback_handler_install("> ", got_command);
}

static void deinit_readline(void)
{
	rl_callback_handler_remove();
}

int console_init(int (*cmd_handler)(char *line))
{
	assert(cmd_handler);

	memset(status_line, 0, STATUS_LINE_SIZE+1);
	cmdh = cmd_handler;
	// Set locale attributes (including encoding) from the environment
	if (setlocale(LC_ALL, "") == NULL)
		fail_exit("Failed to set locale attributes from environment");

	init_ncurses();
	init_readline();
	return CONSOLE_OK;
}

void console_rundown(void)
{
	deinit_ncurses();
	deinit_readline();
}
