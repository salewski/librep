/* values.c -- Handling of Lisp data (includes garbage collection)
   Copyright (C) 1993, 1994 John Harper <john@dcs.warwick.ac.uk>
   $Id$

   This file is part of Jade.

   Jade is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   Jade is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Jade; see the file COPYING.	If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#define _GNU_SOURCE

#include "repint.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifdef NEED_MEMORY_H
# include <memory.h>
#endif

/* #define GC_MONITOR_STK */

/* #define DEBUG_GC */

#ifdef DEBUG_GC
static int debug_gc = 0;
#endif


/* Type handling */

#define TYPE_HASH_SIZE 32
#define TYPE_HASH(type) (((type) >> 1) & (TYPE_HASH_SIZE-1))

static u_int next_free_type = 0;
static rep_type *data_types[TYPE_HASH_SIZE];

void
rep_register_type(u_int code, char *name,
		  int (*compare)(repv, repv),
		  void (*princ)(repv, repv),
		  void (*print)(repv, repv),
		  void (*sweep)(void),
		  void (*mark)(repv),
		  void (*mark_type)(void),
		  int (*getc)(repv),
		  int (*ungetc)(repv, int),
		  int (*putc)(repv, int),
		  int (*puts)(repv, void *, int, rep_bool),
		  repv (*bind)(repv),
		  void (*unbind)(repv))
{
    rep_type *t = rep_alloc(sizeof(rep_type));
    if (t == 0)
    {
	rep_mem_error();
	return;
    }
    t->code = code;
    t->name = name;
    t->compare = compare;
    t->princ = princ;
    t->print = print;
    t->sweep = sweep;
    t->mark = mark;
    t->mark_type = mark_type;
    t->getc = getc;
    t->ungetc = ungetc;
    t->putc = putc;
    t->puts = puts;
    t->bind = bind;
    t->unbind = unbind;
    t->next = data_types[TYPE_HASH(code)];
    data_types[TYPE_HASH(code)] = t;
}

u_int
rep_register_new_type(char *name,
		      int (*compare)(repv, repv),
		      void (*princ)(repv, repv),
		      void (*print)(repv, repv),
		      void (*sweep)(void),
		      void (*mark)(repv),
		      void (*mark_type)(void),
		      int (*getc)(repv),
		      int (*ungetc)(repv, int),
		      int (*putc)(repv, int),
		      int (*puts)(repv, void *, int, rep_bool),
		      repv (*bind)(repv),
		      void (*unbind)(repv))
{
    u_int code;
    assert(next_free_type != 256);
    code = (next_free_type++ << rep_CELL16_TYPE_SHIFT) | rep_CELL_IS_8 | rep_CELL_IS_16;
    rep_register_type(code, name, compare, princ, print,
		  sweep, mark, mark_type,
		  getc, ungetc, putc, puts, bind, unbind);
    return code;
}

rep_type *
rep_get_data_type(u_int code)
{
    rep_type *t = data_types[TYPE_HASH(code)];
    while (t != 0 && t->code != code)
	t = t->next;
    assert (t != 0);
    return t;
}


/* General object handling */

/* Returns zero if V1 == V2, less than zero if V1 < V2, and greater than
   zero otherwise. */
int
rep_value_cmp(repv v1, repv v2)
{
    if(v1 != rep_NULL && v2 != rep_NULL)
    {
	rep_type *t1 = rep_get_data_type(rep_TYPE(v1));
	if (t1 != 0)
	    return (v1 == v2) ? 0 : t1->compare(v1, v2);
	else
	    return (v1 == v2) ? 0 : 1;
    }
    return 1;
}

void
rep_princ_val(repv strm, repv val)
{
    if(val != rep_NULL)
    {
	rep_type *t = rep_get_data_type(rep_TYPE(val));
	rep_GC_root gc_strm, gc_val;
	rep_PUSHGC(gc_strm, strm);
	rep_PUSHGC(gc_val, val);
	t->princ(strm, val);
	rep_POPGC; rep_POPGC;
    }
}

