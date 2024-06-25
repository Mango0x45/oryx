#define _GNU_SOURCE
#include <assert.h>
#include <glob.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cbs.h"

#define TARGET "oryx"
#define GMPDIR "vendor/gmp-6.3.0"
#define OPDIR  "vendor/optparse-master"

enum {
	SIMD_AVX2 = 1 << 0,
	SIMD_NEON = 1 << 1,
	SIMD_SSE4_1 = 1 << 2,
};

static char *cflags_all[] = {
	("-I" GMPDIR),
	("-I" OPDIR),
	"-pipe",
	"-std=c11",
	"-Wall",
	"-Wextra",
	"-Wno-attributes",
	"-Wno-parentheses",
	"-Wno-pointer-sign",
	"-Wpedantic",
	"-Wvla",
#if __GLIBC__
	"-D_GNU_SOURCE",
#endif
};

static char *cflags_dbg[] = {
	"-DDEBUG=1",
	"-g3",
	"-ggdb3",
	"-O0",
};

static char *cflags_rls[] = {
	"-DNDEBUG=1",
	"-flto",
	"-march=native",
	"-mtune=native",
	"-O3",
};

static char *argv0;
static bool fflag, Fflag, rflag, Sflag;
static int simd_flags;

static void ld(void);
static void mkgmp(int);
static bool tagvalid(const char *);
static void chk_cpu_flags(void);
static int globerr(const char *, int);
static tjob cc, cc_test, gperf;

