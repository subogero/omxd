/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include "omxd.h"
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

static void player_quit(int signum);
static void watchdog(int signum);
static void drop_priv(void);

static void init(void);

struct player {
	pid_t pid;
	int wpipe;
	int t_lastcmd;
	int dt;
	enum pstate state;
	char file[LINE_LENGTH];
	char logfile[LINE_LENGTH];
};
#define NUM_PLAYERS 3
static struct player p[NUM_PLAYERS];
static struct player *find_free(void);
static struct player *find_pid(pid_t pid);

static void player_cleanup(struct player *this);

static char vol_sz[11] = "0";

static void init_opts(void);
static void log_opts(char *prefix);

/* opts.argc is the number of args, excluding the closing NULL */
static struct { int argc; char **argv; } opts =
              {       -1,        NULL, };

#define DT_WATCHDOG 5
static void init(void)
{
	if (opts.argv == NULL) {
		init_opts();
		signal(SIGALRM, watchdog);
		alarm(DT_WATCHDOG);
	}
}

static void watchdog(int signum)
{
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i) {
		int t_play = player_dt(p + i);
		int t_len = player_length(p[i].logfile);
		if (t_play == -1 || t_len == 0 || t_play <= t_len)
			continue;
		LOG(0, "watchdog: t_play = %d, t_len = %d\n", t_play, t_len);
		char cmd[50] = { 0, };
		strcpy(cmd, "/usr/bin/omxwd ");
		scatd(cmd, p[i].pid);
		system(cmd);
	}
	signal(SIGALRM, watchdog);
	alarm(DT_WATCHDOG);
}

struct player *player_new(char *file, char *out, enum pstate state)
{
	init();
	if (file == NULL || *file == 0)
		return NULL;
	struct player *this = find_free();
	if (this == NULL)
		return NULL;
	/* Timekeeping */
	this->t_lastcmd = time(NULL);
	this->dt = 0;
	/* IPC, args */
	int ctrlpipe[2];
	pipe(ctrlpipe);
	opts.argv[opts.argc - 2] = out;
	opts.argv[opts.argc - 1] = file;
	opts.argv[opts.argc - 0] = NULL;
	strcpy(this->logfile, OMX_FILE);
	this->pid = fork();
	if (this->pid < 0) { /* Fork error */
		this->pid = 0;
		close(ctrlpipe[0]);
		close(ctrlpipe[1]);
		this->state = P_DEAD;
		return NULL;
	} else if (this->pid > 0) { /* Parent: set SIGCHLD handler */
		close(ctrlpipe[0]);
		this->wpipe = ctrlpipe[1];
		if (state == P_PAUSED)
			write(this->wpipe, "p", 1);
		this->state = state;
		signal(SIGCHLD, player_quit);
		signal(SIGPIPE, SIG_IGN);
		strcpy(this->file, file);
		scatd(this->logfile, this->pid);
		LOG(0, "player_new: PID=%d %s\n", this->pid, file);
		return this;
	} else { /* Child: exec omxplayer */
		scatd(this->logfile, getpid());
		dup2(creat(this->logfile, 0644), 2);
		drop_priv();
		close(ctrlpipe[1]);
		/* Redirect stdin 0 to read end of control pipe */
		if (ctrlpipe[0] != 0) {
			close(0);
			dup(ctrlpipe[0]);
			close(ctrlpipe[0]);
		}
		execve(opts.argv[0], opts.argv, NULL);
		logfd = open(LOG_FILE, O_WRONLY|O_APPEND);
		log_opts(strerror(errno));
		_exit(20);
	}
}

void player_cmd(struct player *this, char *cmd)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	if (strchr(OMX_CMDS, *cmd) == NULL)
		return;
	/* Timekeeping */
	int t = time(NULL);
	if (this->state == P_PLAYING)
		this->dt += t - this->t_lastcmd;
	this->t_lastcmd = t;
	this->dt += *cmd == 'F' ?  600
	          : *cmd == 'R' ? -600
	          : *cmd == 'f' ?  30
	          : *cmd == 'r' ? -30
	          :                0;
	if (this->dt < 0)
		this->dt = 0;
	if (*cmd == 'p')
		this->state = this->state == P_PLAYING ? P_PAUSED : P_PLAYING;
	/* Replace FRfr with arrow-key escape sequences */
	if      (*cmd == 'F')
		cmd = "\033[A";
	else if (*cmd == 'R')
		cmd = "\033[B";
	else if (*cmd == 'f')
		cmd = "\033[C";
	else if (*cmd == 'r')
		cmd = "\033[D";
	writestr(this->wpipe, cmd);
	LOG(0, "player_cmd: Send %s to omxplayer PID/fd %d/%d\n",
		cmd, this->pid, this->wpipe);
}

