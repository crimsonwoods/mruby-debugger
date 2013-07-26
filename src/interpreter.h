#ifndef MRUBY_DEBUGGER_INTERPRETER_H
#define MRUBY_DEBUGGER_INTERPRETER_H

#include "debugger.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ENABLE_STDIO
extern bool mrbdbg_interpreter_is_attached(mrb_state *mrb);
extern bool mrbdbg_interpreter_is_initialized(mrb_state *mrb);
extern bool mrbdbg_interpreter_attach(mrb_state *mrb, mrbdbg_attach_param_t const *param);
extern void mrbdbg_interpreter_detach(mrb_state *mrb);
extern bool mrbdbg_interpreter_init(mrbdbg_context_t *context, mrb_state *mrb, mrbdbg_init_param_t const *param);
extern void mrbdbg_interpreter_cleanup(mrbdbg_context_t context, mrb_state *mrb);
extern bool mrbdbg_interpreter_is_step_required(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs);
extern void mrbdbg_interpreter_on_step(mrbdbg_context_t context, mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs);
#endif

#ifdef __cplusplus
}
#endif

#endif /* end of MRUBY_DEBUGGER_INTERPRETER_H */

