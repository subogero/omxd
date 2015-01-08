#include <unistd.h>
#include <stdarg.h>
#include <string.h>

int I_root;
int logfd;
int loglevel = 0;

/* Write number in decimal format to file descriptor, printf() is BLOATED!!! */
int writedec(int fd, int num)
{
	int bytes = 0;
	/* Special case: negative numbers (print neg.sign) */
	if (num < 0) {
		write(fd, "-", 1);
		num *= -1;
		bytes++;
	}
	/*
	 * If num >= 10, print More Significant DigitS first by recursive call
	 * then we print Least Significatn Digit ourselves.
	 */
	int msds = num / 10;
	int lsd = num % 10;
	if (msds)
		bytes += writedec(fd, msds);
	char digit = '0' + lsd;
	write(fd, &digit, 1);
	return ++bytes;
}

/* Write a C-string to a file descriptor */
int writestr(int fd, char *str)
{
	int len = strlen(str);
	return write(fd, str, len);
}

/* Formatted printing into a file descriptor */
int printfd(int fd, char *fmt, ...)
{
	int bytes = 0;
	va_list va;
	va_start(va, fmt);
	while (*fmt) {
		char *perc = strchr(fmt, '%');
		int len = perc == NULL ? strlen(fmt) : perc - fmt;
		if (len) {
			bytes += write(fd, fmt, len);
			fmt += len;
		} else {
			fmt = perc + 1;
			if (*fmt == 0)
				continue;
			else if (*fmt == '%')
				bytes += write(fd, fmt, 1);
			else if (*fmt == 'd')
				bytes += writedec(fd, va_arg(va, int));
			else if (*fmt == 's')
				bytes += writestr(fd, va_arg(va, char*));
			fmt++;
		}
	}
	va_end(va);
	return bytes;
}

/* Read a decimal number from a string */
int sscand(char *str, int *num)
{
	int digits = 0;
	int number = 0;
	int sign = 1;
	if (*str == '-') {
		str++;
		sign = -1;
		digits++;
	}
	while (*str) {
		int digit = *str++;
		if (digit < '0' || digit > '9')
			break;
		digits++;
		digit -= '0';
		number *= 10;
		number += digit;
	}
	number *= sign;
	*num = number;
	return digits;
}
