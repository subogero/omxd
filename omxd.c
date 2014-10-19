/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include "omxd.h"
#include "m_list.h"

struct player *pl = NULL;

static int daemonize(void);
static int read_fifo(char *line);
static int parse(char *line);
static void player(char *cmd, char **files);
static void stop_playback(void);
static char *get_output(char *cmd);

int main(int argc, char *argv[])
{
	/* Help for -h */
	if (argc == 2 && strncmp(argv[1], "-h", 3) == 0) {
		writestr(1,
#include "omxd_help.h"
		);
		return 0;
	}
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
	I_root = geteuid() == 0;
	/* Fork the real daemon */
	pid_t pid = fork();
	if (pid < 0)
		return 1;
	if (pid > 0) {
		int pidfd = creat(PID_FILE, 0644);
		if (pidfd < 0 || printfd(pidfd, "%d", pid) == 0)
			return 7;
		close(pidfd);
		printfd(1, "omxd daemon started, PID %d\n", pid);
		return -1;
	}
	/* umask and session ID */
	umask(0);
	pid_t sid = setsid();
	if (sid < 0)
		return 2;
	/* Run in /var/run if invoked as root, or allow testing in CWD */
	if (I_root && chdir("/var/run/") < 0)
		return 3;
	/* Create log file as stdout and stderr */
	close(2);
	logfd = creat(LOG_FILE, 0644);
	close(0);
	close(1);
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
		player(cmd, m_list(cmd, file));
	}
	return cmd != NULL ? *cmd : 0;
}

/* Control the actual omxplayer */
static void player(char *cmd, char **files)
{
	if (files != NULL && files[0] != NULL && *files[0] != 0) {
		stop_playback();
		pl = player_new(files[0], get_output(cmd), P_PLAYING);
	} else if (strchr(OMX_CMDS, *cmd) != NULL && pl != NULL ) {
		player_cmd(pl, cmd);
	} else if (strchr(STOP_CMDS, *cmd) != NULL && pl != NULL) {
		stop_playback();
	}
}

/* Stop the playback immediately */
static void stop_playback(void)
{
	if (pl != NULL) {
		player_off(pl);
		pl = NULL;
	}
}

/* Signal handler for SIGCHLD when player exits */
void quit_callback(struct player *this)
{
	if (this == pl)
		player("n", m_list("n", NULL));
	
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
