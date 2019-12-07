/*
 * This file is part of RGBDS.
 *
 * Copyright (c) 1997-2018, Carsten Sorensen and RGBDS contributors.
 *
 * SPDX-License-Identifier: MIT
 */

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "asm/symbol.h"
#include "asm/fstack.h"
#include "asm/lexer.h"
#include "asm/output.h"
#include "asm/main.h"
#include "asm/charmap.h"
#include "asm/warning.h"

#include "extern/err.h"
#include "extern/getopt.h"

#include "helpers.h"
#include "version.h"

extern int yyparse(void);

size_t cldefines_index;
size_t cldefines_numindices;
size_t cldefines_bufsize;
const size_t cldefine_entrysize = 2 * sizeof(void *);
char **cldefines;

clock_t nStartClock, nEndClock;
int32_t nLineNo;
uint32_t nTotalLines, nPC, nIFDepth, nUnionDepth;
bool skipElif;
uint32_t unionStart[128], unionSize[128];

/* extern int yydebug; */

FILE *dependfile;

/*
 * Option stack
 */

struct sOptions DefaultOptions;
struct sOptions CurrentOptions;

struct sOptionStackEntry {
	struct sOptions Options;
	struct sOptionStackEntry *pNext;
};

struct sOptionStackEntry *pOptionStack;

void opt_SetCurrentOptions(struct sOptions *pOpt)
{
	if (nGBGfxID != -1) {
		lex_FloatDeleteRange(nGBGfxID, CurrentOptions.gbgfx[0],
				     CurrentOptions.gbgfx[0]);
		lex_FloatDeleteRange(nGBGfxID, CurrentOptions.gbgfx[1],
				     CurrentOptions.gbgfx[1]);
		lex_FloatDeleteRange(nGBGfxID, CurrentOptions.gbgfx[2],
				     CurrentOptions.gbgfx[2]);
		lex_FloatDeleteRange(nGBGfxID, CurrentOptions.gbgfx[3],
				     CurrentOptions.gbgfx[3]);
		lex_FloatDeleteSecondRange(nGBGfxID, CurrentOptions.gbgfx[0],
					   CurrentOptions.gbgfx[0]);
		lex_FloatDeleteSecondRange(nGBGfxID, CurrentOptions.gbgfx[1],
					   CurrentOptions.gbgfx[1]);
		lex_FloatDeleteSecondRange(nGBGfxID, CurrentOptions.gbgfx[2],
					   CurrentOptions.gbgfx[2]);
		lex_FloatDeleteSecondRange(nGBGfxID, CurrentOptions.gbgfx[3],
					   CurrentOptions.gbgfx[3]);
	}
	if (nBinaryID != -1) {
		lex_FloatDeleteRange(nBinaryID, CurrentOptions.binary[0],
				     CurrentOptions.binary[0]);
		lex_FloatDeleteRange(nBinaryID, CurrentOptions.binary[1],
				     CurrentOptions.binary[1]);
		lex_FloatDeleteSecondRange(nBinaryID, CurrentOptions.binary[0],
					   CurrentOptions.binary[0]);
		lex_FloatDeleteSecondRange(nBinaryID, CurrentOptions.binary[1],
					   CurrentOptions.binary[1]);
	}
	CurrentOptions = *pOpt;

	if (nGBGfxID != -1) {
		lex_FloatAddRange(nGBGfxID, CurrentOptions.gbgfx[0],
				  CurrentOptions.gbgfx[0]);
		lex_FloatAddRange(nGBGfxID, CurrentOptions.gbgfx[1],
				  CurrentOptions.gbgfx[1]);
		lex_FloatAddRange(nGBGfxID, CurrentOptions.gbgfx[2],
				  CurrentOptions.gbgfx[2]);
		lex_FloatAddRange(nGBGfxID, CurrentOptions.gbgfx[3],
				  CurrentOptions.gbgfx[3]);
		lex_FloatAddSecondRange(nGBGfxID, CurrentOptions.gbgfx[0],
					CurrentOptions.gbgfx[0]);
		lex_FloatAddSecondRange(nGBGfxID, CurrentOptions.gbgfx[1],
					CurrentOptions.gbgfx[1]);
		lex_FloatAddSecondRange(nGBGfxID, CurrentOptions.gbgfx[2],
					CurrentOptions.gbgfx[2]);
		lex_FloatAddSecondRange(nGBGfxID, CurrentOptions.gbgfx[3],
					CurrentOptions.gbgfx[3]);
	}
	if (nBinaryID != -1) {
		lex_FloatAddRange(nBinaryID, CurrentOptions.binary[0],
				  CurrentOptions.binary[0]);
		lex_FloatAddRange(nBinaryID, CurrentOptions.binary[1],
				  CurrentOptions.binary[1]);
		lex_FloatAddSecondRange(nBinaryID, CurrentOptions.binary[0],
					CurrentOptions.binary[0]);
		lex_FloatAddSecondRange(nBinaryID, CurrentOptions.binary[1],
					CurrentOptions.binary[1]);
	}
}

