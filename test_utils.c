#include <unistd.h>
#include <string.h>
#include "omxd.h"

int main(void)
{
	writestr(1, "Testing writestr, writedec, printfd\n");
	printfd(1, "%s %d\n", "zero:", 0);
	printfd(1, "%s %d\n", "one:", 1);
	printfd(1, "%s %d\n", "minus two:", -2);
	printfd(1, "%s %d\n", "twenty three:", 23);
	printfd(1, "%s %d\n", "minus five hundred:", -500);
	char line[LINE_LENGTH];
	strcpy(line, "Testing scatd, PID = ");
	scatd(line, getpid());
	strcat(line, "\n");
	writestr(1, line);
	return 0;
}