static void
usage(void)
{
	fprintf(stderr,
	        "Usage: %s [-frS]\n"
	        "       %s clean | distclean | test\n",
	        argv0, argv0);
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	cbsinit(argc, argv);
	rebuild();

	argv0 = argv[0];

	int opt;
	while ((opt = getopt(argc, argv, "fFrS")) != -1) {
		switch (opt) {
		case 'F':
			Fflag = true;
			/* fallthrough */
		case 'f':
			fflag = true;
			break;
		case 'r':
			rflag = true;
			break;
		case 'S':
			Sflag = true;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	struct strs cmd = {0};

	if (argc > 0) {
		if (strcmp("clean", *argv) == 0) {
			strspushl(&cmd, "find", ".",
				"(",
					"-name", TARGET,
					"-or", "-name", "*.o",
					"-or", "-name", "*.gen.c",
					"-or", "-path", "./test/*", "-and", "-executable",
				")", "-delete"
			);
			cmdput(cmd);
			return cmdexec(cmd);
		} else if (strcmp("distclean", *argv) == 0) {
			strspushl(&cmd, "find", ".",
				"(",
					"-name", TARGET,
					"-or", "-name", "*.o",
					"-or", "-name", "*.gen.c",
					"-or", "-path", "./test/*", "-and", "-executable",
				")",  "-delete"
			);
			cmdput(cmd);
			cmdexec(cmd);
			assert(chdir(GMPDIR) != -1);
			strszero(&cmd);
			strspushl(&cmd, "make", "distclean");
			cmdput(cmd);
			return cmdexec(cmd);
		} else if (strcmp("test", *argv) == 0) {
			strspushl(&cmd, "find", "test", "-type", "f", "-executable",
			          "-exec", "{}", ";");
			cmdput(cmd);
			return cmdexec(cmd);
		} else {
			fprintf(stderr, "%s: invalid subcommand — ‘%s’\n", argv0, *argv);
			usage();
		}
	}

	chk_cpu_flags();

	int procs = nproc();
	if (procs == -1)
		procs = 8;

	mkgmp(procs);

	tpool tp;
	tpinit(&tp, procs);

	glob_t g;

	/* GNU Perf files */
	assert(glob("src/*.gperf", 0, globerr, &g) == 0);
	for (size_t i = 0; i < g.gl_pathc; i++)
		tpenq(&tp, gperf, g.gl_pathv[i], NULL);
	tpwait(&tp);

	/* C files */
	globfree(&g);
	assert(glob("src/*.c", 0, globerr, &g) == 0);
	for (size_t i = 0; i < g.gl_pathc; i++)
		tpenq(&tp, cc, g.gl_pathv[i], NULL);
	tpwait(&tp);

	/* Tests */
	globfree(&g);
	assert(glob("test/*.c", 0, globerr, &g) == 0);
	for (size_t i = 0; i < g.gl_pathc; i++)
		tpenq(&tp, cc_test, g.gl_pathv[i], NULL);
	tpwait(&tp);

	tpfree(&tp);

	ld();

	globfree(&g);
	strsfree(&cmd);
	return EXIT_SUCCESS;
}

void
cc(void *arg)
{
	if (!tagvalid(arg))
		return;

	struct strs cmd = {0};
	char *dst = swpext(arg, "o"), *src = arg;

	if (!fflag && fmdnewer(dst, src))
		goto out;

	strspushenvl(&cmd, "CC", "cc");
	strspush(&cmd, cflags_all, lengthof(cflags_all));
	if (rflag)
		strspushenv(&cmd, "CFLAGS", cflags_rls, lengthof(cflags_rls));
	else
		strspushenv(&cmd, "CFLAGS", cflags_dbg, lengthof(cflags_dbg));
	if (!rflag && !Sflag)
		strspushl(&cmd, "-fsanitize=address,undefined");
	if (simd_flags != 0)
		strspushl(&cmd, "-DORYX_SIMD=1");
	llvmquery(&cmd, LLVM_CFLAGS);
	strspushl(&cmd, "-o", dst, "-c", src);

	cmdput(cmd);
	cmdexec(cmd);
	strsfree(&cmd);
out:
	free(dst);
}

void
cc_test(void *arg)
{
	struct strs cmd = {0};
	char *dst, *src = arg;

	assert((dst = strdup(src)) != NULL);
	*strchr(dst, '.') = 0;

	struct deps {
		char  *tst;
		char **objs;
		size_t len;
	} deps[] = {
		{"test/arena", (char *[]){"src/arena.o"}, 1},
	};

	struct deps d;
	for (size_t i = 0; i < lengthof(deps); i++) {
		if (strcmp(deps[i].tst, dst) == 0) {
			d = deps[i];
			break;
		}
	}

	if (!fflag && !foutdated(dst, d.objs, d.len) && !foutdatedl(dst, src))
		goto out;

	strspushenvl(&cmd, "CC", "cc");
	strspush(&cmd, cflags_all, lengthof(cflags_all));
	if (rflag)
		strspushenv(&cmd, "CFLAGS", cflags_rls, lengthof(cflags_rls));
	else
		strspushenv(&cmd, "CFLAGS", cflags_dbg, lengthof(cflags_dbg));
	if (!rflag && !Sflag)
		strspushl(&cmd, "-fsanitize=address,undefined");
	if (simd_flags != 0)
		strspushl(&cmd, "-DORYX_SIMD=1");
	strspushl(&cmd, "-Isrc", "-o", dst, src);
	strspush(&cmd, d.objs, d.len);

	cmdput(cmd);
	cmdexec(cmd);
	strsfree(&cmd);
out:
	free(dst);
}

void
gperf(void *arg)
{
	struct strs cmd = {0};
	char *dst = swpext(arg, "gen.c"), *src = arg;

	if (!fflag && fmdnewer(dst, src))
		goto out;

	strspushl(&cmd, "gperf", src, "--output-file", dst);

	cmdput(cmd);
	cmdexec(cmd);
	strsfree(&cmd);
out:
	free(dst);
}

void
ld(void)
{
	glob_t g;
	bool dobuild = fflag;
	struct strs cmd = {0};

	strspushenvl(&cmd, "CC", "cc");
	strspush(&cmd, cflags_all, lengthof(cflags_all));
	if (rflag)
		strspushenv(&cmd, "CFLAGS", cflags_rls, lengthof(cflags_rls));
	else
		strspushenv(&cmd, "CFLAGS", cflags_dbg, lengthof(cflags_dbg));
	if (!rflag && !Sflag)
		strspushl(&cmd, "-fsanitize=address,undefined");
	llvmquery(&cmd, LLVM_LDFLAGS | LLVM_LIBS);
	strspushl(&cmd, "-lm", "-o", TARGET);

	assert(glob("src/*.o", 0, globerr, &g) == 0);
	for (size_t i = 0; i < g.gl_pathc; i++) {
		if (!tagvalid(g.gl_pathv[i]))
			continue;
		if (fmdolder(TARGET, g.gl_pathv[i]))
			dobuild = true;
		strspushl(&cmd, g.gl_pathv[i]);
	}

	/* Important!  GCC wants this last? */
	strspushl(&cmd, GMPDIR "/.libs/libgmp.a");

	if (dobuild) {
		cmdput(cmd);
		cmdexec(cmd);
	}

	globfree(&g);
	strsfree(&cmd);
}

void
mkgmp(int nprocs)
{
	int ret;
	/* Fork a child process so that we don’t mess up the parent’s
	   working directory */
	pid_t pid = fork();
	assert(pid != -1);

	if (pid == 0) {
		struct strs cmd = {0};
		assert(chdir(GMPDIR) != -1);

		if (!Fflag && fexists("./.libs/libgmp.a"))
			exit(EXIT_SUCCESS);

		strspushl(&cmd, "./configure", "--disable-shared");
		cmdput(cmd);
		if ((ret = cmdexec(cmd)) != EXIT_SUCCESS)
			exit(ret);
		strszero(&cmd);

		if (Fflag) {
			strspushl(&cmd, "make", "distclean");
			cmdput(cmd);
			if ((ret = cmdexec(cmd)) != EXIT_SUCCESS)
				exit(ret);
			strszero(&cmd);
		}

		char flags[64];
		sprintf(flags, "-j%d", nprocs);
		assert(setenv("MAKEFLAGS", flags, true) != -1);
		strspushl(&cmd, "make");
		cmdput(cmd);
		exit(cmdexec(cmd));
	} else if ((ret = cmdwait(pid)) != EXIT_SUCCESS)
		exit(ret);
}

bool
tagvalid(const char *file)
{
	/* No tag */
	if (strchr(file, '-') == NULL)
		return true;

	char *want_and_have = NULL;
	static struct pair {
		char *tag;
		int flag;
	} tags[] = {
		{"avx2",    SIMD_AVX2  },
		{"sse4_1",  SIMD_SSE4_1},
		{"neon",    SIMD_NEON  },
		{"generic", 0          },
	};

	size_t len = strlen(file);
	char *buf = malloc(len + sizeof("generic"));
	assert(buf != NULL);
	for (size_t i = 0; i < lengthof(tags); i++) {
		char *sep = strrchr(file, '-');
		char *ext = strrchr(file, '.');
		assert(sep != NULL);
		assert(ext != NULL);
		sprintf(buf, "%.*s-%s%s", (int)(sep - file), file, tags[i].tag, ext);

		if (fexists(buf)
		    && ((simd_flags & tags[i].flag) != 0 || tags[i].flag == 0))
		{
			want_and_have = buf;
			break;
		}
	}

	bool ret;
	if (want_and_have == NULL)
		ret = false;
	else
		ret = strcmp(want_and_have, file) == 0;

	free(buf);
	return ret;
}

void
chk_cpu_flags(void)
{
	uint32_t exx;
	(void)exx; /* Might be unused */

	if (!rflag)
		return;

	/* Test for AVX512 */
#if __AVX512F__
	simd_flags |= SIMD_AVX2;
#elif __GNUC__ && __x86_64__
	asm volatile("cpuid" : "=b"(exx) : "a"(7), "c"(0));
	if (exx & (1 << 5))
		simd_flags |= SIMD_AVX2;
#endif

	/* Test for SSE4.1 */
#if __SSE4_1__
	simd_flags |= SIMD_SSE4_1;
#elif __GNUC__ && __x86_64__
	asm volatile("cpuid" : "=c"(exx) : "a"(1), "c"(0));
	if (exx & (1 << 19))
		simd_flags |= SIMD_SSE4_1;
#endif

	/* Test for NEON */
#if __ARM_NEON || __ARM_NEON__
	simd_flags |= SIMD_NEON;
#endif
}

int
globerr(const char *s, int e)
{
	fprintf(stderr, "glob: %s: %s\n", s, strerror(e));
	exit(EXIT_FAILURE);
}