void opt_Parse(char *s)
{
	struct sOptions newopt;

	newopt = CurrentOptions;

	switch (s[0]) {
	case 'g':
		if (strlen(&s[1]) == 4) {
			newopt.gbgfx[0] = s[1];
			newopt.gbgfx[1] = s[2];
			newopt.gbgfx[2] = s[3];
			newopt.gbgfx[3] = s[4];
		} else {
			errx(1, "Must specify exactly 4 characters for option 'g'");
		}
		break;
	case 'b':
		if (strlen(&s[1]) == 2) {
			newopt.binary[0] = s[1];
			newopt.binary[1] = s[2];
		} else {
			errx(1, "Must specify exactly 2 characters for option 'b'");
		}
		break;
	case 'z':
		if (strlen(&s[1]) <= 2) {
			int32_t result;
			unsigned int fillchar;

			result = sscanf(&s[1], "%x", &fillchar);
			if (!((result == EOF) || (result == 1)))
				errx(1, "Invalid argument for option 'z'");

			newopt.fillchar = fillchar;
		} else {
			errx(1, "Invalid argument for option 'z'");
		}
		break;
	default:
		fatalerror("Unknown option");
		break;
	}

	opt_SetCurrentOptions(&newopt);
}

void opt_Push(void)
{
	struct sOptionStackEntry *pOpt;

	pOpt = malloc(sizeof(struct sOptionStackEntry));

	if (pOpt == NULL)
		fatalerror("No memory for option stack");

	pOpt->Options = CurrentOptions;
	pOpt->pNext = pOptionStack;
	pOptionStack = pOpt;
}

void opt_Pop(void)
{
	if (pOptionStack == NULL)
		fatalerror("No entries in the option stack");

	struct sOptionStackEntry *pOpt;

	pOpt = pOptionStack;
	opt_SetCurrentOptions(&(pOpt->Options));
	pOptionStack = pOpt->pNext;
	free(pOpt);
}

void opt_AddDefine(char *s)
{
	char *value, *equals;

	if (cldefines_index >= cldefines_numindices) {
		/* Check for overflows */
		if ((cldefines_numindices * 2) < cldefines_numindices)
			fatalerror("No memory for command line defines");

		if ((cldefines_bufsize * 2) < cldefines_bufsize)
			fatalerror("No memory for command line defines");

		cldefines_numindices *= 2;
		cldefines_bufsize *= 2;

		cldefines = realloc(cldefines, cldefines_bufsize);
		if (!cldefines)
			fatalerror("No memory for command line defines");
	}
	equals = strchr(s, '=');
	if (equals) {
		*equals = '\0';
		value = equals + 1;
	} else {
		value = "1";
	}
	cldefines[cldefines_index++] = s;
	cldefines[cldefines_index++] = value;
}

static void opt_ParseDefines(void)
{
	uint32_t i;

	for (i = 0; i < cldefines_index; i += 2)
		sym_AddString(cldefines[i], cldefines[i + 1]);
}

