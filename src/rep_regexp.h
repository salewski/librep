/*
 * Definitions etc. for regexp(3) routines.
 *
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */

#ifndef REGEXP_H
#define REGEXP_H

#define NSUBEXP  10

typedef enum regtype {
    reg_string = 0,
#ifdef JADE
    reg_tx
#endif
} regtype;

typedef union regsubs {
    struct {
	char *startp[NSUBEXP];
	char *endp[NSUBEXP];
    } string;
#ifdef JADE
    struct {
	VALUE startp[NSUBEXP];
	VALUE endp[NSUBEXP];
    } tx;
#endif
} regsubs;

typedef struct regexp {
	regtype lasttype;
	regsubs matches;

	char regstart;		/* Internal use only. */
	char reganch;		/* Internal use only. */
	char *regmust;		/* Internal use only. */
	int regmlen;		/* Internal use only. */
	int regsize;		/* actual size of regexp structure */
	char program[1];	/* Unwarranted chumminess with compiler. */
} regexp;

#ifdef JADE
/* Data structure used to save and restore regexp data internally */
struct saved_regexp_data {
    struct saved_regexp_data *next;
    regtype type;
    VALUE data;
    regsubs matches;
};
#endif

/* eflags for regexec2() */
#define REG_NOTBOL 1		/* start of input isn't start of line */
#define REG_NOCASE 2		/* fold upper and lower case */
#define REG_1LINE  4		/* for regexec_tx: only search to the
				   end of the line for the start of the
				   match. */

#define regexec(p,s) regexec2(p,s,0)

extern regexp *regcomp(char *);
extern int regexec2(regexp *, char *, int);
extern int regmatch_string(regexp *, char *, int);
extern void regsub(int, regsubs *, char *, char *, void *);
extern int regsublen(int, regsubs *, char *, void *);
extern void regerror(char *);

#ifdef JADE
extern int regexec_tx(regexp *prog, TX *tx, VALUE start, int eflags);
extern int regexec_reverse_tx(regexp *prog, TX *tx, VALUE start, int eflags);
extern int regmatch_tx(regexp *prog, TX *tx, VALUE start, int eflags);
#endif

/* My Amiga's C library calls str[n]casecmp() str[n]icmp()  */
#ifdef AMIGA
# ifndef strcasecmp
#  define strcasecmp stricmp
# endif
# ifndef strncasecmp
#  define strncasecmp strnicmp
# endif
#endif

#endif /* REGEXP_H */
