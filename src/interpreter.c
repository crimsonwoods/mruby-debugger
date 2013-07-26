#include "debugger.h"
#include "mruby/string.h"
#include "mruby/class.h"
#include "mruby/proc.h"
#include <errno.h>
#include <string.h>

#ifdef ENABLE_STDIO

#define MAXARG_Bx        (0xffff)
#define MAXARG_sBx       (MAXARG_Bx>>1)         /* `sBx' is signed */

#define GET_OPCODE(i)            ((int)(((mrb_code)(i)) & 0x7f))
#define GETARG_A(i)              ((int)((((mrb_code)(i)) >> 23) & 0x1ff))
#define GETARG_B(i)              ((int)((((mrb_code)(i)) >> 14) & 0x1ff))
#define GETARG_C(i)              ((int)((((mrb_code)(i)) >>  7) & 0x7f))
#define GETARG_Bx(i)             ((int)((((mrb_code)(i)) >>  7) & 0xffff))
#define GETARG_sBx(i)            ((int)(GETARG_Bx(i)-MAXARG_sBx))
#define GETARG_Ax(i)             ((int32_t)((((mrb_code)(i)) >>  7) & 0x1ffffff))
#define GETARG_UNPACK_b(i,n1,n2) ((int)((((mrb_code)(i)) >> (7+(n2))) & (((1<<(n1))-1))))
#define GETARG_UNPACK_c(i,n1,n2) ((int)((((mrb_code)(i)) >> 7) & (((1<<(n2))-1))))
#define GETARG_b(i)              GETARG_UNPACK_b(i,14,2)
#define GETARG_c(i)              GETARG_UNPACK_c(i,14,2)

#define OP_R_NORMAL 0
#define OP_R_BREAK  1
#define OP_R_RETURN 2

enum {
  OP_NOP=0,/*                                                             */
  OP_MOVE,/*      A B     R(A) := R(B)                                    */
  OP_LOADL,/*     A Bx    R(A) := Lit(Bx)                                 */
  OP_LOADI,/*     A sBx   R(A) := sBx                                     */
  OP_LOADSYM,/*   A Bx    R(A) := Sym(Bx)                                 */
  OP_LOADNIL,/*   A       R(A) := nil                                     */
  OP_LOADSELF,/*  A       R(A) := self                                    */
  OP_LOADT,/*     A       R(A) := true                                    */
  OP_LOADF,/*     A       R(A) := false                                   */

  OP_GETGLOBAL,/* A Bx    R(A) := getglobal(Sym(Bx))                      */
  OP_SETGLOBAL,/* A Bx    setglobal(Sym(Bx), R(A))                        */
  OP_GETSPECIAL,/*A Bx    R(A) := Special[Bx]                             */
  OP_SETSPECIAL,/*A Bx    Special[Bx] := R(A)                             */
  OP_GETIV,/*     A Bx    R(A) := ivget(Sym(Bx))                          */
  OP_SETIV,/*     A Bx    ivset(Sym(Bx),R(A))                             */
  OP_GETCV,/*     A Bx    R(A) := cvget(Sym(Bx))                          */
  OP_SETCV,/*     A Bx    cvset(Sym(Bx),R(A))                             */
  OP_GETCONST,/*  A Bx    R(A) := constget(Sym(Bx))                       */
  OP_SETCONST,/*  A Bx    constset(Sym(Bx),R(A))                          */
  OP_GETMCNST,/*  A Bx    R(A) := R(A)::Sym(B)                            */
  OP_SETMCNST,/*  A Bx    R(A+1)::Sym(B) := R(A)                          */
  OP_GETUPVAR,/*  A B C   R(A) := uvget(B,C)                              */
  OP_SETUPVAR,/*  A B C   uvset(B,C,R(A))                                 */

  OP_JMP,/*       sBx     pc+=sBx                                         */
  OP_JMPIF,/*     A sBx   if R(A) pc+=sBx                                 */
  OP_JMPNOT,/*    A sBx   if !R(A) pc+=sBx                                */
  OP_ONERR,/*     sBx     rescue_push(pc+sBx)                             */
  OP_RESCUE,/*    A       clear(exc); R(A) := exception (ignore when A=0) */
  OP_POPERR,/*    A       A.times{rescue_pop()}                           */
  OP_RAISE,/*     A       raise(R(A))                                     */
  OP_EPUSH,/*     Bx      ensure_push(SEQ[Bx])                            */
  OP_EPOP,/*      A       A.times{ensure_pop().call}                      */

