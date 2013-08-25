/* (c) SZABO Gergely <szg@subogero.com>, license GPLv3 */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "omxd.h"

static char *is_url(char *file);
static int cmd_foreach_in(char *cmd);
static int writecmd(char *cmd);

int client(int argc, char *argv[])
{
	char *cmd = argv[1];
	char *file = argc >= 3 ? argv[2] : NULL;
	/* Check command */
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
	/* Directory? */
	struct stat filestat;
	if (stat(line + 2, &filestat) == -1)
		return 12;
	if (S_ISDIR(filestat.st_mode))
		return cmd_foreach_in(line);
	/* Regular file */
	return writecmd(line);
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

static int cmd_foreach_in(char *cmd)
{
	return 0;
}

static int writecmd(char *cmd)
{
	int cmdfd = open("omxctl", O_WRONLY|O_NONBLOCK);
	if (cmdfd < 0) {
		cmdfd = open("/var/run/omxctl", O_WRONLY|O_NONBLOCK);
		if (cmdfd < 0) {
			writestr(2, "Can't open omxctl or /var/run/omxctl\n");
			return 10;
		}
	}
	strcat(cmd, "\n");
	writestr(2, cmd);
	writestr(cmdfd, cmd);
	close(cmdfd);
	return 0;
}