/* Short options */
static char const *optstring = "b:D:Eg:hi:LM:o:p:r:VvW:w";

/*
 * Equivalent long options
 * Please keep in the same order as short opts
 *
 * Also, make sure long opts don't create ambiguity:
 * A long opt's name should start with the same letter as its short opt,
 * except if it doesn't create any ambiguity (`verbose` versus `version`).
 * This is because long opt matching, even to a single char, is prioritized
 * over short opt matching
 */
static struct option const longopts[] = {
	{ "binary-digits",    required_argument, NULL, 'b' },
	{ "define",           required_argument, NULL, 'D' },
	{ "export-all",       no_argument,       NULL, 'E' },
	{ "gfx-chars",        required_argument, NULL, 'g' },
	{ "halt-without-nop", no_argument,       NULL, 'h' },
	{ "include",          required_argument, NULL, 'i' },
	{ "preserve-ld",      no_argument,       NULL, 'L' },
	{ "dependfile",       required_argument, NULL, 'M' },
	{ "output",           required_argument, NULL, 'o' },
	{ "pad-value",        required_argument, NULL, 'p' },
	{ "recursion-depth",  required_argument, NULL, 'r' },
	{ "version",          no_argument,       NULL, 'V' },
	{ "verbose",          no_argument,       NULL, 'v' },
	{ "warning",          required_argument, NULL, 'W' },
	{ NULL,               no_argument,       NULL, 0   }
};

static void print_usage(void)
{
	fputs(
"usage: rgbasm [<options>] <file>...\n"
"    -b, --binary-digits <digits>   change the chars used for binary constants\n"
"    -D, --define <name>[=<value>]  add a string define (`EQUS')\n"
"    -E, --export-all               export all labels\n"
"    -g, --gfx-chars <chars>        change the chars used for gfx constants\n"
"    -h, --halt-without-nop         do not automatically add `nop' after `halt'\n"
"    -i, --include <path>           add the directory to the include search list\n"
"    -L, --preserve-ld              prevent auto-optimizing `ld' to `ldh'\n"
"    -M, --dependfile <path>        set the output dependency file\n"
"    -o, --output <path>            set the output object file\n"
"    -p, --pad-value <value>        set the value to use for `ds'\n"
"    -r, --recursion-depth <depth>  set max recursion depth before aborting\n"
"    -V, --version                  print RGBASM version and exit\n"
"    -v, --verbose                  report more information\n"
"    -W, --warning                  enable or disable warnings\n"
"    -w                             disable all warnings\n", stderr);
	exit(1);
}