  OP_SEND,/*      A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C))    */
  OP_SENDB,/*     A B C   R(A) := call(R(A),mSym(B),R(A+1),...,R(A+C),&R(A+C+1))*/
  OP_FSEND,/*     A B C   R(A) := fcall(R(A),mSym(B),R(A+1),...,R(A+C-1)) */
  OP_CALL,/*      A B C   R(A) := self.call(R(A),.., R(A+C))              */
  OP_SUPER,/*     A B C   R(A) := super(R(A+1),... ,R(A+C-1))             */
  OP_ARGARY,/*    A Bx    R(A) := argument array (16=6:1:5:4)             */
  OP_ENTER,/*     Ax      arg setup according to flags (24=5:5:1:5:5:1:1) */
  OP_KARG,/*      A B C   R(A) := kdict[mSym(B)]; if C kdict.rm(mSym(B))  */
  OP_KDICT,/*     A C     R(A) := kdict                                   */

  OP_RETURN,/*    A B     return R(A) (B=normal,in-block return/break)    */
  OP_TAILCALL,/*  A B C   return call(R(A),mSym(B),*R(C))                 */
  OP_BLKPUSH,/*   A Bx    R(A) := block (16=6:1:5:4)                      */

  OP_ADD,/*       A B C   R(A) := R(A)+R(A+1) (mSyms[B]=:+,C=1)           */
  OP_ADDI,/*      A B C   R(A) := R(A)+C (mSyms[B]=:+)                    */
  OP_SUB,/*       A B C   R(A) := R(A)-R(A+1) (mSyms[B]=:-,C=1)           */
  OP_SUBI,/*      A B C   R(A) := R(A)-C (mSyms[B]=:-)                    */
  OP_MUL,/*       A B C   R(A) := R(A)*R(A+1) (mSyms[B]=:*,C=1)           */
  OP_DIV,/*       A B C   R(A) := R(A)/R(A+1) (mSyms[B]=:/,C=1)           */
  OP_EQ,/*        A B C   R(A) := R(A)==R(A+1) (mSyms[B]=:==,C=1)         */
  OP_LT,/*        A B C   R(A) := R(A)<R(A+1)  (mSyms[B]=:<,C=1)          */
  OP_LE,/*        A B C   R(A) := R(A)<=R(A+1) (mSyms[B]=:<=,C=1)         */
  OP_GT,/*        A B C   R(A) := R(A)>R(A+1)  (mSyms[B]=:>,C=1)          */
  OP_GE,/*        A B C   R(A) := R(A)>=R(A+1) (mSyms[B]=:>=,C=1)         */

  OP_ARRAY,/*     A B C   R(A) := ary_new(R(B),R(B+1)..R(B+C))            */
  OP_ARYCAT,/*    A B     ary_cat(R(A),R(B))                              */
  OP_ARYPUSH,/*   A B     ary_push(R(A),R(B))                             */
  OP_AREF,/*      A B C   R(A) := R(B)[C]                                 */
  OP_ASET,/*      A B C   R(B)[C] := R(A)                                 */
  OP_APOST,/*     A B C   *R(A),R(A+1)..R(A+C) := R(A)                    */

  OP_STRING,/*    A Bx    R(A) := str_dup(Lit(Bx))                        */
  OP_STRCAT,/*    A B     str_cat(R(A),R(B))                              */

  OP_HASH,/*      A B C   R(A) := hash_new(R(B),R(B+1)..R(B+C))           */
  OP_LAMBDA,/*    A Bz Cz R(A) := lambda(SEQ[Bz],Cm)                      */
  OP_RANGE,/*     A B C   R(A) := range_new(R(B),R(B+1),C)                */

  OP_OCLASS,/*    A       R(A) := ::Object                                */
  OP_CLASS,/*     A B     R(A) := newclass(R(A),mSym(B),R(A+1))           */
  OP_MODULE,/*    A B     R(A) := newmodule(R(A),mSym(B))                 */
  OP_EXEC,/*      A Bx    R(A) := blockexec(R(A),SEQ[Bx])                 */
  OP_METHOD,/*    A B     R(A).newmethod(mSym(B),R(A+1))                  */
  OP_SCLASS,/*    A B     R(A) := R(B).singleton_class                    */
  OP_TCLASS,/*    A       R(A) := target_class                            */

