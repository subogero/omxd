/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "omxd.h"

char unsorted = 0;
char loop = 1;
static struct playlist { int i; int size; char **arr_sz; }
		list = {    -1,        0,          NULL, };

static char *now_next[2] = { NULL, NULL };
static char next_file[LINE_LENGTH];
static char inserted_next_file = 0;

enum list_pos { L_START, L_ACT, L_END, L_ALL };
enum e_dirs { D_1ST, D_PREV, D_ACT, D_NEXT, D_LAST, D_NUMOF };
static int dirs[D_NUMOF] = { 0, };

static void load_list(void);
static void save_list(void);
/* Return mask of actual/next played position modified */
static int insert(enum list_pos base, int offs, char *file);
static int delete(enum list_pos base, int offs);
static int jump(enum list_pos base, int offs);

static int get_pos(enum list_pos base, int offs, int wrap);

static void update_dirs(void);
int new_dir(char *last, char *this);

int wrapped(int i);
int to_num(char *file);
int di_random(void);

/*
 * Return a 2-element string array for tracks to play now and next:
 * - NULL: leave running players alone
 * - element NULL: leave that player alone
 * - element empty string: destroy that player
 * - element is file: start new player
 */
char **m_list(char *cmd, char *file)
{
	if (list.i == -1)
		load_list();
	/* Special cases when there is nothing to do */
	if (cmd == NULL || strchr(LIST_CMDS, *cmd) == NULL)
		return NULL;
	if (strchr("IHJ", *cmd) != NULL) {
		now_next[0] = file;
		now_next[1] = NULL;
		return now_next;
	}
	if (*cmd == 'L') {
		strncpy(next_file, file, LINE_LENGTH);
		inserted_next_file = 1;
		return NULL;
	}
	if (inserted_next_file && *cmd == 'n') {
		inserted_next_file = 0;
		now_next[0] = next_file;
		now_next[1] = NULL;
		return now_next;
	}
	if (*cmd == 'X') {
		delete(L_ALL, 0);
		return NULL;
	}
	if (*cmd == 'u') {
		unsorted = !unsorted;
		LOG(0, "m_list: unsorted %s\n", unsorted ? "on" : "off");
	}
	if (*cmd == 'l') {
		loop = !loop;
		LOG(0, "m_list: loop %s\n", loop ? "on" : "off");
	}
	int n = to_num(file);
	int di = unsorted ? di_random() : 1;
	int change =
		  *cmd == 'i' ? insert(L_ACT, 0, file)
		: *cmd == 'a' ? insert(L_ACT, 1, file)
		: *cmd == 'A' ? insert(L_END, 0, file)
		: *cmd == 'x' ? (n < 0 ? delete(L_ACT, 0) : delete(L_START, n))
		: *cmd == 'n' ? jump(L_ACT,  di)
		: *cmd == 'N' ? jump(L_ACT, -di)
		: *cmd == 'd' ? jump(L_START, dirs[D_NEXT])
		: *cmd == 'D' ? jump(L_START, dirs[D_PREV])
		: *cmd == 'g' ? jump(L_START, atoi(file))
		: *cmd == 'u' ? 0
		: *cmd == '.' ? 3
		: *cmd == 'h' ? 3
		: *cmd == 'j' ? 3
		:               0;
	if (list.size <= 1)
		change &= ~2;
	LOG(1, "m_list Change=%d size=%d i=%d\n", change, list.size, list.i);
	if (!change)
		return NULL;
	if (list.size > 0) {
		int i_next = list.i + 1;
		if (loop)
			i_next %= list.size;
		now_next[0] = (change & 1) == 0   ? NULL
		            : list.i >= list.size ? ""
		            :                       list.arr_sz[list.i];
		now_next[1] = (change & 2) == 0   ? NULL
		            : i_next >= list.size ? ""
		            :                       list.arr_sz[i_next];
		if (now_next[1] && unsorted)
			now_next[1] = "";
		return now_next;
	}
	now_next[0] = "";
	now_next[1] = "";
	return now_next;
}

static void load_list(void)
{
	list.size = 0;
	FILE *play = fopen(LIST_FILE, "r");
	if (play == NULL)
		return;
	char line[LINE_LENGTH];
	while (fgets(line, LINE_LENGTH, play)) {
		line[strlen(line) - 1] = '\0'; /* Remove trailing linefeed */
		/* Migrate automatically from in-file playlist */
		char *file = strstr(line, ">\t") == line ? line + 2 : line;
		insert(L_END, 0, file);
	}
	fclose(play);
}