int main(int argc, char *argv[])
{
	int ch;
	char *ep;

	struct sOptions newopt;

	char *tzMainfile;

	dependfile = NULL;

	/* Initial number of allocated elements in array */
	cldefines_numindices = 32;
	cldefines_bufsize = cldefines_numindices * cldefine_entrysize;
	cldefines = malloc(cldefines_bufsize);
	if (!cldefines)
		fatalerror("No memory for command line defines");

	/* yydebug=1; */

	nMaxRecursionDepth = 64;

	DefaultOptions.gbgfx[0] = '0';
	DefaultOptions.gbgfx[1] = '1';
	DefaultOptions.gbgfx[2] = '2';
	DefaultOptions.gbgfx[3] = '3';
	DefaultOptions.binary[0] = '0';
	DefaultOptions.binary[1] = '1';
	DefaultOptions.exportall = false;
	DefaultOptions.fillchar = 0;
	DefaultOptions.optimizeloads = true;
	DefaultOptions.haltnop = true;
	DefaultOptions.verbose = false;
	DefaultOptions.warnings = true;

	opt_SetCurrentOptions(&DefaultOptions);

	newopt = CurrentOptions;

	while ((ch = getopt_long_only(argc, argv, optstring, longopts,
				      NULL)) != -1) {
		switch (ch) {
		case 'b':
			if (strlen(optarg) == 2) {
				newopt.binary[0] = optarg[1];
				newopt.binary[1] = optarg[2];
			} else {
				errx(1, "Must specify exactly 2 characters for option 'b'");
			}
			break;
		case 'D':
			opt_AddDefine(optarg);
			break;
		case 'E':
			newopt.exportall = true;
			break;
		case 'g':
			if (strlen(optarg) == 4) {
				newopt.gbgfx[0] = optarg[1];
				newopt.gbgfx[1] = optarg[2];
				newopt.gbgfx[2] = optarg[3];
				newopt.gbgfx[3] = optarg[4];
			} else {
				errx(1, "Must specify exactly 4 characters for option 'g'");
			}
			break;
		case 'h':
			newopt.haltnop = false;
			break;
		case 'i':
			fstk_AddIncludePath(optarg);
			break;
		case 'L':
			newopt.optimizeloads = false;
			break;
		case 'M':
			dependfile = fopen(optarg, "w");
			if (dependfile == NULL)
				err(1, "Could not open dependfile %s", optarg);

			break;
		case 'o':
			out_SetFileName(optarg);
			break;
		case 'p':
			newopt.fillchar = strtoul(optarg, &ep, 0);

			if (optarg[0] == '\0' || *ep != '\0')
				errx(1, "Invalid argument for option 'p'");

			if (newopt.fillchar < 0 || newopt.fillchar > 0xFF)
				errx(1, "Argument for option 'p' must be between 0 and 0xFF");

			break;
		case 'r':
			nMaxRecursionDepth = strtoul(optarg, &ep, 0);

			if (optarg[0] == '\0' || *ep != '\0')
				errx(1, "Invalid argument for option 'r'");
			break;
		case 'V':
			printf("rgbasm %s\n", get_package_version_string());
			exit(0);
		case 'v':
			newopt.verbose = true;
			break;
		case 'W':
			processWarningFlag(optarg);
			break;
		case 'w':
			newopt.warnings = false;
			break;
		default:
			print_usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	opt_SetCurrentOptions(&newopt);

	DefaultOptions = CurrentOptions;

	if (argc == 0) {
		fputs("FATAL: no input files\n", stderr);
		print_usage();
	}

	tzMainfile = argv[argc - 1];

	setup_lexer();

	if (CurrentOptions.verbose)
		printf("Assembling %s\n", tzMainfile);

	if (dependfile) {
		if (!tzObjectname)
			errx(1, "Dependency files can only be created if an output object file is specified.\n");

		fprintf(dependfile, "%s: %s\n", tzObjectname, tzMainfile);
	}

	nStartClock = clock();

	nLineNo = 1;
	nTotalLines = 0;
	nIFDepth = 0;
	skipElif = true;
	nUnionDepth = 0;
	nPC = 0;
	sym_Init();
	sym_SetExportAll(CurrentOptions.exportall);
	fstk_Init(tzMainfile);
	opt_ParseDefines();
	charmap_InitMain();

	yy_set_state(LEX_STATE_NORMAL);
	opt_SetCurrentOptions(&DefaultOptions);

	if (yyparse() != 0 || nbErrors != 0)
		errx(1, "Assembly aborted (%ld errors)!", nbErrors);

	if (nIFDepth != 0)
		errx(1, "Unterminated IF construct (%ld levels)!", nIFDepth);

	if (nUnionDepth != 0) {
		errx(1, "Unterminated UNION construct (%ld levels)!",
		     nUnionDepth);
	}

	double timespent;

	nEndClock = clock();
	timespent = ((double)(nEndClock - nStartClock))
		     / (double)CLOCKS_PER_SEC;
	if (CurrentOptions.verbose) {
		printf("Success! %u lines in %d.%02d seconds ", nTotalLines,
		       (int)timespent, ((int)(timespent * 100.0)) % 100);
		if (timespent < FLT_MIN_EXP)
			printf("(INFINITY lines/minute)\n");
		else
			printf("(%d lines/minute)\n",
			       (int)(60 / timespent * nTotalLines));
	}

	out_CheckErrors();
	/* If no path specified, don't write file */
	if (tzObjectname != NULL)
		out_WriteObject();
	return 0;
}