void
rep_print_val(repv strm, repv val)
{
    if(val != rep_NULL)
    {
	rep_type *t = rep_get_data_type(rep_TYPE(val));
	rep_GC_root gc_strm, gc_val;
	rep_PUSHGC(gc_strm, strm);
	rep_PUSHGC(gc_val, val);
	t->print(strm, val);
	rep_POPGC; rep_POPGC;
    }
}

int
rep_type_cmp(repv val1, repv val2)
{
    return !(rep_TYPE(val1) == rep_TYPE(val2));
}


/* Strings */

static rep_string *strings;
static int allocated_strings, allocated_string_bytes;

DEFSTRING(null_string_const, "");

repv
rep_null_string(void)
{
    return rep_VAL(&null_string_const);
}

DEFSTRING(string_overflow, "String too long");

/* Return a string object with room for exactly LEN characters. No extra
   byte is allocated for a zero terminator; do this manually if required. */
repv
rep_make_string(long len)
{
    rep_string *str;
    int memlen;

    if(len > rep_MAX_STRING)
	return Fsignal(Qerror, rep_LIST_1(rep_VAL(&string_overflow)));

    memlen = rep_DSTRING_SIZE(len);
    str = rep_ALLOC_CELL(memlen);
    if(str != NULL)
    {
	str->car = rep_MAKE_STRING_CAR(len - 1);
	str->next = strings;
	str->data = ((char *)str) + sizeof (rep_string);
	strings = str;
	allocated_strings++;
	allocated_string_bytes += memlen;
	rep_data_after_gc += memlen;
	return rep_VAL(str);
    }
    else
	return rep_NULL;
}

repv
rep_string_dupn(const u_char *src, long slen)
{
    rep_string *dst = rep_STRING(rep_make_string(slen + 1));
    if(dst != NULL)
    {
	memcpy(rep_STR(dst), src, slen);
	rep_STR(dst)[slen] = 0;
    }
    return rep_VAL(dst);
}

repv
rep_string_dup(const u_char *src)
{
    return rep_string_dupn(src, strlen(src));
}

repv
rep_concat2(u_char *s1, u_char *s2)
{
    int len = strlen(s1) + strlen(s2);
    repv res = rep_make_string(len + 1);
    stpcpy(stpcpy(rep_STR(res), s1), s2);
    return(res);
}

repv
rep_concat3(u_char *s1, u_char *s2, u_char *s3)
{
    int len = strlen(s1) + strlen(s2) + strlen(s3);
    repv res = rep_make_string(len + 1);
    stpcpy(stpcpy(stpcpy(rep_STR(res), s1), s2), s3);
    return(res);
}

repv
rep_concat4(u_char *s1, u_char *s2, u_char *s3, u_char *s4)
{
    int len = strlen(s1) + strlen(s2) + strlen(s3) + strlen(s4);
    repv res = rep_make_string(len + 1);
    stpcpy(stpcpy(stpcpy(stpcpy(rep_STR(res), s1), s2), s3), s4);
    return(res);
}

static int
string_cmp(repv v1, repv v2)
{
    if(rep_STRINGP(v1) && rep_STRINGP(v2))
    {
	long len1 = rep_STRING_LEN(v1);
	long len2 = rep_STRING_LEN(v2);
	long tem = memcmp(rep_STR(v1), rep_STR(v2), MIN(len1, len2));
	return tem != 0 ? tem : (len1 - len2);
    }
    else
	return 1;
}

static void
string_sweep(void)
{
    rep_string *x = strings;
    strings = 0;
    allocated_strings = 0;
    allocated_string_bytes = 0;
    while(x != 0)
    {
	rep_string *next = x->next;
	if(rep_GC_CELL_MARKEDP(rep_VAL(x)))
	{
	    rep_GC_CLR_CELL(rep_VAL(x));
	    x->next = strings;
	    strings = x;
	    allocated_strings++;
	    allocated_string_bytes += rep_DSTRING_SIZE(rep_STRING_LEN(rep_VAL(x)));
	}
	else
	    rep_free(x);
	x = next;
    }
}