void player_off(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	int pid = this->pid;
	LOG(0, "player_off: PID %d\n", pid);
	player_cleanup(this);
	char cmd[50] = { 0, };
	strcpy(cmd, "/usr/bin/omxwd ");
	scatd(cmd, pid);
	system(cmd);
}

void player_killall(void)
{
	LOG(0, "player_killall\n");
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i)
		player_cleanup(p + i);
	system("/usr/bin/omxwd");
}

const char *player_file(struct player *this)
{
	return this == NULL || this->state == P_DEAD
	     ? NULL
	     : (const char*)this->file;
}

const char *player_logfile(struct player *this)
{
	return this == NULL || this->state == P_DEAD
	     ? NULL
	     : (const char*)this->logfile;
}

int player_dt(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return -1;
	int t = time(NULL);
	if (this->state == P_PLAYING)
		this->dt += t - this->t_lastcmd;
	this->t_lastcmd = t;
	return this->dt;
}

enum pstate player_state(struct player *this)
{
	return this == NULL ? P_DEAD : this->state;
}

void player_add_opt(char *opt)
{
	init();
	if (opt == NULL || *opt == 0) {
		init_opts();
		return;
	}
	opts.argc++;
	int size = opts.argc + 1;
	opts.argv = realloc(opts.argv, size * sizeof(char*));
	int optsize = strlen(opt) + 1; /* Add 1 char for closing zero */
	int pos_new_opt = size - 4; /* Before closing NULL, file and audio */
	opts.argv[pos_new_opt] = malloc(optsize * sizeof(char));
	strcpy(opts.argv[pos_new_opt], opt);
}

void player_set_vol(int vol_mB)
{
	init();
	vol_sz[0] = 0;
	scatd(vol_sz, vol_mB);
	if (opts.argv != NULL)
		opts.argv[3] = vol_sz;
}

static void init_opts(void)
{
	#define ARGC_FIXED 4
	#define ARGC_DEFAULT (ARGC_FIXED + 2)
	/* Free the entire argv array if necessary */
	if (opts.argv != NULL) {
		int i;
		/* Only the added options are dynamically allocated */
		for (i = ARGC_FIXED; i < opts.argc - 2; ++i) {
			if (opts.argv[i] != NULL)
				free(opts.argv[i]);
		}
		free(opts.argv);
		opts.argv = NULL;
		opts.argc = -1;
	}
	opts.argc = ARGC_DEFAULT;
	int size = opts.argc + 1;
	opts.argv = malloc(size * sizeof(char*));
	opts.argv[0] = "/usr/bin/omxplayer";
	opts.argv[1] = "-I";
	opts.argv[2] = "--vol";
	opts.argv[3] = vol_sz;
	opts.argv[4] = NULL; /* Audio output option */
	opts.argv[5] = NULL; /* File */
	opts.argv[6] = NULL; /* Closing NULL pointer */
}

static void log_opts(char *prefix)
{
	int i;
	char msg[LINE_LENGTH];
	strcpy(msg, prefix);
	for (i = 0; i < opts.argc; ++i) {
		strcat(msg, "  ");
		strcat(msg, opts.argv[i]);
	}
	LOG(0, msg);
}

static void player_quit(int signum)
{
	while (1) {
		int status;
		pid_t pid = waitpid(0, &status, WNOHANG);
		status = WEXITSTATUS(status);
		/* pid 0: no processes to reap */
		if (pid == 0)
			return;
		/* pid -1 error: retry if waitpid interrupted by new signal */
		if (pid == -1) {
			int err = errno;
			LOG(0, "player_quit: %s\n", strerror(err));
			if (err == EINTR)
				continue;
			return;
		}
		LOG(0, "player_quit: PID=%d with %d\n", pid, status);
		/* Clean up player object exited by itself */
		struct player *this = find_pid(pid);
		if (this == NULL)
			return;
		if (this->state != P_DEAD) {
			player_cleanup(this);
			quit_callback(this);
		}
	}
}

static void player_cleanup(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	close(this->wpipe);
	this->pid = 0;
	this->file[0] = 0;
	this->state = P_DEAD;
	unlink(this->logfile);
	this->logfile[0] = 0;
}

static struct player *find_free(void)
{
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i)
		if (p[i].state == P_DEAD)
			return p + i;
	return NULL;
}

static struct player *find_pid(pid_t pid)
{
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i)
		if (p[i].pid == pid)
			return p + i;
	return NULL;
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
