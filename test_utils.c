#include <unistd.h>
#include "omxd.h"

int main(void)
{
	printfd(1, "%s %d\n", "zero:", 0);
	printfd(1, "%s %d\n", "one:", 1);
	printfd(1, "%s %d\n", "minus two:", -2);
	printfd(1, "%s %d\n", "twenty three:", 23);
	printfd(1, "%s %d\n", "minus five hundred:", -500);
}