/* Sets the length-field of the dynamic string STR to LEN. */
rep_bool
rep_set_string_len(repv str, long len)
{
    if(rep_STRING_WRITABLE_P(str))
    {
	rep_STRING(str)->car = rep_MAKE_STRING_CAR(len);
	return rep_TRUE;
    }
    else
	return rep_FALSE;
}


/* Misc */

static int
number_cmp(repv v1, repv v2)
{
    if(rep_TYPE(v1) == rep_TYPE(v2))
	return rep_INT(v1) - rep_INT(v2);
    else
	return 1;
}

int
rep_ptr_cmp(repv v1, repv v2)
{
    if(rep_TYPE(v1) == rep_TYPE(v2))
	return !(rep_PTR(v1) == rep_PTR(v2));
    else
	return 1;
}


/* Cons */

static rep_cons_block *cons_block_chain;
static rep_cons *cons_freelist;
static int allocated_cons, used_cons;

DEFUN("cons", Fcons, Scons, (repv car, repv cdr), rep_Subr2) /*
::doc:Scons::
cons CAR CDR

Returns a new cons-cell with car CAR and cdr CDR.
::end:: */
{
    rep_cons *cn;
    cn = cons_freelist;
    if(cn == NULL)
    {
	rep_cons_block *cb;
	cb = rep_alloc(sizeof(rep_cons_block));
	if(cb != NULL)
	{
	    int i;
	    allocated_cons += rep_CONSBLK_SIZE;
	    cb->next = cons_block_chain;
	    cons_block_chain = cb;
	    for(i = 0; i < (rep_CONSBLK_SIZE - 1); i++)
		cb->cons[i].cdr = rep_CONS_VAL(&cb->cons[i + 1]);
	    cb->cons[i].cdr = 0;
	    cons_freelist = cb->cons;
	}
	else
	    return rep_mem_error();
	cn = cons_freelist;
    }
    cons_freelist = rep_CONS(cn->cdr);
    cn->car = car;
    cn->cdr = cdr;
    used_cons++;
    rep_data_after_gc += sizeof(rep_cons);
    return rep_CONS_VAL(cn);
}

void
rep_cons_free(repv cn)
{
    rep_CDR(cn) = rep_CONS_VAL(cons_freelist);
    cons_freelist = rep_CONS(cn);
    used_cons--;
}

static void
cons_sweep(void)
{
    rep_cons_block *cb = cons_block_chain;
    cons_block_chain = NULL;
    cons_freelist = NULL;
    used_cons = 0;
    while(cb != NULL)
    {
	rep_cons_block *nxt = cb->next;
	rep_cons *newfree = NULL, *newfreetail = NULL, *this;
	int i, newused = 0;
	for(i = 0, this = cb->cons; i < rep_CONSBLK_SIZE; i++, this++)
	{
	    if(!rep_GC_CONS_MARKEDP(rep_CONS_VAL(this)))
	    {
		if(!newfreetail)
		    newfreetail = this;
		this->cdr = rep_CONS_VAL(newfree);
		newfree = this;
	    }
	    else
	    {
		rep_GC_CLR_CONS(rep_CONS_VAL(this));
		newused++;
	    }
	}
	if(newused == 0)
	{
	    /* Whole ConsBlk unused, lets get rid of it.  */
	    rep_free(cb);
	    allocated_cons -= rep_CONSBLK_SIZE;
	}
	else
	{
	    if(newfreetail != NULL)
	    {
		/* Link this mini-freelist onto the main one.  */
		newfreetail->cdr = rep_CONS_VAL(cons_freelist);
		cons_freelist = newfree;
		used_cons += newused;
	    }
	    /* Have to rebuild the ConsBlk chain as well.  */
	    cb->next = cons_block_chain;
	    cons_block_chain = cb;
	}
	cb = nxt;
    }
}

