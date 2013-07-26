#include "debugger.h"

#ifdef ENABLE_DEBUG
typedef void (*mrb_code_fetch_hook)(struct mrb_state* mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs);

static mrbdbg_debugger_t *s_debugger = NULL;
static mrbdbg_context_t  s_context = NULL;
static mrb_code_fetch_hook s_prev_hook = (mrb_code_fetch_hook)NULL;
static bool s_is_activated = false;

static void
register_previous_hook(mrb_state *mrb, mrb_code_fetch_hook hook)
{
  s_prev_hook = hook;
}

static mrb_code_fetch_hook
unregister_previous_hook(mrb_state *mrb)
{
  mrb_code_fetch_hook hook = s_prev_hook;
  s_prev_hook = (mrb_code_fetch_hook)NULL;
  return hook;
}

static void
invoke_previous_hook(mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  if((mrb_code_fetch_hook)NULL == s_prev_hook) {
    return;
  }
  s_prev_hook(mrb, irep, pc, regs);
}

static void
mrbdbg_code_fetch_hook(mrb_state* mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  mrbdbg_debugger_t * const debugger = s_debugger;
  if (NULL != debugger) {
    if (false != debugger->is_step_required(s_context, mrb, irep, pc, regs)) {
      debugger->on_step(s_context, mrb, irep, pc, regs);
    }
  }
  invoke_previous_hook(mrb, irep, pc, regs);
}

static bool
is_activated(mrb_state *mrb)
{
  return s_is_activated;
}

static void
activated(mrb_state *mrb)
{
  s_is_activated = true;
}
static void
inactivated(mrb_state *mrb)
{
  s_is_activated = false;
}

static bool
register_debugger(mrb_state *mrb, mrbdbg_debugger_t *debugger)
{
  if (NULL == debugger) {
    return false;
  }

  if (NULL != s_debugger) {
    return false;
  }

  s_debugger = (mrbdbg_debugger_t*)mrb_malloc(mrb, sizeof(mrbdbg_debugger_t));
  if (NULL == s_debugger) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "insufficient memory.");
  }

  *s_debugger = *debugger;

  return true;
}

static void
unregister_debugger(mrb_state *mrb)
{
  if (NULL == s_debugger) {
    return;
  }

  mrb_free(mrb, s_debugger);
  s_debugger = NULL;
}

#endif

static mrb_value
mrb_debugger_start(mrb_state *mrb, mrb_value self)
{
#ifdef ENABLE_DEBUG
  if (false != is_activated(mrb)) {
    return mrb_true_value();
  }

  mrbdbg_debugger_t debugger_api;
  if (false == mrbdbg_debugger_activate(mrb, NULL, &debugger_api)) {
    return mrb_false_value();
  }

  if (false == register_debugger(mrb, &debugger_api)) {
    return mrb_false_value();
  }

  activated(mrb);

  return mrb_true_value();
#else
  return mrb_false_value();
#endif
}

static mrb_value
mrb_debugger_stop(mrb_state *mrb, mrb_value self)
{
#ifdef ENABLE_DEBUG
  if (false == is_activated(mrb)) {
    return mrb_nil_value();
  }

  mrbdbg_debugger_t *debugger = s_debugger;
  if (NULL != debugger) {
    if (false != debugger->is_initialized(mrb)) {
      debugger->cleanup(s_context, mrb);
    }
    if (false != debugger->is_attached(mrb)) {
      debugger->detach(mrb);
    }
  }

  mrbdbg_debugger_inactivate(mrb, NULL);

  inactivated(mrb);

  unregister_debugger(mrb);
#endif
  return mrb_nil_value();
}

static mrb_value
mrb_debugger_delegate(mrb_state *mrb, mrb_value self)
{
#ifdef ENABLE_DEBUG
  mrbdbg_debugger_t * const debugger = s_debugger;
  if (NULL == debugger) {
    return mrb_false_value();
  }

  if (false == debugger->is_attached(mrb)) {
    if (false == debugger->attach(mrb, NULL)) {
      return mrb_false_value();
    }
  }

  if (false == debugger->is_initialized(mrb)) {
    if (false == debugger->init(&s_context, mrb, NULL)) {
      mrb_raise(mrb, E_RUNTIME_ERROR, "debugger initialization failed.");
    }
  }

#if 0
  if (false != debugger->is_step_required(s_context, mrb, NULL, NULL, NULL)) {
    debugger->on_step(s_context, mrb, NULL, NULL, NULL);
  }
#endif

  return mrb_true_value();
#else
  return mrb_false_value();
#endif
}

void
mrb_mruby_debugger_gem_init(mrb_state* mrb)
{
  struct RClass *class;
  class = mrb_define_class(mrb, "Debugger", NULL);
  mrb_define_class_method(mrb, class, "start",    mrb_debugger_start,    ARGS_NONE());
  mrb_define_class_method(mrb, class, "stop",     mrb_debugger_stop,     ARGS_NONE());
  mrb_define_class_method(mrb, class, "debugger", mrb_debugger_delegate, ARGS_NONE());

#ifdef ENABLE_DEBUG
  register_previous_hook(mrb, mrb->code_fetch_hook);
  mrb->code_fetch_hook = mrbdbg_code_fetch_hook;
#endif
}

void
mrb_mruby_debugger_gem_final(mrb_state* mrb)
{
#ifdef ENABLE_DEBUG
  mrb->code_fetch_hook = unregister_previous_hook(mrb);
#endif
}