  OP_DEBUG,/*     A       print R(A)                                      */
  OP_STOP,/*              stop VM                                         */
  OP_ERR,/*       Bx      raise RuntimeError with message Lit(Bx)         */

  OP_RSVD1,/*             reserved instruction #1                         */
  OP_RSVD2,/*             reserved instruction #2                         */
  OP_RSVD3,/*             reserved instruction #3                         */
  OP_RSVD4,/*             reserved instruction #4                         */
  OP_RSVD5,/*             reserved instruction #5                         */
};

#define MAX_COMMAND_LENGTH 128
#define MAX_BREAK_POINT     32

typedef enum step_mode_t_ {
  STEP_IN,
  STEP_OUT,
  STEP_OVER,
  STEP_NONE
} step_mode_t;

typedef struct mrbgbg_breakpoint_t_ {
  int         irep_idx;
  mrb_code   *pc;
  char const *filename;
  int         line_no;
} mrbdbg_breakpoint_t;

typedef struct mrbdbg_interpreter_context_t_ {
  step_mode_t          step_mode;
  step_mode_t          prev_step_mode;
  mrb_code            *next_break_pc;
  mrbdbg_breakpoint_t  break_points[MAX_BREAK_POINT];
  int                  number_of_break_points;
  bool                 is_quit;
} mrbdbg_interpreter_context_t;

static bool is_attached = false;
static bool is_initialized = false;

bool mrbdbg_interpreter_is_attached(mrb_state *mrb)
{
  return is_attached;
}
 
bool mrbdbg_interpreter_is_initialized(mrb_state *mrb)
{
  return is_initialized;
}

bool mrbdbg_interpreter_attach(mrb_state *mrb, mrbdbg_attach_param_t const *param)
{
  if (is_attached) {
    return true;
  }

  is_attached = true;
  return true;
}

void mrbdbg_interpreter_detach(mrb_state *mrb)
{
  is_attached = false;
}

bool mrbdbg_interpreter_init(mrbdbg_context_t *context, mrb_state *mrb, mrbdbg_init_param_t const *param)
{
  if (is_initialized) {
    return true;
  }

  if (NULL == context) {
    return false;
  }

  mrbdbg_interpreter_context_t *icontext = (mrbdbg_interpreter_context_t*)mrb_malloc(mrb, sizeof(mrbdbg_interpreter_context_t));
  if (NULL == icontext) {
    return false;
  }

  icontext->step_mode              = STEP_IN;
  icontext->prev_step_mode         = STEP_IN;
  icontext->next_break_pc          = NULL;
  icontext->number_of_break_points = 0;
  icontext->is_quit = false;
  *context = icontext;

  printf("Interpreter Debugger is activated.\n");

  is_initialized = true;
  return true;
}

void mrbdbg_interpreter_cleanup(mrbdbg_context_t context, mrb_state *mrb)
{
  if (NULL != context) {
    mrb_free(mrb, context);
  }
  is_initialized = false;
}

bool mrbdbg_interpreter_is_step_required(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  if (NULL == context) {
    /* not initialized. */
    return false;
  }

  if (NULL == irep) {
    /* first time. */
    return true;
  }

  mrbdbg_interpreter_context_t * const icontext = (mrbdbg_interpreter_context_t*)context;

  if (icontext->is_quit) {
    return false;
  }

  switch (icontext->step_mode)
  {
  case STEP_IN:
    if ((NULL == irep->filename) || (NULL == irep->lines)) {
      icontext->prev_step_mode = icontext->step_mode;
      icontext->step_mode = STEP_NONE;
    } else if (pc == icontext->next_break_pc) {
      return true;
    } else if (NULL == icontext->next_break_pc) {
      return true;
    }
    break;
  case STEP_OVER:
    if ((NULL == irep->filename) || (NULL == irep->lines)) {
      icontext->prev_step_mode = icontext->step_mode;
      icontext->step_mode = STEP_NONE;
    } else if (pc == icontext->next_break_pc) {
      return true;
    }
    break;
  case STEP_OUT:
    break;
  case STEP_NONE:
    if ((NULL != irep->filename) && (NULL != irep->lines)) {
      icontext->step_mode = icontext->prev_step_mode;
    }
    if (pc == icontext->next_break_pc) {
      return true;
    }
    return false;
  default:
    return false;
  }

  return false;
}