static int
cons_cmp(repv v1, repv v2)
{
    int rc = 1;
    if(rep_TYPE(v1) == rep_TYPE(v2))
    {
	rc = rep_value_cmp(rep_CAR(v1), rep_CAR(v2));
	if(!rc)
	    rc = rep_value_cmp(rep_CDR(v1), rep_CDR(v2));
    }
    return rc;
}

repv
rep_list_1(repv v1)
{
    return rep_LIST_1(v1);
}

repv
rep_list_2(repv v1, repv v2)
{
    return rep_LIST_2(v1, v2);
}

repv
rep_list_3(repv v1, repv v2, repv v3)
{
    return rep_LIST_3(v1, v2, v3);
}

repv
rep_list_4(repv v1, repv v2, repv v3, repv v4)
{
    return rep_LIST_4(v1, v2, v3, v4);
}

repv
rep_list_5(repv v1, repv v2, repv v3, repv v4, repv v5)
{
    return rep_LIST_5(v1, v2, v3, v4, v5);
}


/* Vectors */

static rep_vector *vector_chain;
static int used_vector_slots;

repv
rep_make_vector(int size)
{
    int len = rep_VECT_SIZE(size);
    rep_vector *v = rep_ALLOC_CELL(len);
    if(v != NULL)
    {
	rep_SET_VECT_LEN(rep_VAL(v), size);
	v->next = vector_chain;
	vector_chain = v;
	used_vector_slots += size;
	rep_data_after_gc += len;
    }
    return rep_VAL(v);
}

static void
vector_sweep(void)
{
    rep_vector *this = vector_chain;
    vector_chain = NULL;
    used_vector_slots = 0;
    while(this != NULL)
    {
	rep_vector *nxt = this->next;
	if(!rep_GC_CELL_MARKEDP(rep_VAL(this)))
	    rep_FREE_CELL(this);
	else
	{
	    this->next = vector_chain;
	    vector_chain = this;
	    used_vector_slots += rep_VECT_LEN(this);
	    rep_GC_CLR_CELL(rep_VAL(this));
	}
	this = nxt;
    }
}

static int
vector_cmp(repv v1, repv v2)
{
    int rc = 1;
    if((rep_TYPE(v1) == rep_TYPE(v2)) && (rep_VECT_LEN(v1) == rep_VECT_LEN(v2)))
    {
	int i;
	int len = rep_VECT_LEN(v1);
	for(i = rc = 0; (i < len) && (rc == 0); i++)
	    rc = rep_value_cmp(rep_VECTI(v1, i), rep_VECTI(v2, i));
    }
    return rc;
}


/* Garbage collection */

static repv **static_roots;
static int next_static_root, allocated_static_roots;

rep_GC_root *rep_gc_root_stack = 0;
rep_GC_n_roots *rep_gc_n_roots_stack = 0;

rep_bool rep_in_gc = rep_FALSE;

/* rep_data_after_gc = bytes of storage used since last gc
   rep_gc_threshold = value that rep_data_after_gc should be before gc'ing
   rep_idle_gc_threshold = value that DAGC should be before gc'ing in idle time */
int rep_data_after_gc, rep_gc_threshold = 100000, rep_idle_gc_threshold = 20000;

#ifdef GC_MONITOR_STK
static int *gc_stack_high_tide;
#endif

void
rep_mark_static(repv *obj)
{
    if (next_static_root == allocated_static_roots)
    {
	int new_size = (allocated_static_roots
			? (allocated_static_roots * 2) : 256);
	if (static_roots != 0)
	    static_roots = rep_realloc (static_roots,
					new_size * sizeof (repv *));
	else
	    static_roots = rep_alloc (new_size * sizeof (repv *));
	assert (static_roots != 0);
	allocated_static_roots = new_size;
    }
    static_roots[next_static_root++] = obj;
}

/* Mark a single Lisp object.
   This attempts to eliminate as much tail-recursion as possible (by
   changing the rep_VAL and jumping back to the `again' label).

   Note that rep_VAL must not be NULL, and must not already have been
   marked, (see the rep_MARKVAL macro in lisp.h) */
