/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include "omxd.h"

struct player *now = NULL;
struct player *next = NULL;

static int daemonize(void);
static int read_fifo(char *line);
static int parse(char *line);
static void player(char *cmd, char **files);
static void stop_playback(struct player *this);
static char *get_output(char *cmd);

static volatile char spinlock;
#define SPINLOCK_TAKE while (spinlock) { usleep(1000); } spinlock = 1;
#define SPINLOCK_RELEASE spinlock = 0;

int main(int argc, char *argv[])
{
	I_root = geteuid() == 0;
	logfd = STDERR_FILENO;

	int should_daemonize = 1;
	int opt_count = 0;
	int i;
	for(i = 1; i < argc; i++) {
		if(strncmp(argv[i], "--version", 9) == 0) {
			writestr(1,
#include "version.h"
					);
			return 0;
		} else if(strncmp(argv[i], "-h", 2) == 0) {
			writestr(1,
#include "omxd_help.h"
					);
			return 0;
		} else if(strncmp(argv[i], "-n", 2) == 0) {
			should_daemonize = 0;
			opt_count++;
		} else if(strncmp(argv[i], "-d", 2) == 0) {
			loglevel = 1;
			opt_count++;
		} else if(strncmp(argv[i], "--", 2) == 0) {
			break;
		}
	}

	/* Client when called with arguments. */
	if (opt_count + 1 < argc)
		return client(argc, argv);

	/* Run in /var/run if invoked as root, or allow testing in CWD */
	if (I_root && chdir("/var/run/") < 0)
		return 3;

	if (should_daemonize) {
		int daemon_error = daemonize();
		if (daemon_error > 0)
			return daemon_error;
		else if (daemon_error < 0)
			return 0;
	}

	/* Create and open FIFO for command input as stdin */
	unlink("omxctl");
	LOG(1, "main: Deleted original omxctl FIFO.\n");
	const mode_t old_mask = umask(0);
	if (mknod("omxctl", S_IFIFO | 0622, 0) < 0)
		return 6;
	umask(old_mask);
	LOG(1, "main: Created new omxctl FIFO.\n");

	/* Main loop */
	while (1) {
		char line[LINE_LENGTH];
		read_fifo(line);
		LOG(0, "main: %s\n", line);
		parse(line);
	}
	return 0;
}

/* Fork, umask, SID, close, logfile. */
static int daemonize(void)
{
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

	/* Create log file as stdout and stderr */
	close(STDERR_FILENO);
	logfd = creat(LOG_FILE, 0644);
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	if (logfd < 0)
		return 4;
	LOG(0, "daemonize: omxd started, SID %d\n", sid);
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
			LOG(1, "read_fifo: Client opened omxctl\n");
		}
	}
	int i = 0;
	while (1) {
		if (!read(cmdfd, line + i, 1)) {
			LOG(1, "read_fifo: Client closed omxctl\n");
			close(cmdfd);
			cmdfd = -1;
			line[i] = 0;
			return i;
		} else if (line[i] == '\n') {
			LOG(1, "read_fifo: omxctl end of line\n");
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
	SPINLOCK_TAKE
	if (strchr(STOP_CMDS, *cmd) != NULL) {
		LOG(0, "player: stop all\n");
		stop_playback(now);
		stop_playback(next);
		goto player_end;
	}
	if (strchr(OMX_CMDS, *cmd) != NULL && now != NULL ) {
		if (*cmd == 'p')
			LOG(0, "player: play/pause\n")
		else
			LOG(0, "player: send %s\n", cmd)
		player_cmd(now, cmd);
		goto player_end;
	}
	if (files == NULL)
		goto player_end;
	/* Now/next: NULL = leave player alone; "" = destroy player */
	if (files[0] != NULL) {
		stop_playback(now);
		if (*files[0] != 0) {
			LOG(0, "player: start %s\n", files[0]);
			now = player_new(files[0], get_output(cmd), P_PLAYING);
		}
	}
	if (files[1] != NULL) {
		stop_playback(next);
		if (*files[1] != 0) {
			LOG(1, "player: prime %s\n", files[1]);
			next = player_new(files[1], get_output(cmd), P_PAUSED);
		}
	}
player_end:
	SPINLOCK_RELEASE
}

/* Stop the playback immediately */
static void stop_playback(struct player *this)
{
	if (this != NULL) {
		player_off(this);
		this = NULL;
	}
}

/* Signal handler for SIGCHLD when player exits */
void quit_callback(struct player *this)
{
	SPINLOCK_TAKE
	if (this == next) {
		next = NULL;
		goto quit_callback_end;
	}
	if (this != now)
		goto quit_callback_end;
	int now_started = 0;
	if (next != NULL) {
		now = next;
		next = NULL;
		player_cmd(now, "p");
		now_started = 1;
	}
	char **now_next = m_list("n", NULL);
	if (now_next == NULL)
		goto quit_callback_end;
	if (now_next[0] != NULL)
		LOG(0, "quit_callback: start %s\n", now_next[0]);
	if (now_next[0] != NULL && !now_started) {
		now = player_new(now_next[0], get_output("n"), P_PLAYING);
	}
	if (now_next[1] != NULL) {
		sleep(2);
		LOG(1, "quit_callback: prime %s\n", now_next[1]);
		next = player_new(now_next[1], get_output("n"), P_PAUSED);
	}
quit_callback_end:
	SPINLOCK_RELEASE
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
