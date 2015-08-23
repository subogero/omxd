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
static int files(void);
static int read_fifo(char *line);
static int parse(char *line);
static void player(char *cmd, char **files);
static void stop_playback(struct player **this);
static char *get_output(char *cmd);
static void status_log(void);

static volatile char spinlock;
#define SPINLOCK_TAKE while (spinlock) { usleep(1000); } spinlock = 1;
#define SPINLOCK_RELEASE spinlock = 0;

static char **next_hdmi_filter(char **files);

int main(int argc, char *argv[])
{
	/* Help for -h */
	if (argc == 2 && strncmp(argv[1], "-h", 3) == 0) {
		writestr(1,
#include "omxd_help.h"
		);
		return 0;
	}
	/* Version for --version */
	if (argc == 2 && strncmp(argv[1], "--version", 10) == 0) {
		writestr(1,
#include "version.h"
		);
		return 0;
	}
	/* Client when called with options */
	int daemon = 1;
	int arg;
	for (arg = 1; arg < argc; ++arg) {
		if (strncmp(argv[arg], "-d", 3) == 0)
			loglevel = 1;
		else if (strncmp(argv[arg], "-n", 3) == 0)
			daemon = 0;
		else
			return client(argc, argv);
	}
	I_root = geteuid() == 0;
	if (argc == 1 && !I_root) {
		writestr(2, "Non-root daemon debug in current dir: omxd -d\n");
		return -1;
	}
	/* Daemonize */
	logfd = 2;
	int daemon_error = daemon ? daemonize() : 0;
	if (daemon_error > 0)
		return daemon_error;
	else if (daemon_error < 0)
		return 0;
	/* Set up FIFO */
	int files_error = files();
	if (files_error)
		return files_error;
	/* Main loop */
	status_log();
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
		int pidfd = creat(PID_FILE, 0644);
		if (pidfd < 0 || printfd(pidfd, "%d", pid) == 0)
			return 7;
		close(pidfd);
		printfd(1, "omxd daemon started, PID %d\n", pid);
		return -1;
	}
	/* umask and session ID */
	pid_t sid = setsid();
	if (sid < 0)
		return 2;
	/* Create log file as stdout and stderr */
	close(2);
	logfd = creat(LOG_FILE, 0644);
	close(0);
	close(1);
	if (logfd < 0)
		return 4;
	LOG(0, "daemonize: omxd started, SID %d\n", sid);
	return 0;
}

/* CD, set up files */
static int files(void)
{
	/* Run in /var/run if invoked as root, or allow testing in CWD */
	if (I_root && chdir("/var/run/") < 0)
		return 3;
	/* Create and open FIFO for command input as stdin */
	umask(0);
	unlink("omxctl");
	LOG(1, "files: Deleted original omxctl FIFO\n");
	if (mknod("omxctl", S_IFIFO | 0622, 0) < 0)
		return 6;
	LOG(1, "files: Created new omxctl FIFO\n");
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
		get_output(cmd);
		player(cmd, next_hdmi_filter(m_list(cmd, file)));
	}
	if (cmd != NULL && *cmd == 'O')
		player_add_opt(file);
	return cmd != NULL ? *cmd : 0;
}

/* Control the actual omxplayer */
static void player(char *cmd, char **files)
{
	SPINLOCK_TAKE
	if (strchr(STOP_CMDS, *cmd) != NULL) {
		LOG(0, "player: stop all\n");
		stop_playback(&now);
		stop_playback(&next);
		goto player_end;
	}
	if (strchr(OMX_CMDS, *cmd) != NULL && now != NULL ) {
		if (*cmd == 'p')
			LOG(0, "player: play/pause\n")
		else
			LOG(0, "player: send %s\n", cmd)
		player_cmd(now, cmd);
		status_log();
		goto player_end;
	}
	if (files == NULL)
		goto player_end;
	/* Now/next: NULL = leave player alone; "" = destroy player */
	if (files[0] != NULL) {
		stop_playback(&now);
		if (*files[0] != 0) {
			LOG(0, "player: start %s\n", files[0]);
			if (next != NULL &&
			    strcmp(files[0], player_file(next)) == 0) {
				now = next;
				next = NULL;
				player_cmd(now, "p");
				status_log();
			} else {
				now = player_new(files[0],
				                 get_output(cmd),
				                 P_PLAYING);
				status_log();
			}
		}
	}
	if (files[1] != NULL) {
		stop_playback(&next);
		if (*files[1] != 0) {
			LOG(1, "player: prime %s\n", files[1]);
			next = player_new(files[1], get_output(cmd), P_PAUSED);
		}
	}
player_end:
	SPINLOCK_RELEASE
}

/* Stop the playback immediately */
static void stop_playback(struct player **this)
{
	if (*this != NULL) {
		player_off(*this);
		*this = NULL;
	}
	if (*this == now)
		status_log();
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
		status_log();
		now_started = 1;
	}
	char **now_next = next_hdmi_filter(m_list("n", NULL));
	if (now_next == NULL)
		goto quit_callback_end;
	if (now_next[0] != NULL && *now_next[0] == 0) {
		LOG(0, "quit_callback: nothing to play");
		status_log();
	}
	if (now_next[0] != NULL && *now_next[0] && !now_started) {
		LOG(0, "quit_callback: start %s\n", now_next[0]);
		now = player_new(now_next[0], get_output("n"), P_PLAYING);
		status_log();
	}
	if (now_next[1] != NULL && *now_next[1]) {
		LOG(1, "quit_callback: prime %s\n", now_next[1]);
		next = player_new(now_next[1], get_output("n"), P_PAUSED);
	}
quit_callback_end:
	SPINLOCK_RELEASE
}

/* Disable low-gap for videos when audio over HDMI */
static char **next_hdmi_filter(char **files)
{
	static char *copy[2] = { NULL, NULL };
	if (files == NULL)
		return files;
	if (files[1] == NULL)
		return files;
	if (strcmp("-olocal", get_output(NULL)) == 0)
		return files;
	copy[0] = files[0];
	copy[1] = "";
	LOG(1, "next_hdmi_filter: disable next\n");
	return copy;
}

/* Return omxplayer argument to set output interface (Jack/HDMI) */
static char *get_output(char *cmd)
{
	enum e_outputs           { OUT_JACK,  OUT_HDMI };
	static char *outputs[] = { "-olocal", "-ohdmi" };
	static enum e_outputs output = OUT_JACK;
	enum e_outputs output_now;
	if (cmd == NULL)
		output_now = output;
	else if (*cmd == 'h')
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

static void status_log(void)
{
	unlink(STAT_FILE);
	int fd = creat(STAT_FILE, 0644);
	/* Format: timestamp state [dt logfile file] */
	enum pstate state = player_state(now);
	if (state == P_DEAD)
		printfd(fd, "%d Stopped\n", time(NULL));
	else
		printfd(
			fd,
			"%d %s %d %s %s\n",
			time(NULL),
			state == P_PAUSED ? "Paused"
			       :            "Playing",
			player_dt(now),
			player_logfile(now),
			player_file(now)
		);
	close(fd);
}