static void list_source_code(char const * const filename, int line);
static void dump_code(mrb_state *mrb, mrb_irep *irep, mrb_code* pc, mrb_value *regs);
static void dump_value(mrb_state *mrb, mrb_value value);
static mrb_code *lookup_next_break_pc(step_mode_t mode, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs);

void mrbdbg_interpreter_on_step(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  if ((false == is_initialized) || (NULL == context)) {
    return;
  }

  mrbdbg_interpreter_context_t * const icontext = (mrbdbg_interpreter_context_t*)context;

  while (false == icontext->is_quit) {
    printf("(mrdb): ");fflush(stdout);

    char command[MAX_COMMAND_LENGTH];
    if (NULL == fgets(command, sizeof(command), stdin)) {
      return;
    }

    switch (command[0])
    {
    case 's': /* step-in */
      if (NULL != irep->filename) {
        mrb_code *next_break_pc = lookup_next_break_pc(STEP_IN, mrb, irep, pc, regs);
        if (NULL != next_break_pc) {
          icontext->next_break_pc = next_break_pc;
        }
      }
      icontext->step_mode = STEP_IN;
      return;
    case 'n': /* step-over */
      if (NULL != irep->filename) {
        mrb_code *next_break_pc = lookup_next_break_pc(STEP_OVER, mrb, irep, pc, regs);
        if (NULL != next_break_pc) {
          icontext->next_break_pc = next_break_pc;
        }
      }
      icontext->step_mode = STEP_OVER;
      return;
    case 'd': /* display */
      printf("irep-idx: %d\n", irep->idx);
      printf("nregs = %d, nlocals = %d, pools = %lu, syms = %lu\n",
        irep->nregs, irep->nlocals, irep->plen, irep->slen);
      printf("instruction: 0x%08x : ", *pc);
      dump_code(mrb, irep, pc, regs);
      if (NULL != regs) {
        int i;
        for (i = 0; i < irep->nregs; ++i) {
          printf("R%d = ", i);
          dump_value(mrb, regs[i]);
          if ((i % 4) == 0) {
            printf("\n");
          } else if (i < (irep->nregs - 1)) {
            printf(" / ");
          } else {
            printf("\n");
          }
        }
      }
      fflush(stdout);
      break;
    case 'l': /* list source */
      if (NULL != irep) {
        int const line = (NULL == irep->lines) ? -1 : irep->lines[pc - irep->iseq];
        list_source_code(irep->filename, line);
      }
      break;
    case 'q': /* quit */
      icontext->is_quit = true;
      break;
    }
  }
}

static void list_source_code(char const * const filename, int line)
{
  FILE *fp = fopen(filename, "r");
  if (NULL == fp) {
    fprintf(stderr, "cannot open file '%s': %s\n", filename, strerror(errno));
    return;
  }
  char line_buf[512];
  int i;
  for (i = 1; NULL != fgets(line_buf, sizeof(line_buf), fp); ++i) {
    if ((i >= (line - 5)) && (i <= (line + 5))) {
      if (i == line) {
        fprintf(stdout, " -> ");
      } else {
        fprintf(stdout, "    ");
      }
      if ((line + 5) >= 10000) {
        fprintf(stdout, "%05d %s", i, line_buf);
      } else if ((line + 5) >= 1000) {
        fprintf(stdout, "%04d %s", i, line_buf);
      } else {
        fprintf(stdout, "%03d %s", i, line_buf);
      }
      fflush(stdout);
    }
  }
  fclose(fp);
}