void
rep_mark_value(register repv val)
{
#ifdef GC_MONITOR_STK
    int dummy;
    /* Assumes that the stack grows downwards (towards 0) */
    if(&dummy < gc_stack_high_tide)
	gc_stack_high_tide = &dummy;
#endif

#ifdef GC_MINSTACK
    /* This is a real problem. I can't safely stop marking since this means
       that some lisp data won't have been marked and therefore the sweep
       will screw up. But if I just keep on merrily recursing I risk
       blowing the stack.  */
    if(STK_SIZE <= GC_MINSTACK)
    {
	STK_WARN("garbage-collect(major problem!)");
	/* Perhaps I should longjmp() back to the start of the gc, then quit
	   totally?  */
	return;
    }
#endif

again:
    if(rep_INTP(val))
	return;

    if(rep_CONSP(val))
    {
	if(rep_CONS_WRITABLE_P(val))
	{
	    /* A cons. Attempts to walk though whole lists at a time
	       (since Lisp lists mainly link from the cdr).  */
	    rep_GC_SET_CONS(val);
	    if(rep_NILP(rep_GCDR(val)))
		/* End of a list. We can safely
		   mark the car non-recursively.  */
		val = rep_CAR(val);
	    else
	    {
		rep_MARKVAL(rep_CAR(val));
		val = rep_GCDR(val);
	    }
	    if(val && !rep_INTP(val) && !rep_GC_MARKEDP(val))
		goto again;
	    return;
	}
	else
	{
	    /* A constant cons cell. */
	    return;
	}
    }

    if (rep_CELL16P(val))
    {
	/* A user allocated type. */
	rep_type *t = rep_get_data_type(rep_CELL16_TYPE(val));
	rep_GC_SET_CELL(val);
	if (t->mark != 0)
	    t->mark(val);
	return;
    }

    /* So we know that it's a cell8 object */
    switch(rep_CELL8_TYPE(val))
    {
	rep_type *t;

    case rep_Vector:
    case rep_Compiled:
#ifdef rep_DUMPED
	/* Ensure that read-only objects aren't marked */
	if(rep_VECTOR_WRITABLE_P(val))
#endif
	{
	    int i, len = rep_VECT_LEN(val);
	    rep_GC_SET_CELL(val);
	    for(i = 0; i < len; i++)
		rep_MARKVAL(rep_VECTI(val, i));
	}
	break;

    case rep_Symbol:
	/* Dumped symbols are dumped read-write, so no worries.. */
	rep_GC_SET_CELL(val);
	rep_MARKVAL(rep_SYM(val)->name);
	rep_MARKVAL(rep_SYM(val)->value);
	rep_MARKVAL(rep_SYM(val)->function);
	rep_MARKVAL(rep_SYM(val)->prop_list);
	val = rep_SYM(val)->next;
	if(val && !rep_INTP(val) && !rep_GC_MARKEDP(val))
	    goto again;
	break;

    case rep_String:
	if(!rep_STRING_WRITABLE_P(val))
	    break;
	rep_GC_SET_CELL(val);
	break;

    case rep_Funarg:
	rep_GC_SET_CELL(val);
	rep_MARKVAL(rep_FUNARG(val)->env);
	rep_MARKVAL(rep_FUNARG(val)->fenv);
	rep_MARKVAL(rep_FUNARG(val)->special_env);
	rep_MARKVAL(rep_FUNARG(val)->fh_env);
	val = rep_FUNARG(val)->fun;
	if (val && !rep_GC_MARKEDP(val))
	    goto again;
	break;

    case rep_Var:
    case rep_Subr0:
    case rep_Subr1:
    case rep_Subr2:
    case rep_Subr3:
    case rep_Subr4:
    case rep_Subr5:
    case rep_SubrN:
    case rep_SF:
	break;

    default:
	t = rep_get_data_type(rep_CELL8_TYPE(val));
	rep_GC_SET_CELL(val);
	if (t->mark != 0)
	    t->mark(val);
    }
}

