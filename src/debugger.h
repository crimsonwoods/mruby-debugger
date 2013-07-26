#ifndef MRUBY_DEBUGGER_H
#define MRUBY_DEBUGGER_H

#include "mruby.h"
#include "mruby/irep.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Context for debugger.
 */
typedef void *mrbdbg_context_t;

/**
 * Parameters to attach.
 */
typedef struct mrbdbg_attach_param_t_ {
} mrbdbg_attach_param_t;

/**
 * Parameters to initialize debugger.
 */
typedef struct mrbdbg_init_param_t_ {
} mrbdbg_init_param_t;

/**
 * Activator.
 */
typedef struct mrbdbg_debugger_activator_t_ {
} mrbdbg_debugger_activator_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*mrbdbg_is_attached)(mrb_state *mrb);
typedef bool (*mrbdbg_is_initialized)(mrb_state *mrb);
typedef bool (*mrbdbg_attach)(mrb_state *mrb, mrbdbg_attach_param_t const *param);
typedef void (*mrbdbg_detach)(mrb_state *mrb);
typedef bool (*mrbdbg_init)(mrbdbg_context_t *context, mrb_state *mrb, mrbdbg_init_param_t const *param);
typedef void (*mrbdbg_cleanup)(mrbdbg_context_t context, mrb_state *mrb);
typedef bool (*mrbdbg_is_step_required)(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs);
typedef void (*mrbdbg_on_step)(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs);

#ifdef __cplusplus
}
#endif

typedef struct mrbdbg_debugger_t_ {
  mrbdbg_is_attached      is_attached;
  mrbdbg_is_initialized   is_initialized;
  mrbdbg_attach           attach;
  mrbdbg_detach           detach;
  mrbdbg_init             init;
  mrbdbg_cleanup          cleanup;
  mrbdbg_is_step_required is_step_required;
  mrbdbg_on_step          on_step;
} mrbdbg_debugger_t;

#ifdef __cplusplus
extern "C" {
#endif

extern bool mrbdbg_debugger_activate(mrb_state *mrb, mrbdbg_debugger_activator_t *activator, mrbdbg_debugger_t *debugger);
extern void mrbdbg_debugger_inactivate(mrb_state *mrb, mrbdbg_debugger_activator_t *activator);

#ifdef __cplusplus
}
#endif

#endif /* end of MRUBY_DEBUGGER_H */

