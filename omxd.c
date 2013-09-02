/* (c) SZABO Gergely <szg@subogero.com>, license GPLv3 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include "omxd.h"

int logfd;
static int ctrlpipe[2];
static pid_t player_pid = 0;

static int daemonize(void);
static int parse(char *line);
static int player(char *cmd, char *file);
static void player_quit(int signum); /* SIGCHLD signal handler */
static char *get_output(char *cmd);

int main(int argc, char *argv[])
{
	/* Client when called with options */
	if (argc > 1) {
		return client(argc, argv);
	}
	int daemon_error = daemonize();
	if (daemon_error > 0)
		return daemon_error;
	else if (daemon_error < 0)
		return 0;
	/* Main loop */
	int cmdfd = -1;
	while (1) {
		if (cmdfd < 0) {
			cmdfd = open("omxctl", O_RDONLY);
			if (cmdfd < 0) {
				writestr(logfd, "main: Can't open omxctl\n");
				return 7;
			} else {
				writestr(logfd, "main: Client opened omxctl\n");
				continue;
			}
		}
		char line[LINE_LENGTH];
		int len = read(cmdfd, line, LINE_MAX);
		if (len == 0) {
			writestr(logfd, "main: Client closed omxctl\n");
			close(cmdfd);
			cmdfd = -1;
			continue;
		}
		/* Make C-string from one input line, discard LF and rest*/
		line[LINE_MAX] = 0;
		char *lf = strchr(line, '\n');
		if (lf != NULL) {
			*lf = 0;
		}
		printfd(logfd, "main: %s\n", line);
		parse(line);
	}
	return 0;
}

/* Fork, umask, SID, chdir, close, logfile, FIFO */
static int daemonize(void)
{
	/* Fork the real daemon */
	pid_t pid = fork();
	if (pid < 0)
		return 1;
	if (pid > 0) {
		printfd(1, "omxd daemon started, PID %d\n", pid);
		return -1;
	}
	/* umask and session ID */
	umask(0);
	pid_t sid = setsid();
	if (sid < 0)
		return 2;
	/* Run in /var/run if invoked as root, or allow testing in CWD */
	int I_root = geteuid() == 0;
	if (I_root && chdir("/var/run/") < 0)
		return 3;
	/* Create log file as stdout and stderr */
	close(0);
	close(1);
	close(2);
	logfd = creat(I_root ? "/var/log/omxlog" : "omxlog", 0644);
	if (logfd < 0)
		return 4;
	if (printfd(logfd, "daemonize: omxd started, SID %d\n", sid) == 0)
		return 5;
	/* Create and open FIFO for command input as stdin */
	unlink("omxctl");
	writestr(logfd, "daemonize: Deleted original omxctl FIFO\n");
	if (mknod("omxctl", S_IFIFO | 0622, 0) < 0)
		return 6;
	writestr(logfd, "daemonize: Created new omxctl FIFO\n");
	return 0;
}

/* Get command char and file/URL name from a command line */
static int parse(char *line)
{
	/* Extract command and file/URL from line */
	char *cmd = NULL;
	char *file = line;
	while (1) {
		if (*file == 0) {
			if (cmd == NULL)
				cmd = line;
			break;
		} else if (*file == ' ' || *file == '\t') {
			if (cmd == NULL) {
				*file = 0;
				cmd = line;
			}
		} else if (cmd != NULL) {
			break;
		}
		file++;
	}
	if (cmd != NULL) {
		player(cmd, playlist(cmd, file));
	}
	return cmd != NULL ? *cmd : 0;
}

/* Control the actual omxplayer */
static int player(char *cmd, char *file)
{
	if (file != NULL && *file != 0) {
		if (player_pid != 0) {
			signal(SIGCHLD, SIG_DFL);
			write(ctrlpipe[1], "q", 1);
			player_quit(0);
		}
		pipe(ctrlpipe);
		player_pid = fork();
		if (player_pid < 0) { /* Fork error */
			player_pid = 0;
			close(ctrlpipe[0]);
			close(ctrlpipe[1]);
		} else if (player_pid > 0) { /* Parent: set SIGCHLD handler */
			close(ctrlpipe[0]);
			signal(SIGCHLD, player_quit);
			printfd(logfd, "player: PID=%d %s\n", player_pid, file);
		} else { /* Child: exec omxplayer */
			close(ctrlpipe[1]);
			/* Redirect read end of control pipe to 0 stdin */
			if (logfd == 0)
				logfd = dup(logfd);
			close(0);
			dup(ctrlpipe[0]);
			close(ctrlpipe[0]);
			char *argv[4];
			argv[0] = "/usr/bin/omxplayer";
			argv[1] = get_output(cmd);
			argv[2] = file;
			argv[3] = NULL;
			execve(argv[0], argv, NULL);
			writestr(logfd, "player child: Can't exec omxplayer\n");
			_exit(20);
		}
	} else if (strchr(OMX_CMDS, *cmd) != NULL && player_pid != 0) {
		printfd(logfd, "player: Send %s to omxplayer\n", cmd);
		cmd[1] = 0; /* Just one character normally */
		/* Replace FRfr with arrow-key escape sequences */
		if      (*cmd == 'F')
			strcpy(cmd, "\033[A");
		else if (*cmd == 'R')
			strcpy(cmd, "\033[B");
		else if (*cmd == 'f')
			strcpy(cmd, "\033[C");
		else if (*cmd == 'r')
			strcpy(cmd, "\033[D");
		writestr(ctrlpipe[1], cmd);
	}
}

/* Signal handler for SIGCHLD when player exits */
static void player_quit(int signum)
{
	int status;
	pid_t pid = wait(&status);
	status = WEXITSTATUS(status);
	close(ctrlpipe[1]);
	printfd(logfd, "player_quit: PID=%d (%d) with %d\n", pid, player_pid, status);
	player_pid = 0;
	if (signum == SIGCHLD)
		player("n", playlist("n", NULL));
}

/* Return omxplayer argument to set output interface (Jack/HDMI) */
static char *get_output(char *cmd)
{
	enum e_outputs           { OUT_JACK,  OUT_HDMI };
	static char *outputs[] = { "-olocal", "-ohdmi" };
	static enum e_outputs output = OUT_JACK;
	enum e_outputs output_now;
	if      (*cmd == 'h')
		output_now = output = OUT_HDMI;
	else if (*cmd == 'H')
		output_now = OUT_HDMI;
	else if (*cmd == 'j')
		output_now = output = OUT_JACK;
	else if (*cmd == 'J')
		output_now = OUT_JACK;
	else
		output_now = output;
	return outputs[output_now];
}

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
	int i_val = 0;
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