DEFUN("garbage-threshold", Vgarbage_threshold, Sgarbage_threshold, (repv val), rep_Var) /*
::doc:Vgarbage-threshold::
The number of bytes of storage which must be used before a garbage-
collection is triggered.
::end:: */
{
    return rep_handle_var_int(val, &rep_gc_threshold);
}

DEFUN("idle-garbage-threshold", Vidle_garbage_threshold, Sidle_garbage_threshold, (repv val), rep_Var) /*
::doc:Vidle-garbage-threshold::
The number of bytes of storage which must be used before a garbage-
collection is triggered when the editor is idle.
::end:: */
{
    return rep_handle_var_int(val, &rep_idle_gc_threshold);
}

DEFUN_INT("garbage-collect", Fgarbage_collect, Sgarbage_collect, (repv noStats), rep_Subr1, "") /*
::doc:Sgarbage-collect::
garbage-collect

Scans all allocated storage for unusable data, and puts it onto the free-
list. This is done automatically when the amount of storage used since the
last garbage-collection is greater than `garbage-threshold'.
::end:: */
{
    int i;
    rep_GC_root *rep_gc_root;
    rep_GC_n_roots *rep_gc_n_roots;
    struct rep_Call *lc;
#ifdef GC_MONITOR_STK
    int dummy;
    gc_stack_high_tide = &dummy;
#endif

    rep_in_gc = rep_TRUE;

#ifdef DEBUG_GC
    if (debug_gc)
    {
	repv stream = Fstderr_file ();
	rep_stream_puts (stream, "\n ** garbage collection:", -1, rep_FALSE);
	Fbacktrace (stream);
    }
#endif

    /* mark static objects */
    for(i = 0; i < next_static_root; i++)
	rep_MARKVAL(*static_roots[i]);
    /* mark stack based objects protected from GC */
    for(rep_gc_root = rep_gc_root_stack;
	rep_gc_root != 0; rep_gc_root = rep_gc_root->next)
    {
	rep_MARKVAL(*rep_gc_root->ptr);
    }
    for(rep_gc_n_roots = rep_gc_n_roots_stack; rep_gc_n_roots != 0;
	rep_gc_n_roots = rep_gc_n_roots->next)
    {
	for(i = 0; i < rep_gc_n_roots->count; i++)
	    rep_MARKVAL(rep_gc_n_roots->first[i]);
    }

    /* Do data-type specific marking. */
    for (i = 0; i < TYPE_HASH_SIZE; i++)
    {
	rep_type *t = data_types[i];
	while (t != 0)
	{
	    if (t->mark_type != 0)
		t->mark_type();
	    t = t->next;
	}
    }

    rep_mark_regexp_data();

#ifdef HAVE_DYNAMIC_LOADING
    rep_mark_dl_data();
#endif

    /* have to mark the Lisp backtrace.	 */
    lc = rep_call_stack;
    while(lc)
    {
	rep_MARKVAL(lc->fun);
	rep_MARKVAL(lc->args);
	rep_MARKVAL(lc->saved_env);
	rep_MARKVAL(lc->saved_fenv);
	rep_MARKVAL(lc->saved_special_env);
	/* don't bother marking `args_evalled_p' it's always `nil' or `t'  */
	lc = lc->next;
    }

    /* Finished marking, start sweeping. */

    for(i = 0; i < TYPE_HASH_SIZE; i++)
    {
	rep_type *t = data_types[i];
	while (t != 0)
	{
	    if (t->sweep != 0)
		t->sweep();
	    t = t->next;
	}
    }

    rep_data_after_gc = 0;
    rep_in_gc = rep_FALSE;

#ifdef GC_MONITOR_STK
    fprintf(stderr, "gc: stack usage = %d\n",
	    ((int)&dummy) - (int)gc_stack_high_tide);
#endif

#ifdef DEBUG_GC
    if (debug_gc)
    {
	repv stream = Fstderr_file ();
	rep_stream_putc (stream, '\n');
    }
#endif

    if(rep_NILP(noStats))
    {
	return(rep_list_4(Fcons(rep_MAKE_INT(used_cons),
			       rep_MAKE_INT(allocated_cons - used_cons)),
		      Fcons(rep_MAKE_INT(rep_used_symbols),
			       rep_MAKE_INT(rep_allocated_symbols - rep_used_symbols)),
		      Fcons(rep_MAKE_INT(allocated_strings),
			       rep_MAKE_INT(allocated_string_bytes)),
		      rep_MAKE_INT(used_vector_slots)));
    }

    return(Qt);
}


