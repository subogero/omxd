/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include "omxd.h"

static int client_cmd(char *cmd);
static char *is_url(char *file);
static mode_t get_ftype(char *file);
static int cmd_foreach_in(char *cmd);
static int writecmd(char *cmd);
char file_opening[32];
static int open_tout(char *file);
static void open_tout_handler(int signal);

int client(int argc, char *argv[])
{
	char *cmd = argv[1];
	char *file = argc >= 3 ? argv[2] : NULL;
	signal(SIGALRM, open_tout_handler);
	/* Check command */
	if (strchr(CLIENT_CMDS, *cmd) != NULL)
		return client_cmd(cmd);
	if (strchr(OMX_CMDS LIST_CMDS, *cmd) == NULL)
		return 11;
	if (file == NULL)
		return writecmd(cmd);
	char line[LINE_LENGTH];
	line[0] = *cmd;
	line[1] = ' ';
	line[2] = 0;
	/* URL? */
	char *url = is_url(file);
	if (url != NULL) {
		strcat(line, url);
		return writecmd(line);
	}
	/* Relative path? */
	if (file[0] != '/') {
		getcwd(line + 2, LINE_LENGTH);
		strcat(line, "/");
	}
	strcat(line, file);
	mode_t type = get_ftype(line + 2);
	switch (type) {
	case S_IFDIR:
		return cmd_foreach_in(line);
	case S_IFREG:
		return writecmd(line);
	default:
		printfd(2, "Wrong file type %d: %s\n", type, line + 2);
		return 12;
	}
}

static int client_cmd(char *cmd)
{
	if (*cmd != 'S')
		return 15;
	FILE *logfile = fopen("/var/log/omxlog", "r");
	if (logfile == NULL)
		return 15;
	char line[LINE_LENGTH];
	char playing[LINE_LENGTH];
	*playing = 0;
	int paused = 0;
	while (fgets(line, LINE_LENGTH, logfile)) {
		if (strstr(line, "player: PID=") == line) {
			strtok(line, " ");
			strtok(NULL, " ");
			strcpy(playing, strtok(NULL, "\n"));
			paused = 0;
		}
		if (strstr(line, "player: Send p ") == line)
			paused = !paused;
	}
	char *st = *playing == 0 ? "Stopped" : paused ? "Paused" : "Playing";
	printfd(1, "%s %s\n", st, playing);
	return 0;
}

static char *is_url(char *file)
{
	char *url_sep = strstr(file, "://");
	if (url_sep == NULL)
		return NULL;
	if (strncmp(file, "rtmpt", 5) == 0) {
		memcpy(file + 1, file, 4);
		return file + 1;
	}
	return file;
}

static mode_t get_ftype(char *file)
{
	struct stat filestat;
	if (stat(file, &filestat) == -1) {
		printfd(2, "Could not stat %s\n", file);
		return 0;
	}
	return filestat.st_mode & S_IFMT;
}

static int cmd_foreach_in(char *cmd)
{
	if (strchr("IHJ", *cmd)) {
		writestr(2, "Temporary injection (IHJ) for files only\n");
		return 13;
	}
	if (cmd[strlen(cmd) - 1] != '/')
		strcat(cmd, "/");
	struct dirent **entries;
	int n = scandir(cmd + 2, &entries, NULL, alphasort);
	if (n < 0) {
		printfd(2, "Unable to scan dir %s\n", cmd + 2);
		return 14;
	}
	int i;
	for (i = 0; i < n; ++i) {
		char *entry = entries[i]->d_name;
		if (*entry == '.')
			goto free_entry;
		char line[LINE_LENGTH];
		strcpy(line, cmd);
		strcat(line, entry);
		switch (get_ftype(line + 2)) {
		case S_IFDIR:
			cmd_foreach_in(line);
			break;
		case S_IFREG:
			writecmd(line);
			if (*cmd == 'i')
				*cmd = 'a';
			break;
		default:
			break;
		}
	free_entry:
		free(entries[i]);
	}
	free(entries);
	return 0;
}

static int writecmd(char *cmd)
{
	strcat(cmd, "\n");
	const char *const filters[] = {
		"jpg\n","JPG\n","m3u\n","txt\n","nfo\n","sfv\n","log\n",NULL
	};
	int i;
	for (i = 0; filters[i] != NULL; ++i) {
		if (strstr(cmd, filters[i]) != NULL)
			return 0;
	}
	int cmdfd = open_tout("omxctl");
	if (cmdfd < 0) {
		cmdfd = open_tout("/var/run/omxctl");
		if (cmdfd < 0) {
			writestr(2, "Can't open omxctl or /var/run/omxctl\n");
			return 10;
		}
	}
	writestr(2, cmd);
	writestr(cmdfd, cmd);
	close(cmdfd);
	/*
	 * Give up the CPU to the omxd daemon to read the above line from FIFO.
	 * The clean solution to this race condition would be to teach the
	 * daemon to read multiple lines from the FIFO after a single open().
	 */
	usleep(10000);
	return 0;
}

static int open_tout(char *file)
{
	strcpy(file_opening, file);
	alarm(1);
	int fd = open(file, O_WRONLY);
	alarm(0);
	file_opening[0] = 0;
	return fd;
}

static void open_tout_handler(int signal)
{
	printfd(2,
		"Opening FIFO '%s' timed out.\n"
		"The omxd daemon is dead or listening on another one.\n"
		"An unprivileged omxd listens on omxctl in its current dir.\n"
		"One started as root listens on '/var/run/omxctl'.\n",
		file_opening);
	_exit(10);
}
