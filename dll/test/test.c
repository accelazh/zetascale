//----------------------------------------------------------------------------
// ZetaScale
// Copyright (c) 2016, SanDisk Corp. and/or all its affiliates.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published by the Free
// Software Foundation;
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License v2.1 for more details.
//
// A copy of the GNU Lesser General Public License v2.1 is provided with this package and
// can also be found at: http://opensource.org/licenses/LGPL-2.1
// You should have received a copy of the GNU Lesser General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 59 Temple
// Place, Suite 330, Boston, MA 02111-1307 USA.
//----------------------------------------------------------------------------

/*
 * Run various ZS tests.
 *
 * Author: Johann George
 * Copyright (c) 2012-2013, Sandisk Corporation.  All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "tlib.h"


/*
 * Configurable parameters.
 */
#define BUF_SIZE            256
#define ZS_LIB             "ZS_LIB"
#define ZS_PROPERTY_FILE   "ZS_PROPERTY_FILE"


/*
 * Macro functions.
 */
#define nel(a) (sizeof(a)/sizeof(*(a)))


/*
 * Types.
 */
typedef void (tfunc_t)(zs_t *);


/*
 * The various options for persistent storage.
 *  NONE - Don't set any options
 *  AUTO - Decide if we use SLOW or FAST based on memory availability
 *  SLOW - Use /tmp as our perisistent store
 *  FAST - Use memory as our persistent store
 */
typedef enum {
    NONE,
    AUTO,
    SLOW,
    FAST
} speed_t;


/*
 * Tests information structure.
 */
typedef struct tinfo {
    int           nlen;
    struct tinfo *next;
    char         *name;
    char         *desc;
    char         *help;
    tfunc_t      *func;
    clint_t      *clint;
} tinfo_t;


/*
 * Static variables.
 */
static char    *Level;
static tinfo_t *Test;
static tinfo_t *Tinfo;
static speed_t  Speed = AUTO;


/*
 * Global variables.
 */
int Verbose;


/*
 * Command line options for tests that don't take any arguments.
 */
static void do_arg_null(char *arg, char **args, clint_t **clint);
static clint_t Clint_null ={ do_arg_null, NULL, {{} } };


/*
 * Command line options.
 */
static void do_arg(char *arg, char **args, clint_t **clint);
static clint_t Clint ={
    do_arg, NULL, {
        { "-h",        0 },
        { "--help",    0 },
        { "-l",        1 },
        { "--level",   1 },
        { "-s",        1 },
        { "--speed",   1 },
        { "-v",        0 },
        { "--verbose", 0 },
        {},
    }
};


/*
 * Log levels.
 */
char *Levels[] ={
     "none",
     "fatal",
     "error",
     "warning",
     "info",
     "diagnostic",
     "debug",
     "trace",
     "trace_low",
     "devel",
};


/*
 * Set the log level.
 */
static void
set_level(zs_t *zs)
{
    if (Level)
        zs_set_prop(zs, "ZS_LOG_LEVEL", Level);
}


/*
 * Determine whether we have enough memory for it to substitute as flash.
 */
static speed_t
get_speed(zs_t *zs)
{
    unsigned long flash;
    if (!zs_utoi(zs_get_prop(zs, "ZS_FLASH_SIZE", "0"), &flash))
        return SLOW;

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp)
        return SLOW;

    char buf[64];
    char *line = fgets(buf, sizeof(buf), fp);
    fclose(fp);

    if (!line)
        return SLOW;
    if (strncmp(line, "MemTotal:", 9) != 0)
        return SLOW;
    unsigned long mem = atol(&line[9]) * 1024;

    if (mem - 2L*1024*1024*1024 < flash)
        return SLOW;
    return FAST;
}


/*
 * Set the properties relating to the speed.
 */
static void
set_speed(zs_t *zs)
{
    if (Speed == AUTO)
        Speed = get_speed(zs);

    if (Speed == SLOW)
        zs_set_prop(zs, "ZS_FLASH_FILENAME", "/tmp/zs_disk%d");
    else if (Speed == FAST) {
        zs_set_prop(zs, "ZS_O_DIRECT",       "0");
        zs_set_prop(zs, "ZS_TEST_MODE",      "1");
        zs_set_prop(zs, "ZS_LOG_FLUSH_DIR",  "/dev/shm");
        zs_set_prop(zs, "ZS_FLASH_FILENAME", "/dev/shm/zs_disk%d");
    }
}


