#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/param.h>
#include "config_parser.h"
#include "output.h"
#include "list.h"

#ifdef MEMWATCH
	#include "memwatch.h"
#endif


#define MAX_OPT_LEN	50
#define MAX_PARAM_LEN	2000

static const char *delim = ";;";

extern char *feed_url;
extern int check_interval;
extern linked_list regex_patterns;
extern char log_file[MAXPATHLEN + 1];
extern char state_file[MAXPATHLEN + 1];
extern int use_transmission;

static int init_regex_patterns(const char *pattern_str);

void shorten(char *str) {
		char tmp[MAX_PARAM_LEN + 1];
		int tmp_pos;
		char c;
		int line_pos = 0, i;
		int len = strlen(str);

		for(i = 0; i < sizeof(tmp); ++i)
			tmp[i] = '\0';

		while (isspace(str[line_pos])) {
			++line_pos;
		}
		tmp_pos = 0;
		while(line_pos < len) {
			/* case 1: quoted strings */
			if(tmp_pos != 0) {
				for(i = 0; i < strlen(delim); ++i)
					tmp[tmp_pos++] = delim[i];
			}
			if (str[line_pos] == '"' || str[line_pos] == '\'') {
				c = str[line_pos];
				++line_pos;  /* skip quote */
				for (; str[line_pos] != c; /* NOTHING */) {
						tmp[tmp_pos++] = str[line_pos++];
				}
				line_pos++;	/* skip the closing " or ' */
			} else {
				for(; isprint(str[line_pos]) && !isspace(str[line_pos]); /* NOTHING */) {
					tmp[tmp_pos++] = str[line_pos++];
				}
			}
			while (isspace(str[line_pos])) {
				++line_pos;
			}
		}
		tmp[tmp_pos] = '\0';
		assert(strlen(tmp) < MAX_PARAM_LEN);
		strncpy(str, tmp, strlen(tmp) + 1);
}

int set_option(const char *opt, char *param, option_type type) {
	int i;
	int numval;
	dbg_printf(P_INFO, "%s=%s (type: %d)\n", opt, param, type);
	if(!strcmp(opt, "url")) {
		feed_url = realloc(feed_url, strlen(param) + 1);
		if(feed_url)
			strncpy(feed_url, param, strlen(param) + 1);
	}else if(!strcmp(opt, "logfile")) {
		if(strlen(param) < MAXPATHLEN)
			strncpy(log_file, param, strlen(param) + 1);
	}else if(!strcmp(opt, "statefile")) {
		if(strlen(param) < MAXPATHLEN)
			strncpy(state_file, param, strlen(param) + 1);
	} else if(!strcmp(opt, "interval")) {
		numval = 1;
		for(i = 0; i < strlen(param); i++) {
			if(isdigit(param[i]) == 0)
				numval--;
		}
		if(numval == 1) {
			check_interval = atoi(param);
		} else {
			dbg_printf(P_ERROR, "Unknown parameter: %s=%s\n", opt, param);
		}
	} else if(!strcmp(opt, "use-transmission")) {
		if(!strncmp(param, "0", 1) || !strncmp(param, "no", 2)) {
			use_transmission = 0;
		} else if(!strncmp(param, "1", 1) || !strncmp(param, "yes", 3)) {
			use_transmission = 1;
		} else {
			dbg_printf(P_ERROR, "WARN: unknown parameter: %s=%s\n", opt, param);
		}
	} else if(!strcmp(opt, "patterns")) {
		shorten(param);
		init_regex_patterns(param);
	} else {
		dbg_printf(P_ERROR, "[set_option] Unknown option: %s\n", opt);
	}
	return 0;
}

static int init_regex_patterns(const char *pattern_str) {
	rss_item re;
	char *buf, *p;
	int len;

	re = newRSSItem();
	buf = calloc(1, strlen(pattern_str) + 1);
	strncpy(buf, pattern_str, strlen(pattern_str) + 1);
	p = strtok(buf, delim);
	while (p) {
		len = strlen(p);
		re.name = malloc(len + 1);
		if(re.name) {
			dbg_printf(P_INFO2, "[init_regex_patterns] allocated %d bytes for '%s'\n", len + 1, p);
			strcpy(re.name, p);
			addRSSItem(re, &regex_patterns);
		} else {
			perror("malloc");
		}
		p = strtok(NULL, delim);
	}
	free(buf);
	dbg_printf(P_INFO2, "[init_regex_patterns] freed 'buf'\n");
	return 0;
}

