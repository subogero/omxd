/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#define _FILE_OFFSET_BITS 64
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

static int client_cmd(char *cmd, char *file);
static int player_length(char *omxp_log);
static void print_list(char *playing);
static char *is_url(char *file);
static int cmd_foreach_in(char *cmd);
static int writecmd(char *cmd);
static int writeopts(char *cmd, int argc, char *argv[]);
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
		return client_cmd(cmd, file);
	if (strchr(OMX_CMDS LIST_CMDS STOP_CMDS OPT_CMDS, *cmd) == NULL)
		return 11;
	/* Pass omxplayer options */
	if (strchr(OPT_CMDS, *cmd)) {
		return writeopts(cmd, argc, argv);
	}
	if (file == NULL)
		return writecmd(cmd);
	char line[LINE_LENGTH];
	line[0] = *cmd;
	line[1] = ' ';
	line[2] = 0;
	/* Direct jump/delete */
	if (strchr("gx", *cmd)) {
		strcat(line, file);
		return writecmd(line);
	}
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
	case S_IFIFO:
		return writecmd(line);
	default:
		printfd(2, "Wrong file type %d: %s\n", type, line + 2);
		return 12;
	}
}

static int client_cmd(char *cmd, char *file)
{
	if (*cmd != 'S')
		return 15;
	char st[LINE_LENGTH] = { 0, };
	char playing[LINE_LENGTH] = { 0, };
	int t_play, t_len;
	int pid = 0;
	int res = parse_status(st, playing, &t_play, &t_len, &pid);
	if (res)
		return res;
	/* Print */
	printfd(1, "%s %d/%d %s\n", st, t_play, t_len, playing);
	if (file != NULL && strncmp(file, "all", 4) == 0)
		print_list(playing);
	return 0;
}

int parse_status(char *st, char *playing, int *t_play, int *t_len, int *pid)
{
	/* Open status log file */
	FILE *logfile = fopen("omxstat", "r");
	if (logfile == NULL) {
		logfile = fopen("/var/log/omxstat", "r");
		if (logfile == NULL)
			return 15;
	}
	/* Vars for line and fields */
	char line[LINE_LENGTH];
	int t_last;
	int t = time(NULL);
	if (fgets(line, LINE_LENGTH, logfile) == NULL) {
		fclose(logfile);
		return 15;
	}
	/* Parse line: timestamp state dt omxplayer.logfile file */
	sscand(strtok(line, " \n"), &t_last);
	char *tok = strtok(NULL, " \n");
	if (tok != NULL)
		strcpy(st, tok);
	sscand(strtok(NULL, " \n"), t_play);
	char *omxp_log = strtok(NULL, " \n");
	if (omxp_log != NULL)
		sscand(omxp_log + 23, pid); /* Keep PID from logfile name */
	tok = strtok(NULL, "\n");
	if (tok != NULL)
		strcpy(playing, tok);
	/* Special case: Stopped */
	if (strncmp(st, "Stopped", 8) == 0) {
		*t_play = 0;
		*t_len = 0;
		playing[0] = 0; /* Empty string */
	} else {
		/* Extract track length from omxplayer log file */
		*t_len = player_length(omxp_log);
		/* Add dt since last command to t_play if not paused */
		if (strncmp(st, "Paused", 8) != 0)
			*t_play += t - t_last;
	}
	fclose(logfile);
	return 0;
}

/* Get track length from omxplayer logfile */
static int player_length(char *omxp_log)
{
	if (omxp_log == NULL)
		return 0;
	FILE *log = fopen(omxp_log, "r");
	if (log == NULL)
		return 0;
	int t = 0;
	char line[LINE_LENGTH];
	while (fgets(line, LINE_LENGTH, log)) {
		if (strncmp(strtok(line, " "), "Duration:", 9) != 0)
			continue;
		int unit;
		char *hours = strtok(NULL, ":");
		if (strlen(hours) != 2 ||
		    !strchr("0123456789", hours[0]) ||
		    !strchr("0123456789", hours[1])) {
			t = -1;
			break;
		}
		sscand(hours, &unit);
		t += 3600 * unit;
		sscand(strtok(NULL, ":"), &unit);
		t += 60 * unit;
		sscand(strtok(NULL, "., "), &unit);
		t += unit;
		break;
	}
	fclose(log);
	return t;
}

static void print_list(char *playing)
{
	FILE *play = fopen(LIST_FILE, "r");
	if (play == NULL) {
		int I_root = 1;
		play = fopen(LIST_FILE, "r");
		if (play == NULL)
			return;
	}
	char line[LINE_LENGTH];
	while (fgets(line, LINE_LENGTH, play)) {
		if (playing != NULL && *playing != 0 &&
		    strstr(line, playing) == line)
			printf("> ");
		printf(line);
	}
	fclose(play);
}

/* Other helpers */
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

mode_t get_ftype(char *file)
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

static int writeopts(char *cmd, int argc, char *argv[])
{
	char cmds[LINE_LENGTH];
	*cmds = 0;
	int i;
	for (i = 2; i < argc; ++i) {
		strcat(cmds, cmd);
		strcat(cmds, " ");
		strcat(cmds, argv[i]);
		strcat(cmds, "\n");
	}
	strcat(cmds, ".");
	return writecmd(cmds);
}

static int writecmd(char *cmd)
{
	if (cmd[strlen(cmd)-1] != '\n')
		strcat(cmd, "\n");
	const char *const filters[] = {
		"jpg\n","JPG\n","jpeg\n","JPEG\n","m3u\n","txt\n","nfo\n","sfv\n","log\n",NULL
	};
	int i;
	for (i = 0; filters[i] != NULL; ++i) {
		if (*cmd != 'O' && strstr(cmd, filters[i]) != NULL)
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
