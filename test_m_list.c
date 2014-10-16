#include <stdio.h>
#include "omxd.h"
#include "m_list.h"

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
}
