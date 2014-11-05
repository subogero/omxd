#include <stdio.h>
#include "omxd.h"

static void print_now_next(char **now_next)
{
	printf("---\n");
	if (now_next == NULL)
		return;
	printf("- %s\n", now_next[0] == NULL ? "~" : now_next[0]);
	printf("- %s\n", now_next[1] == NULL ? "~" : now_next[1]);
}

int main(void)
{
	I_root = 0;
	logfd = 2;
	print_now_next(m_list("i", "/foo/1"));
	print_now_next(m_list("a", "/foo/2"));
	print_now_next(m_list("A", "/foo/3"));
	print_now_next(m_list("A", "/spam/1"));
	print_now_next(m_list("A", "/spam/2"));
	print_now_next(m_list("x", NULL));
	print_now_next(m_list("A", "/blabla/1"));
	print_now_next(m_list("A", "/blabla/2"));
	print_now_next(m_list("A", "/bar/1"));
	print_now_next(m_list("A", "/bar/2"));
	print_now_next(m_list("n", NULL));
	print_now_next(m_list("N", NULL));
	print_now_next(m_list("N", NULL));
	print_now_next(m_list("d", NULL));
	print_now_next(m_list("D", NULL));
	print_now_next(m_list("I", "/insert/this/now"));
	print_now_next(m_list("L", "/insert/this/after"));
	print_now_next(m_list("n", NULL));
	print_now_next(m_list("n", NULL));
	print_now_next(m_list("j", NULL));
}