static void
dump_code(mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  mrb_code arg_A, arg_B, arg_C, arg_Bx, arg_sBx, arg_Ax;
  uint32_t i;

  arg_A   = GETARG_A(*pc);
  arg_B   = GETARG_B(*pc);
  arg_C   = GETARG_C(*pc);
  arg_Bx  = GETARG_Bx(*pc);
  arg_sBx = GETARG_sBx(*pc);
  arg_Ax  = GETARG_Ax(*pc);

  switch (GET_OPCODE(*pc)) {
  case OP_NOP:
    printf("OP_NOP\n");
    break;
  case OP_MOVE:
    printf("OP_MOVE(%u %u) : R%u = R%u\n", arg_A, arg_B, arg_A, arg_B);
    break;
  case OP_LOADL: {
    mrb_value s = mrb_funcall(mrb, irep->pool[arg_Bx], "to_s", 0);
    printf("OP_LOADL(%u %u) : R%u = Lit(%s)", arg_A, arg_Bx, arg_A, RSTRING_PTR(s));
    } break;
  case OP_LOADI:
    printf("OP_LOADI(%u) : R%u = %d\n", arg_A, arg_A, (int)arg_sBx);
    break;
  case OP_LOADSYM:
    printf("OP_LOADSYM(%u %u) : R%u = Sym(:%s)\n",
      arg_A, arg_Bx,
      arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_LOADNIL:
    printf("OP_LOADNIL(%u) : R%u = nil\n", arg_A, arg_A);
    break;
  case OP_LOADSELF:
    printf("OP_LOADSELF(%u) : R%u = self\n", arg_A, arg_A);
    break;
  case OP_LOADT:
    printf("OP_LOADT(%u) : R%u = true\n", arg_A, arg_A);
    break;
  case OP_LOADF:
    printf("OP_LOADF(%u) : R%u = false\n", arg_A, arg_A);
    break;
  case OP_GETGLOBAL:
    printf("OP_GETGLOBAL(%u %u) : R%u = getglobal(Sym(:%s))\n",
      arg_A, arg_Bx,
      arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_SETGLOBAL:
    printf("OP_SETGLOBAL(%u %u) : setglobal(Sym(:%s), R%u)\n",
      arg_A, arg_Bx,
      mrb_sym2name(mrb, irep->syms[arg_Bx]), arg_A);
    break;
  case OP_GETSPECIAL:
    printf("OP_GETSPECIAL(%u %u) : R%u = Special[%u]\n",
      arg_A, arg_Bx,
      arg_A, arg_Bx);
    break;
  case OP_SETSPECIAL:
    printf("OP_SETSPECIAL(%u %u) : Special[%u] = R%u\n",
      arg_A, arg_Bx,
      arg_Bx, arg_A);
    break;
  case OP_GETIV:
    printf("OP_GETIV(%u %u) : R%u = ivget(Sym(:%s))\n",
      arg_A, arg_Bx,
      arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_SETIV:
    printf("OP_SETIV(%u %u) : ivset(Sym(:%s), R%u)\n",
      arg_A, arg_Bx,
      mrb_sym2name(mrb, irep->syms[arg_Bx]), arg_A);
    break;
  case OP_GETCV:
    printf("OP_GETCV(%u %u) : R%u = cvget(Sym(:%s))\n",
      arg_A, arg_Bx,
      arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_SETCV:
    printf("OP_SETCV(%u %u) : cvset(Sym(:%s), R%u)\n",
      arg_A, arg_Bx,
      mrb_sym2name(mrb, irep->syms[arg_Bx]), arg_A);
    break;
  case OP_GETCONST:
    printf("OP_GETCONST(%u, %u) : R%u = constget(Sym(:%s))\n",
      arg_A, arg_Bx,
      arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_SETCONST:
    printf("OP_SETCONST(%u %u) : constset(Sym(:%s), R%u)\n",
      arg_A, arg_Bx,
      mrb_sym2name(mrb, irep->syms[arg_Bx]), arg_A);
    break;
  case OP_GETMCNST:
    printf("OP_GETMCNST(%u %u) : R%u = R%u::Sym(:%s)\n",
      arg_A, arg_Bx,
      arg_A, arg_A, mrb_sym2name(mrb, irep->syms[arg_Bx]));
    break;
  case OP_SETMCNST:
    printf("OP_SETMCNST(%u %u) : R%u::Sym(:%s) = R%u\n",
      arg_A, arg_Bx,
      arg_A + 1, mrb_sym2name(mrb, irep->syms[arg_Bx]), arg_A);
    break;
  case OP_GETUPVAR:
    printf("OP_GETUPVAR(%u %u %u) : R%u = uvget(%u, %u)\n",
      arg_A, arg_B, arg_C,
      arg_A, arg_B, arg_C);
    break;
  case OP_SETUPVAR:
    printf("OP_SETUPVAR(%u %u %u) : uvset(%u, %u, R%u)\n",
      arg_A, arg_B, arg_C,
      arg_B, arg_C, arg_A);
    break;
  case OP_JMP:
    printf("OP_JMP(%u) : PC += %u\n",
      arg_sBx, arg_sBx);
    break;
  case OP_JMPIF:
    printf("OP_JMPIF(%u %u) : if R%u then PC += %u\n",
      arg_A, arg_sBx,
      arg_A, arg_sBx);
    break;
  case OP_JMPNOT:
    printf("OP_JMPNOT(%u %u) : if !R%u then PC += %u\n",
      arg_A, arg_sBx,
      arg_A, arg_sBx);
    break;
  case OP_ONERR:
    printf("OP_ONERR(%u) : rescue_push(PC + %u)\n",
      arg_sBx, arg_sBx);
    break;
  case OP_RESCUE:
    printf("OP_RESCUE(%u) : clear(exc); R%u = exception\n",
      arg_A, arg_A);
    break;
  case OP_POPERR:
    printf("OP_POPERR(%u) : %u.times{rescue_pop()}\n",
      arg_A, arg_A);
    break;
  case OP_RAISE:
    printf("OP_RAISE(%u) : raise(R%u)\n",
      arg_A, arg_A);
    break;
  case OP_EPUSH:
    printf("OP_EPUSH(%u) : ensure_push(SEQ[%u])\n",
      arg_Bx, arg_Bx);
    break;
  case OP_EPOP:
    printf("OP_EPOP(%u) : %u.times{ensure_pop().call}\n",
      arg_A, arg_A);
    break;
  case OP_SEND:
    printf("OP_SEND(%u, %u, %u) : R%d = call(R%d, Sym(:%s)",
      arg_A, arg_B, arg_C,
      arg_A, arg_A,
      mrb_sym2name(mrb, irep->syms[arg_B]));
    for (i = 1; i <= arg_C; ++i) {
      printf(", R%d", arg_A + i);
    }
    printf(")\n");
    break;
  case OP_SENDB:
    printf("OP_SENDB: R%d = call(R%d, Sym(:%s)",
      arg_A, arg_A,
      mrb_sym2name(mrb, irep->syms[arg_B]));
    for (i = 1; i <= arg_C; ++i) {
      printf(", R%d", arg_A + i);
    }
    printf(", &R%d)\n", arg_A + arg_C + 1);
    break;
  case OP_FSEND:
    printf("OP_FSEND: R%d = fcall(R%d, Sym(:%s)",
      arg_A, arg_A,
      mrb_sym2name(mrb, irep->syms[arg_B]));
    if (arg_C > 1) {
      for (i = 1; i <= (arg_C - 1); ++i) {
        printf(", R%d", arg_A + i);
      }
    }
    printf(")\n");
    break;
  case OP_CALL:
    printf("OP_CALL(%u %u %u) : self.call(...)\n",
      arg_A, arg_B, arg_C);
    break;
  case OP_SUPER:
    break;
  case OP_ARGARY:
    break;
  case OP_ENTER: {
    int const m1 = (arg_Ax >> 18) & 0x1f;
    int const o  = (arg_Ax >> 13) & 0x1f;
    int const r  = (arg_Ax >> 12) & 0x01;
    int const m2 = (arg_Ax >>  7) & 0x1f;
    printf("OP_ENTER(0x%06x) : "
           "required = %d, optional = %d, rest = %d"
           "required_after_rest = %d\n",
      arg_Ax, m1, o, r, m2);
    } break;
  case OP_KARG:
    break;
  case OP_KDICT:
    break;
  case OP_RETURN:
    printf("OP_RETURN(%u %u) : return R%u (%s)\n",
      arg_A, arg_B, arg_A,
      (arg_B == OP_R_NORMAL ? "normal" :
       arg_B == OP_R_BREAK  ? "break"  :
       arg_B == OP_R_RETURN ? "return" : "unknown"));
    break;
  case OP_TAILCALL:
    break;
  case OP_BLKPUSH:
    break;
  case OP_ADD:
    break;
  case OP_ADDI:
    break;
  case OP_SUB:
    break;
  case OP_SUBI:
    break;
  case OP_MUL:
    break;
  case OP_DIV:
    break;
  case OP_EQ:
    break;
  case OP_LT:
    break;
  case OP_LE:
    break;
  case OP_GT:
    break;
  case OP_GE:
    break;
  case OP_ARRAY:
    break;
  case OP_ARYCAT:
    break;
  case OP_ARYPUSH:
    break;
  case OP_AREF:
    break;
  case OP_ASET:
    break;
  case OP_APOST:
    break;
  case OP_STRING: {
      mrb_value s = mrb_str_literal(mrb, irep->pool[GETARG_Bx(*pc)]);
      printf("OP_STRING(%u %u) : R%d = Lit(%s)\n", arg_A, arg_Bx, arg_A, RSTRING_PTR(s));
    }
    break;
  case OP_STRCAT:
    printf("OP_STRCAT(%u %u) : str_cat(R%u, R%u)\n",
      arg_A, arg_B, arg_A, arg_B);
    break;
  case OP_HASH:
    break;
  case OP_LAMBDA:
    break;
  case OP_RANGE:
    break;
  case OP_OCLASS:
    break;
  case OP_CLASS:
    break;
  case OP_MODULE:
    break;
  case OP_EXEC:
    printf("OP_EXEC(%u %u) : R%u = blockexec(R%u, SEQ[%u])\n",
      arg_A, arg_Bx,
      arg_A, arg_A, arg_Bx);
    break;
  case OP_METHOD:
    break;
  case OP_SCLASS:
    break;
  case OP_TCLASS:
    break;
  case OP_DEBUG:
    break;
  case OP_STOP:
    printf("OP_STOP\n");
    break;
  case OP_ERR:
    break;
  default:
    printf("OP_UNKNOWN\n");
    break;
  }
}

static void
dump_value(mrb_state *mrb, mrb_value value)
{
  switch (mrb_type(value)) {
  case MRB_TT_FALSE:
    if (mrb_nil_p(value)) {
      printf("nil");
    } else {
      printf("false");
    }
    break;
  case MRB_TT_TRUE:
    printf("true");
    break;
  case MRB_TT_FIXNUM:
    printf("%d", mrb_fixnum(value));
    break;
  case MRB_TT_FLOAT:
#ifdef MRB_USE_FLOAT
    printf("%f", mrb_float(value));
#else
    printf("%lf", mrb_float(value));
#endif
    break;
  case MRB_TT_STRING:
    printf("%s", RSTRING_PTR(value));
    break;
  default: {
    mrb_value s = mrb_funcall(mrb, value, "to_s", 0);
    printf("%s(%s)", mrb_obj_classname(mrb, value), RSTRING_PTR(s));
    } break;
  }
}

static mrb_code *
lookup_next_break_pc(step_mode_t mode, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  mrb_code *next_break_pc = NULL;
  int const line = irep->lines[pc - irep->iseq];
  int const offset = pc - irep->iseq;
  int i;
  for (i = offset; i < irep->ilen; ++i) {
    int const arg_A = GETARG_A(*pc);
    int const arg_B = GETARG_B(*pc);
    switch (GET_OPCODE(*pc)) {
    case OP_SEND:
    case OP_SENDB:
    case OP_FSEND:
    case OP_CALL:
    case OP_SUPER: {
      if (mode == STEP_OVER) {
        next_break_pc = pc + 1;
      } else if (mode == STEP_IN) {
        struct RClass *c = mrb_class(mrb, regs[arg_A]);
        struct RProc *m = mrb_method_search_vm(mrb, &c, irep->syms[arg_B]);
        if (MRB_PROC_CFUNC_P(m)) {
          next_break_pc = pc + 1;
        } else {
          if (NULL != m->body.irep->filename) {
            next_break_pc = m->body.irep->iseq;
          } else {
            next_break_pc = pc + 1;
          }
        }
      }
      } break;
    default: {
      int const next_line = irep->lines[pc + i - irep->iseq];
      if (line < next_line) {
        next_break_pc = pc + i;
        break;
      }
      } break;
    }
    if (NULL != next_break_pc) {
      break;
    }
    ++pc;
  }
  if (NULL == next_break_pc) {
    next_break_pc = mrb->c->ci->pc;
  }
  return next_break_pc;
}

#endif

