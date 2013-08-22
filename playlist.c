/* (c) SZABO Gergely <szg@subogero.com>, license GPLv3 */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "omxd.h"

/* Count from 1, like vi line numbers, -1 invalid, 0 empty list */
static int i_list = -1;
static int size = 0;
static char file_playing[LINE_LENGTH] = { 0, };

static void init_list(void);
static void rewrite_list(int add, int del, int act, char *file, int orig_size);
static void insert_line(FILE *play, char *file, int activate, int *i);
/*
 * Manipulate the playlist, adding/removing files at various positions.
 * Return the filename to play now, or NULL if playback shall not be affected.
 */
char *playlist(char *cmd, char *file)
{
	/* Init playlist data if necessary */
	if (i_list == -1)
		init_list();
	/* Special cases when there is nothing to do */
	if (cmd == NULL || strchr(LIST_CMDS, *cmd) == NULL)
		return NULL;
	if (*cmd == 'I')
		return file;
	if (*cmd == '.')
		return file_playing;
	if (*cmd == 'X') {
		unlink("omxd.play");
		size = 0;
		i_list = 0;
		*file_playing = 0;
		return file_playing;
	}
	if (size == 0 && *cmd == 'i')
		*cmd = 'a';
	int orig_size = size;
	int add, del, act;
	switch (*cmd) {
	case 'i': add = i_list;   del = 0;      act = i_list;  size++; break;
	case 'a': add = i_list+1; del = 0,      act = i_list;  size++; break;
	case 'A': add = size+1;   del = 0;      act = i_list;  size++; break;
	case 'x': add = 0;        del = i_list; act = i_list;  size--; break;
	case 'n': add = 0;        del = 0;      act = i_list+1;        break;
	case 'N': add = 0;        del = 0;      act = i_list-1;        break;
	}
	if (act > size)
		act = 1;
	else if (act < 1)
		act = size;
	writestr(logfd, "playlist: add=");
	writedec(logfd, add);
	writestr(logfd, " del=");
	writedec(logfd, del);
	writestr(logfd, " act=");
	writedec(logfd, act);
	writestr(logfd, "\n");
	rewrite_list(add, del, act, file, orig_size);
	writestr(logfd, "playlist: Playing: ");
	writestr(logfd, file_playing);
	writestr(logfd, " size=");
	writedec(logfd, size);
	writestr(logfd, " index=");
	writedec(logfd, i_list);
	writestr(logfd, "\n");
	return size == 1          ? file_playing
	     : strchr("aA", *cmd) ? NULL
	     :                      file_playing;
}

/* Init list length and current index from file */
static void init_list(void)
{
	i_list = 0;
	size = 0;
	FILE *play = fopen("omxd.play", "r");
	if (play == NULL)
		return;
	char line[LINE_LENGTH];
	while (fgets(line, LINE_LENGTH, play)) {
		size++;
		if (strstr(line, ">\t") == line) {
			i_list = size;
			strcpy(file_playing, line + 2);
		}
	}
	fclose(play);
}

/* Rewrite list and adjust internal state */
static void rewrite_list(int add, int del, int act, char *file, int orig_size)
{
	FILE *bak = fopen("omxd.play", "r");
	unlink("omxd.play");
	FILE *play = fopen("omxd.play", "w");
	if (play == NULL) {
		if (bak != NULL)
			fclose(bak);
		writestr(logfd, "playlist: could not open omxd.play\n");
		return;
	}
	int i = 1;
	int old_size = 0;
	char line[LINE_LENGTH];
	while (i <= size) {
		/* Read a line from old play file, count lines, new size */
		char *line_from_bak = NULL;
		if (bak != NULL) {
			line_from_bak = fgets(line, LINE_LENGTH, bak);
			old_size += line_from_bak != NULL;
		}
		/* Check for grow/shrink by someone else, adjust state */
		if (line_from_bak != NULL) {
			if (old_size > orig_size) {
				if (add == size)
					add++;
				size++;
				writestr(logfd, "playlist: external grow\n");
			}
		} else if (orig_size != old_size) {
			size += old_size - orig_size;
			del = 0;
			if (add > size)
				add = size;
			if (i_list > size)
				i_list = size;
			writestr(logfd, "playlist: external shrink\n");
		}
		/* Add or delete a line */
		if (i == add) {
			insert_line(play, file, i == act, &i);
		} else if (i == del) {
			del = 0;
			continue;
		}
		/* Print original line to new play file */
		if (line_from_bak != NULL)
			insert_line(play, line, i == act, &i);
	}
	if (bak != NULL)
		fclose(bak);
	fclose(play);
}

/* Insert a line into the playlist file */
static void insert_line(FILE *play, char *line, int activate, int *i)
{
	/* Strip > indicator and LF */
	if (strstr(line, ">\t") == line)
		line += 2;
	char *lf = strchr(line, '\n');
	if (lf != NULL)
		*lf = 0;
	/* If active, update state, add leading > indicator */
	if (activate) {
		strcpy(file_playing, line);
		i_list = *i;
		fputs(">\t", play);
	}
	/* Add LF, print to file */
	strcat(line, "\n");
	fputs(line, play);
	/* Increment line counter */
	(*i)++;
}