/*
 * Initialize the property file.
 */
static void
init_prop_file(zs_t *zs, char *name)
{
    char *err;
    char buf[BUF_SIZE];
    struct stat sbuf;
    char *prop = getenv(ZS_PROPERTY_FILE);

    if (prop)
        unsetenv(ZS_PROPERTY_FILE);
    else {
        snprintf(buf, sizeof(buf), "./%s.prop", name);
        if (stat(buf, &sbuf) < 0) {
            snprintf(buf, sizeof(buf), "./test.prop");
            if (stat(buf, &sbuf) < 0)
                return;
        }
        prop = buf;
    }

    printv("using property file: %s", prop);
    if (!zs_load_prop_file(zs, prop, &err))
        die_err(err, "zs_load_prop_file failed");
}


/*
 * Initialize the test framework.  We are passed the test name.
 */
void
test_init(zs_t *zs, char *name)
{
    char *err;

    init_prop_file(zs, name);
    set_speed(zs);
    set_level(zs);

    if (!zs_start(zs, &err))
        die_err(err, "zs_start failed");
}


/*
 * Initialize properties.
 */
static void
set_def_props(zs_t *zs)
{
    zs_set_prop(zs, "ZS_LOG_LEVEL",  "warning");
    zs_set_prop(zs, "HUSH_FASTCC",    "1");
    zs_set_prop(zs, "ZS_REFORMAT",   "1");
    zs_set_prop(zs, "ZS_FLASH_SIZE", "4G");
}


/*
 * Initialize the ZS library.
 */
static void
init_zs_lib()
{
    int n;
    char **l;
    struct stat sbuf;
    char *lib = getenv(ZS_LIB);
    char *libs[] ={
        "../../output/lib/libzs.so",
        "/tmp/libzs.so"
    };

    if (lib)
        return;
    for (n = nel(libs), l = libs; n--; l++) {
        if (stat(*l, &sbuf) < 0)
            continue;
        setenv(ZS_LIB, *l, 0);
        printv("using ZS library: %s", *l);
        return;
    }
    die("cannot determine ZS library; set ZS_LIB");
}


/*
 * Run a particular test.
 */
static void
run_test(tinfo_t *tinfo)
{
    char *err;

    zs_t *zs = zs_init(0, &err);
    if (!zs)
        die_err(err, "zs_init failed");

    init_zs_lib();
    set_def_props(zs);
    tinfo->func(zs);
    zs_done(zs);
}


/*
 * Add a test.
 */
void
test_info(char *name, char *desc, char *help, clint_t *clint, tfunc_t *func)
{
    tinfo_t *tinfo = malloc_q(sizeof(tinfo_t));
    tinfo->nlen  = strlen(name);
    tinfo->name  = name;
    tinfo->desc  = desc;
    tinfo->help  = help;
    tinfo->clint = clint;
    tinfo->func  = func;
    tinfo->next  = Tinfo;
    Tinfo = tinfo;
}


/*
 * Find a test.
 */
static tinfo_t *
tfind(char *name)
{
    tinfo_t *tinfo;

    for (tinfo = Tinfo; tinfo; tinfo = tinfo->next)
        if (streq(name, tinfo->name))
            return tinfo;
    return NULL;
}


/*
 * Compare function for qsorting tinfo entries.
 */
static int
compare(const void *a1, const void *a2)
{
    const tinfo_t *t1 = *((tinfo_t **) a1);
    const tinfo_t *t2 = *((tinfo_t **) a2);

    if (t1->nlen < t2->nlen)
        return -1;
    if (t1->nlen > t2->nlen)
        return 1;
    return strcmp(t1->name, t2->name);
}


/*
 * Print out a usage message and exit.
 */
static void
usage(void)
{
    int i;
    tinfo_t *tinfo;
    const char *str[] = {
        "Usage",
        "    test Options Test",
        "Options",
        "    -h|--help",
        "       Print this message.",
        "    -l|--level S",
        "        Set level to S: none, error, warn, info, diag, debug, trace,",
        "        trace_low, devel.",
        "    -s|--speed S",
        "       Set speed to S: none, auto, slow, fast",
        "    -v|--verbose",
        "       Turn on verbose mode.",
        "Test",
    };

    for (i = 0; i < sizeof(str)/sizeof(*str); i++)
        printf("%s\n", str[i]);

    int num = 0;
    int max = 0;
    for (tinfo = Tinfo; tinfo; tinfo = tinfo->next) {
        num++;
        if (tinfo->nlen > max)
            max = tinfo->nlen;
    }

    tinfo_t **sorted = malloc_q(num * sizeof(tinfo_t *));
    for (i = 0, tinfo = Tinfo; tinfo; tinfo = tinfo->next)
        sorted[i++] = tinfo;

    qsort(sorted, num, sizeof(tinfo_t *), compare);
    for (i = 0; i < num; i++) {
        tinfo = sorted[i];
        printf("    %-*s - %s\n", max, tinfo->name, tinfo->desc);
    }
    exit(0);
}