static void save_list(void)
{
	update_dirs();
	if (list.size == 0) {
		unlink(LIST_FILE);
		return;
	}
	FILE *play = fopen(LIST_FILE, "w");
	int i;
	for (i = 0; i < list.size; ++i) {
		fputs(list.arr_sz[i], play);
		fputs("\n", play);
	}
	fclose(play);
}

static int insert(enum list_pos base, int offs, char *file)
{
	int i = list.size == 0 ? 0 : get_pos(base, offs, 0);
	if (i < 0)
		return 0;
	int size_new = list.size + 1;
	char **arr_new = realloc(list.arr_sz, size_new * sizeof(char*));
	if (arr_new == NULL) {
		LOG(0, "Unable to realloc playlist\n");
		return 0;
	}
	char *file_new = malloc(strlen(file) + 1);
	if (file_new == NULL) {
		LOG(0, "Unable to malloc filename %s\n", file);
		return 0;
	}
	strcpy(file_new, file);
	list.arr_sz = arr_new;
	memmove(list.arr_sz + i + 1,
	        list.arr_sz + i,
	        (list.size - i) * sizeof(char*));
	list.arr_sz[i] = file_new;
	list.size = size_new;
	if (list.i > i || list.i < 0)
		list.i++;
	save_list();
	return i == list.i ? 3 : i == list.i+1 ? 2 : 0;
}

static int delete(enum list_pos base, int offs)
{
	if (base == L_ALL && list.size > 0) {
		int i;
		for (i = 0; i < list.size; ++i)
			free(list.arr_sz[i]);
		free(list.arr_sz);
		list.i = -1;
		list.size =0;
		list.arr_sz = NULL;
		save_list();
		return 1;
	}
	int i = get_pos(base, offs, 0);
	if (i < 0)
		return 0;
	int size_new = list.size - 1;
	free(list.arr_sz[i]);
	memmove(list.arr_sz + i,
	        list.arr_sz + i + 1,
	        (size_new - i) * sizeof(char*));
	list.arr_sz = realloc(list.arr_sz, size_new * sizeof(char*));
	list.size = size_new;
	if (list.i > i)
		list.i--;
	save_list();
	return i == list.i ? 3 : i == list.i+1 ? 2 : 0;
}

static int jump(enum list_pos base, int offs)
{
	int i = get_pos(base, offs, loop);
	if (i >= 0) {
		list.i = i;
		update_dirs();
	}
	return 3;
}

static int get_pos(enum list_pos base, int offs, int wrap)
{
	int i_base =
		  list.i < 0      ? -1
		: base == L_START ? 0
		: base == L_ACT   ? list.i
		: base == L_END   ? list.size
		:                   -1;
	if (i_base == -1)
		return -1;
	int i = i_base + offs;
	return wrap ? wrapped(i) : i >= 0 && i <= list.size ? i : -1;
}

static void update_dirs(void)
{
	if (list.size == 0)
		return;
	memset(dirs, 0, sizeof dirs);
	int i;
	for (i = 0; i <= list.size; ++i) {
		int wi = wrapped(i);
		int wp = wrapped(i - 1);
		if (!new_dir(list.arr_sz[wp], list.arr_sz[wi]))
			continue;
		if (i < list.size)
			dirs[D_LAST] = i;
		if (dirs[D_NEXT] > list.i)
			continue;
		dirs[D_PREV] = dirs[D_ACT];
		dirs[D_ACT]  = dirs[D_NEXT];
		dirs[D_NEXT] = wi;
	}
	if (dirs[D_PREV] == dirs[D_ACT])
		dirs[D_PREV] = dirs[D_LAST];
	if (dirs[D_NEXT] == dirs[D_ACT])
		dirs[D_NEXT] = dirs[D_1ST];
}

int new_dir(char *last, char *this)
{
	if (last == NULL)
		return 1;
	int last_size = strrchr(last, '/') - last;
	int this_size = strrchr(this, '/') - this;
	if (this_size != last_size)
		return 1;
	if (strncmp(last, this, this_size) != 0)
		return 1;
	return 0;
}

int wrapped(int i)
{
	i %= list.size;
	return i < 0 ? list.size + i : i;
}

int to_num(char *file)
{
	return file == NULL || *file < '0' || *file > '9' ? -1 : atoi(file);
}

int di_random(void)
{
	int random = 1;
	int rfd = open("/dev/urandom", O_RDONLY);
	if (rfd >= 0) {
		read(rfd, &random, sizeof random);
		close(rfd);
	}
	LOG(1, "di_random: %d\n", random);
	/* Never jump zero when playback unsorted */
	if (random % list.size == 0)
		random += list.size / 2;
	return random;
}
