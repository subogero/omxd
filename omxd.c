/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include "omxd.h"

int logfd;
static int ctrlpipe[2];
static pid_t player_pid = 0;

static int daemonize(void);
static int read_fifo(char *line);
static int parse(char *line);
static void player(char *cmd, char *file);
static void stop_playback(void);
static void player_quit(int signum); /* SIGCHLD signal handler */
static void drop_priv(void);
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
	while (1) {
		char line[LINE_LENGTH];
		read_fifo(line);
		LOG(0, "main: %s\n", line);
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
	LOG(0, "daemonize: omxd started, SID %d\n", sid);
	/* Create and open FIFO for command input as stdin */
	unlink("omxctl");
	LOG(0, "daemonize: Deleted original omxctl FIFO\n");
	if (mknod("omxctl", S_IFIFO | 0622, 0) < 0)
		return 6;
	LOG(0, "daemonize: Created new omxctl FIFO\n");
	return 0;
}

/* Read a line from FIFO /var/run/omxctl */
static int read_fifo(char *line)
{
	static int cmdfd = -1;
	if (cmdfd < 0) {
		cmdfd = open("omxctl", O_RDONLY);
		if (cmdfd < 0) {
			LOG(0, "read_fifo: Can't open omxctl\n");
			return -1;
		} else {
			LOG(0, "read_fifo: Client opened omxctl\n");
		}
	}
	int i = 0;
	while (1) {
		if (!read(cmdfd, line + i, 1)) {
			LOG(0, "read_fifo: Client closed omxctl\n");
			close(cmdfd);
			cmdfd = -1;
			line[i] = 0;
			return i;
		} else if (line[i] == '\n') {
			LOG(0, "read_fifo: omxctl end of line\n");
			line[i] = 0;
			return i;
		} else if (i == LINE_LENGTH - 2) {
			LOG(0, "read_fifo: omxctl too long line\n");
			i++;
			line[i] = 0;
			return i;
		}
		i++;
	}
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
	if (cmd != NULL && *cmd != 0) {
		player(cmd, playlist(cmd, file));
	}
	return cmd != NULL ? *cmd : 0;
}

/* Control the actual omxplayer */
static void player(char *cmd, char *file)
{
	if (file != NULL && *file != 0) {
		stop_playback();
		pipe(ctrlpipe);
		char *argv[4];
		argv[0] = "/usr/bin/omxplayer";
		argv[1] = get_output(cmd);
		argv[2] = file;
		argv[3] = NULL;
		player_pid = fork();
		if (player_pid < 0) { /* Fork error */
			player_pid = 0;
			close(ctrlpipe[0]);
			close(ctrlpipe[1]);
		} else if (player_pid > 0) { /* Parent: set SIGCHLD handler */
			close(ctrlpipe[0]);
			signal(SIGCHLD, player_quit);
			LOG(0, "player: PID=%d %s\n", player_pid, file);
			/* 2nd omxplayer for info, redirect stdout to logfile */
			pid_t info_pid = fork();
			if (info_pid != 0)
				return;
			argv[1] = "-i";
			close(1);
			int omx_stdout = dup(logfd);
			execve(argv[0], argv, NULL);
			_exit(20);
		} else { /* Child: exec omxplayer */
			drop_priv();
			close(ctrlpipe[1]);
			/* Redirect read end of control pipe to 0 stdin */
			close(logfd);
			close(0);
			dup(ctrlpipe[0]);
			close(ctrlpipe[0]);
			execve(argv[0], argv, NULL);
			_exit(20);
		}
	} else if (strchr(OMX_CMDS, *cmd) != NULL && player_pid != 0) {
		LOG(0, "player: Send %s to omxplayer\n", cmd);
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
	} else if (strchr(STOP_CMDS, *cmd) != NULL && player_pid != 0) {
		stop_playback();
	}
}

/* Stop the playback immediately */
static void stop_playback(void)
{
	if (player_pid != 0) {
		signal(SIGCHLD, SIG_DFL);
		write(ctrlpipe[1], "q", 1);
		player_quit(0);
	}
}

/* Signal handler for SIGCHLD when player exits */
static void player_quit(int signum)
{
	int status;
	pid_t pid = wait(&status);
	if (pid != player_pid) /* Do nothing if info-omxplayer exited */
		return;
	status = WEXITSTATUS(status);
	close(ctrlpipe[1]);
	LOG(0, "player_quit: PID=%d (%d) with %d\n", pid, player_pid, status);
	player_pid = 0;
	if (signum == SIGCHLD)
		player("n", playlist("n", NULL));
}

/* Drop root privileges before execing omxplayer */
static void drop_priv(void)
{
	int cfg = open("/etc/omxd.conf", O_RDONLY);
	if (cfg < 0)
		return;
	char buffer[4096];
	if (read(cfg, buffer, 4096) == 0)
		return;
	char *line = strstr(buffer, "user=");
	if (line == NULL)
		return;
	strtok(line, "=");
	char *user = strtok(NULL, "\n");
	struct passwd *pwd = getpwnam(user);
	if (pwd == NULL)
		return;
	chdir(pwd->pw_dir);
	initgroups(pwd->pw_name, pwd->pw_gid);
	setgid(pwd->pw_gid);
	setuid(pwd->pw_uid);
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
