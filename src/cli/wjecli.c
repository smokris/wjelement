#include "wjecli.h"

WJECLIGlobals wje;

extern WJECLIcmd WJECLIcmds[];

static void usage(char *arg0)
{
	WJECLIcmd	*cmd;
	int			i;

	if (arg0) {
		printf("Command line JSON document browser\n");
		printf("Usage: %s [json file]\n\n", arg0);

		printf("Commands:\n");
	}

	for (i = 0; (cmd = &WJECLIcmds[i]) && cmd->name; i++) {
		if (!cmd->description) {
			continue;
		}

		printf("\t%s %s\n", cmd->name, cmd->args ? cmd->args : "");
		printf("\t\t%s\n\n", cmd->description);
	}
}

static char * nextArg(int *c, int argc, char **argv)
{
	if ((*c) > argc || (*c) < 0) {
		return(NULL);
	}

	return(argv[(*c)++]);
}

static char * escapeString(char *src)
{
	size_t		l;
	char		*s, *d, *r;

	if (!src) {
		return(NULL);
	}

	/* Determine the size needed to hold the escaped string */
	l = 1;
	for (s = src; *s; s++) {
		switch (*s) {
			case '"':
			case '\\':
				l++;
				/* fallthrough */

			default:
				l++;
		}
	}

	if ((r = MemMallocWait(l))) {
		for (s = src, d = r; *s; s++) {
			switch (*s) {
				case '"':
				case '\\':
					*(d++) = '\\';
					/* fallthrough */

				default:
					*(d++) = *s;
					break;
			}
		}
		*d = '\0';
	}

	return(r);
}

char * nextField(char *value, char **end)
{
	char		*d, *s;

	if (end) {
		*end = NULL;
	}

	if (!value) {
		return(NULL);
	}

	value = skipspace(value);

	if (!*value) {
		return(NULL);
	}

	if ('"' == *value) {
		value++;

		d = s = value;
		while (s) {
			switch (*s) {
				case '"':
					*d = '\0';
					if (end) {
						*end = s + 1;
					}
					return(*value ? value : NULL);

				case '\\':
					switch (*(++s)) {
						case '"':
						case '\\':
							*(d++) = *(s++);
							break;

						default:
							*(d++) = '\\';
					}
					break;

				default:
					*(d++) = *(s++);
					break;
			}
		}
	} else {
		if ((d = strspace(value))) {
			*d = '\0';
			if (end) {
				*end = d + 1;
			}
		}

		return(*value ? value : NULL);
	}

	return(NULL);
}

static int runcmd(WJElement *doc, WJElement *current, char *cmd, char *args)
{
	WJECLIcmd		*command;
	int				i;
	int				r;

	for (i = 0; (command = &WJECLIcmds[i]) && command->name; i++) {
		if (!stricmp(cmd, command->name)) {
			break;
		}
	}

	if (!*current) {
		*current = *doc;
	}

	if (command && command->name) {
		return(command->cb(doc, current, args));
	} else if (!stricmp(cmd, "help")) {
		usage(NULL);
		return(0);
	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		return(3);
	}
}

int main(int argc, char **argv)
{
	FILE		*in			= NULL;
	WJElement	doc			= NULL;
	WJElement	current		= NULL;
	int			r			= 0;
	WJReader	reader;
	WJWriter	writer;
	char		*cmd, *args;
	char		line[1024];

	/* Print pretty documents by default */
	wje.pretty = TRUE;

	/* Print base 10 by default */
	wje.base = 10;

	if (argc > 2) {
		/* Umm, we only allow one argument... a filename */
		usage(argv[0]);
		return(1);
	}

	if (argc == 2) {
		if (!stricmp(argv[1], "--help") || !stricmp(argv[1], "-h")) {
			usage(argv[0]);
			return(0);
		}

		if (!(in = fopen(argv[1], "rb"))) {
			perror(NULL);
			return(2);
		}

		/*
			A filename was specified on the command line. Does this look
			like a script, or a JSON document?
		*/
		if (fgets(line, sizeof(line), in) &&
			!strncmp(line, "!#", 2)
		) {
			/* This looks like a script, read commands from this file */
			;
		} else {
			/* Assume it is a JSON document, rewind back to the start. */
			rewind(in);

			if ((reader = WJROpenFILEDocument(in, NULL, 0))) {
				doc = WJEOpenDocument(reader, NULL, NULL, NULL);
				WJRCloseDocument(reader);
			}

			fclose(in);
			in = NULL;

			if (!doc) {
				fprintf(stderr, "Could not parse JSON document: %s\n", argv[1]);
				return(3);
			}
		}
	}

	if (!in) {
		/* Read commands from standard in */
		in = stdin;
	}

	if (!doc) {
		/* Start with an empty document if one wasn't specified */
		doc = WJEObject(NULL, NULL, WJE_SET);
	}
	current = doc;

	MemoryManagerOpen("wje-cli");

	for (;;) {
		/* Read the next command */
		if (in == stdin) {
			fprintf(stdout, "wje");

			if (r) {
				fprintf(stdout, " (%d)", r);
			}

			fprintf(stdout, "> ");
			fflush(stdout);
		}

		args = NULL;
		if (fgets(line, sizeof(line), in)) {
			cmd = nextField(line, &args);
		} else {
			cmd = NULL;
		}

		if (!cmd) {
			/* Ignore blank lines */
		} else if (!stricmp(cmd, "exit") || !stricmp(cmd, "quit")) {
			break;
		} else if (!*cmd || '#' == *cmd) {
			/* Just ignore an empty line, and comments */
		} else {
			r = runcmd(&doc, &current, cmd, args);
		}
		cmd = NULL;

		if (feof(in)) {
			break;
		}
	}

	if (doc) {
		WJECloseDocument(doc);
		doc = NULL;
	}

	if (in && in != stdin) {
		fclose(in);
		in = NULL;
	}

	MemoryManagerClose("wje-cli");
	return(r);
}

