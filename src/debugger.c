#include "debugger.h"
#include "interpreter.h"

bool
mrbdbg_debugger_activate(mrb_state *mrb, mrbdbg_debugger_activator_t *activator, mrbdbg_debugger_t *debugger)
{
  if (NULL == mrb) {
    return false;
  }
  if (NULL == debugger) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "debugger API interface is not present.");
    /* DO NOT reach here. */
  }

  /* 'activator' is ignored currently. */

#ifdef ENABLE_STDIO
  debugger->is_attached       = mrbdbg_interpreter_is_attached;
  debugger->is_initialized    = mrbdbg_interpreter_is_initialized;
  debugger->attach            = mrbdbg_interpreter_attach;
  debugger->detach            = mrbdbg_interpreter_detach;
  debugger->init              = mrbdbg_interpreter_init;
  debugger->cleanup           = mrbdbg_interpreter_cleanup;
  debugger->is_step_required  = mrbdbg_interpreter_is_step_required;
  debugger->on_step           = mrbdbg_interpreter_on_step;

  return true;
#else
  return false;
#endif
}

void
mrbdbg_debugger_inactivate(mrb_state *mrb, mrbdbg_debugger_activator_t *activator)
{
}

