#include <ncurses.h>
#include <stdlib.h>

void bomb(void);

int main()
{
	WINDOW *a, *b, *c, *d;
	int maxx, maxy, halfx, halfy;

	initscr();

/* calculate window sizes and locations */
	getmaxyx(stdscr, maxy, maxx);
	halfx = maxx / 2;
	halfy = maxy / 2;

	printw("maxx=%d, maxy=%d, halfx=%d, halfy=%d\n", maxx, maxy, halfx, halfy);

/* create four windows to fill the screen */
	if ((a = newwin(halfy, halfx, 0, 0)) == NULL)
		bomb();
	if ((b = newwin(halfy, halfx, 0, halfx)) == NULL)
		bomb();
	if ((c = newwin(halfy, halfx, halfy, 0)) == NULL)
		bomb();
	if ((d = newwin(halfy, halfx, halfy, halfx)) == NULL)
		bomb();

/* Write to each window */
	wprintw(a, "This is window A");
	wrefresh(a);
//	getch();
	wprintw(b, "This is window B ");
	wrefresh(b);
	getch();
	wprintw(c, "This is window C ");
	wrefresh(c);
//	getch();
	wprintw(d, "This is window D ");
	wrefresh(d);
	getch();
	refresh();
	getch();
	
	endwin();
	return 0;
}

void bomb(void)
{
	addstr("Unable to allocate memory for new window. \n") ;
	refresh();
	endwin();
	exit(1);
}