/*
 * Handle an argument for tests that don't take any arguments.
 */
static void
do_arg_null(char *arg, char **args, clint_t **clint)
{
    if (arg[0] == '-')
        die("bad option: %s", arg);
    else
        die("bad argument: %s", arg);
}


/*
 * Select a test.
 */
static void
arg_test(char *arg, clint_t **clintp)
{
    Test = tfind(arg);
    if (!Test)
        die("unknown test: %s", arg);

    clint_t *clint = Test->clint;
    *clintp = clint ? clint : &Clint_null;
}


/*
 * Set the log level.
 */
static void
arg_level(char *s)
{
    int i;
    int n = strlen(s);

    for (i = 0; i < nel(Levels); i++) {
        if (strncmp(s, Levels[i], n) != 0)
            continue;
        Level = Levels[i];
        return;
    }
    die("bad log level: %s", s);
}


/*
 * Set the speed.
 */
static void
arg_speed(char *s)
{
    int n = strlen(s);

    if (strncmp(s, "none", n) == 0)
        Speed = NONE;
    else if (strncmp(s, "auto", n) == 0)
        Speed = AUTO;
    else if (strncmp(s, "slow", n) == 0)
        Speed = SLOW;
    else if (strncmp(s, "fast", n) == 0)
        Speed = FAST;
    else
        die("bad speed: %s", s);
}


/*
 * Handle an argument.
 */
static void
do_arg(char *arg, char **args, clint_t **clint)
{
    if (streq(arg, "-h") || streq(arg, "--help"))
        usage();
    else if (streq(arg, "-l") || streq(arg, "--level"))
        arg_level(args[0]);
    else if (streq(arg, "-s") || streq(arg, "--speed"))
        arg_speed(args[0]);
    else if (streq(arg, "-v") || streq(arg, "--verbose"))
        Verbose = 1;
    else if (arg[0] == '-')
        die("bad option: %s", arg);
    else
        arg_test(arg, clint);
}


/*
 * Parse arguments.
 */
static void
parse(int argc, char *argv[], clint_t *clint)
{
    int ai;
    struct clopt *opt;

    for (ai = 1; ai < argc; ai++) {
        char *arg = argv[ai];
        if (arg[0] != '-')
            clint->do_arg(arg, NULL, &clint);
        else if (arg[1] != '-') {
            char *p;
            for (p = &arg[1]; *p; p++) {
                int c = *p;
                for (opt = clint->opts; opt->str; opt++) {
                    char *str = opt->str;
                    if (str[0] == '-' && str[1] == c && str[2] == '\0')
                        break;
                }
                if (!opt->str || opt->req == 0) {
                    char str[3] = {'-', c, '\0'};
                    clint->do_arg(str, NULL, &clint);
                } else if (opt->req == 1 && p[1] != '\0') {
                    char oname[3] = {'-', c, '\0'};
                    char *oargs[] = {&p[1]};
                    clint->do_arg(oname, oargs, &clint);
                    break;
                } else {
                    char oname[3] = {'-', c, '\0'};
                    if (ai + opt->req >= argc)
                        die("-%c requires %d argument(s)", c, opt->req);
                    clint->do_arg(oname, &argv[ai+1], &clint);
                    ai += opt->req;
                }
            }
        } else {
            for (opt = clint->opts; opt->str; opt++)
                if (streq(opt->str, arg))
                    break;
            if (!opt->str || opt->req == 0)
                clint->do_arg(arg, NULL, &clint);
            else if (ai + opt->req >= argc) {
                    die("%s requires %d argument(s)", arg, opt->req);
                clint->do_arg(arg, &argv[ai+1], &clint);
                ai += opt->req;
            }
        }
    }
}


int
main(int argc, char *argv[])
{
    if (argc < 2)
        usage();
    parse(argc, argv, &Clint);

    if (!Test)
        die("must specify test");
    run_test(Test);
    return 0;
}
