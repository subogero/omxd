/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include "omxd.h"
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

static void player_quit(int signum);
static void drop_priv(void);

struct player {
	pid_t pid;
	int wpipe;
	enum pstate state;
	char file[LINE_LENGTH];
};
#define NUM_PLAYERS 3
static struct player p[NUM_PLAYERS];
static struct player *find_free(void);
static struct player *find_pid(pid_t pid);

static void init_opts(void);
static void log_opts(void);

/* opts.argc is the number of args, excluding the closing NULL */
static struct { int argc; char **argv; } opts =
              {       -1,        NULL, };

struct player *player_new(char *file, char *out, enum pstate state)
{
	if (opts.argv == NULL)
		init_opts();
	if (file == NULL || *file == 0)
		return NULL;
	struct player *this = find_free();
	if (this == NULL)
		return NULL;
	int ctrlpipe[2];
	pipe(ctrlpipe);
	opts.argv[opts.argc - 2] = out;
	opts.argv[opts.argc - 1] = file;
	opts.argv[opts.argc - 0] = NULL;
	log_opts();
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
		signal(SIGPIPE, player_quit);
		strcpy(this->file, file);
		LOG(1, "player_new: PID=%d %s\n", this->pid, file);
		return this;
	} else { /* Child: exec omxplayer */
		drop_priv();
		close(ctrlpipe[1]);
		/* Redirect read end of control pipe to 0 stdin */
		if (ctrlpipe[0] != 0) {
			close(0);
			dup(ctrlpipe[0]);
			close(ctrlpipe[0]);
		}
		execve(opts.argv[0], opts.argv, NULL);
		_exit(20);
	}
}

void player_cmd(struct player *this, char *cmd)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	if (strchr(OMX_CMDS, *cmd) == NULL)
		return;
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
	LOG(1, "player_cmd: Send %s to omxplayer PID/fd %d/%d\n",
		cmd, this->pid, this->wpipe);
}

void player_off(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	LOG(1, "player_off: PID %d\n", this->pid);
	write(this->wpipe, "q", 1);
	close(this->wpipe);
	this->pid = 0;
	this->file[0] = 0;
	this->state = P_DEAD;
}

const char *player_file(struct player *this)
{
	return this == NULL || this->state == P_DEAD
	     ? NULL
	     : (const char*)this->file;
}

void player_add_opt(char *opt)
{
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

static void init_opts(void)
{
	/* Free the entire argv array if necessary */
	if (opts.argv != NULL) {
		int i;
		/* Only the added options are dynamically allocated */
		for (i = 3; i <= opts.argc - 3; ++i) {
			if (opts.argv[i] != NULL)
				free(opts.argv[i]);
		}
		free(opts.argv);
		opts.argv = NULL;
		opts.argc = -1;
	}
	opts.argc = 5;
	int size = opts.argc + 1;
	opts.argv = malloc(size * sizeof(char*));
	opts.argv[0] = "/usr/bin/omxplayer";
	opts.argv[1] = "-I";
	opts.argv[2] = "--no-osd";
	opts.argv[3] = NULL; /* Audio output option */
	opts.argv[4] = NULL; /* File */
	opts.argv[5] = NULL; /* Closing NULL pointer */
}

static void log_opts(void)
{
	int i;
	for (i = 0; i <= opts.argc; ++i)
		LOG(1, "argv %d = %s\n", i, opts.argv[i] == NULL ? "NULL" : opts.argv[i]);
}

static void player_quit(int signum)
{
	if (signum == SIGPIPE)
		return;
	int status;
	pid_t pid = wait(&status);
	struct player *this = find_pid(pid);
	if (this == NULL)
		return;
	status = WEXITSTATUS(status);
	if (this->state != P_DEAD) {
		close(this->wpipe);
		this->pid = 0;
		this->file[0] = 0;
		this->state = P_DEAD;
		quit_callback(this);
	}
	LOG(1, "player_quit: PID=%d (%d) with %d\n", pid, status);
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