int parse_config_file(const char *filename) {
	FILE *fp;
	char *line;
	char opt[MAX_OPT_LEN + 1];
	char param[MAX_PARAM_LEN + 1];
	char erbuf[100];
	char c;		/* for the "" and '' check */
	int line_num = 0;
	int line_pos;	/* line pos */
	int opt_pos;	/* opt pos */
	int param_pos;	/* param pos */
	int parse_error = 0;
	int opt_good = 0;
	int param_good = 0;
	struct stat fs;
	int ret = 0;
	option_type type;

	if(stat(filename, &fs) == -1)  {
		return -1;
	}
	dbg_printf(P_INFO2, "Configuration file size: %d\n", fs.st_size);

	if ((fp = fopen(filename, "rb")) == NULL) {
		perror("fopen");
		return -1;
	}

	if ((line = malloc(fs.st_size + 1)) == NULL) {
		dbg_printf(P_ERROR, "Can't allocate memory for 'line': %s (%ldb)\n", strerror(errno), fs.st_size + 1);
		return -1;
	}

	if(fread(line, fs.st_size, 1, fp) != 1) {
		perror("fread");
		free(line);
		return -1;
	}
	if(fp)
		fclose(fp);

	line_pos = 0;

	while(line_pos != fs.st_size) {
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				dbg_printf(P_INFO2, "skipping newline (line %d)\n", line_num);
				line_num++;
			}
			++line_pos;
		}

		/* comment */
		if (line[line_pos] == '#') {
			dbg_printf(P_INFO2, "skipping comment (line %d)\n", line_num);
			while (line[line_pos] != '\n') {
				++line_pos;
			}
			++line_num;
			++line_pos;  /* skip the newline as well */
		}
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				line_num++;
				dbg_printf(P_INFO2, "skipping newline (line %d)\n", line_num);
			}

			++line_pos;
		}

		/* read option */
		for (opt_pos = 0; isprint(line[line_pos]) && line[line_pos] != ' ' &&
				line[line_pos] != '#' && line[line_pos] != '='; /* NOTHING */) {
			opt[opt_pos++] = line[line_pos++];
			if (opt_pos >= MAX_OPT_LEN) {
				dbg_printf(P_ERROR, "too long option at line %d\n", line_num);
				parse_error = 1;
				opt_good = 0;
			}
		}
		if (opt_pos == 0 || parse_error == 1) {
			dbg_printf(P_ERROR, "parse error at line %d (pos: %d)\n", line_num, line_pos);
			parse_error = 1;
			break;
		} else {
			opt[opt_pos] = '\0';
			opt_good = 1;
		}
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				line_num++;
				dbg_printf(P_INFO2, "skipping newline (line %d)\n", line_num);
			}
			++line_pos;
		}

		/* check for '=' */
		if (line[line_pos++] != '=') {
			snprintf(erbuf, sizeof(erbuf), "Option '%s' needs a parameter (line %d)\n", opt, line_num);
			parse_error = 1;
			break;
		}

		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n') {
				line_num++;
				dbg_printf(P_INFO2, "skipping newline (line %d)\n", line_num);
			}
			++line_pos;
		}

		/* read the parameter */

		/* case 1: single string, no linebreaks allowed */
		if (line[line_pos] == '"' || line[line_pos] == '\'') {
			c = line[line_pos];
			++line_pos;  /* skip quote */
			param_good = 1;
			for (param_pos = 0; line[line_pos] != c; /* NOTHING */) {
				if(line_pos < fs.st_size && param_pos < MAX_PARAM_LEN && line[line_pos] != '\n') {
					param[param_pos++] = line[line_pos++];
				} else {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n",opt, line_num);
					parse_error = 1;
					param_good = 0;
					break;
				}
			}
			if(param_good) {
				line_pos++;	/* skip the closing " or ' */
				type = STRING_TYPE;
			} else {
				break;
			}
		/* case 2: multiple items, linebreaks allowed */
		} else if (line[line_pos] == '{') {
			dbg_printf(P_INFO2, "reading multiline param\n", line_num);
			c = '}';
			++line_pos;
			param_good = 1;
			for (param_pos = 0; line[line_pos] != c; /* NOTHING */) {
				if(line_pos < fs.st_size && param_pos < MAX_PARAM_LEN) {
					param[param_pos++] = line[line_pos++];
					if(line[line_pos] == '\n')
						line_num++;
				} else {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n", opt, line_num);
					parse_error = 1;
					param_good = 0;
					break;
				}
			}
			dbg_printf(P_INFO2, "multiline param: param_good=%d\n", param_good);
			if(param_good) {
				line_pos++;	/* skip the closing ')' */
				type = STRINGLIST_TYPE;
			} else {
				break;
			}
		/* Case 3: integers */
		} else {
			param_good = 1;
			for (param_pos = 0; isprint(line[line_pos]) && !isspace(line[line_pos])
					&& line[line_pos] != '#'; /* NOTHING */) {
				param[param_pos++] = line[line_pos++];
				if (param_pos >= MAX_PARAM_LEN) {
					snprintf(erbuf, sizeof(erbuf), "Option %s has a too long parameter (line %d)\n", opt, line_num);
					parse_error = 1;
					param_good = 0;
					break;
				}
			}
			if(param_good == 0) {
				break;
			}
			type = INT_TYPE;
		}
		param[param_pos] = '\0';
		dbg_printf(P_INFO2, "option: %s\n", opt);
		dbg_printf(P_INFO2, "param: %s\n", param);
		dbg_printf(P_INFO2, "-----------------\n");
		set_option(opt, param, type);
		/* skip whitespaces */
		while (isspace(line[line_pos])) {
			if(line[line_pos] == '\n')
				line_num++;
			++line_pos;
		}
	}

	if(parse_error == 1) {
		ret = -1;
	}
	if(line)
		free(line);
	return ret;
}

/*

int main(int argc, char **argv) {

	if(argc != 2) {
		printf("Usage: %s <conf-file>\n", argv[0]);
		exit(0);
	}
	if(parse_config_file(argv[1]) == -1)
		printf("Error parsing configuration file\n");
	return 0;
}


*/