void
rep_pre_values_init(void)
{
    rep_register_type(rep_Cons, "cons", cons_cmp,
		  rep_lisp_prin, rep_lisp_prin, cons_sweep, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Int, "integer", number_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Vector, "vector", vector_cmp,
		  rep_lisp_prin, rep_lisp_prin, vector_sweep, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_String, "string", string_cmp, rep_string_princ,
		  rep_string_print, string_sweep, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Compiled, "bytecode", vector_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Void, "void", rep_type_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Var, "var", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_SF, "special-form", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr0, "subr0", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr1, "subr1", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr2, "subr2", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr3, "subr3", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr4, "subr4", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_Subr5, "subr5", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    rep_register_type(rep_SubrN, "subrn", rep_ptr_cmp,
		  rep_lisp_prin, rep_lisp_prin, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void
rep_values_init(void)
{
    rep_ADD_SUBR(Scons);
    rep_ADD_SUBR(Sgarbage_threshold);
    rep_ADD_SUBR(Sidle_garbage_threshold);
    rep_ADD_SUBR_INT(Sgarbage_collect);
}

void
rep_values_kill(void)
{
    rep_cons_block *cb = cons_block_chain;
    rep_vector *v = vector_chain;
    rep_string *s = strings;
    while(cb != NULL)
    {
	rep_cons_block *nxt = cb->next;
	rep_free(cb);
	cb = nxt;
    }
    while(v != NULL)
    {
	rep_vector *nxt = v->next;
	rep_FREE_CELL(v);
	v = nxt;
    }
    while(s != NULL)
    {
	rep_string *nxt = s->next;
	rep_FREE_CELL(s);
	s = nxt;
    }
    cons_block_chain = NULL;
    vector_chain = NULL;
    strings = NULL;
}


/* Support for dumped Lisp code */

#ifdef rep_DUMPED

void
rep_dumped_init(void)
{
    /* Main function is to go through all dumped symbols, interning
       them, and changing rep_NULL values to be void. */
    rep_symbol *sym;

    /* But first, intern nil, it'll be filled in later. */
    Qnil = Fintern_symbol(rep_VAL(rep_DUMPED_SYM_NIL), rep_void_value);

    /* Initialise allocated_X counts */
    allocated_cons = &rep_dumped_cons_end - &rep_dumped_cons_start;
    rep_allocated_symbols = &rep_dumped_symbols_end - &rep_dumped_symbols_start;
    /* ish.. */
    used_vector_slots = ((&rep_dumped_vectors_end - &rep_dumped_vectors_start)
			 + (&rep_dumped_bytecode_end - &rep_dumped_bytecode_start));

    /* Stop one symbol too early, since we've already added it (nil) */
    for(sym = &rep_dumped_symbols_start; sym < (&rep_dumped_symbols_end)-1; sym++)
    {
	/* Second arg is [OBARRAY], but it's only checked against
	   being a vector. */
	Fintern_symbol(rep_VAL(sym), rep_void_value);
	if(sym->value == rep_NULL)
	    sym->value = rep_void_value;
	if(sym->function == rep_NULL)
	    sym->function = rep_void_value;
	if(sym->prop_list == rep_NULL)
	    sym->prop_list = Qnil;
    }
}

#endif /* rep_DUMPED */
