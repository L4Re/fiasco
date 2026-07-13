
/*
 * Timeslice infrastructure
 */

INTERFACE [sched_fixed_prio]:

#include <cxx/dlist>
#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include "ready_queue_fp.h"


class Sched_context : public cxx::D_list_item
{
  MEMBER_OFFSET();
  friend class Jdb_list_timeouts;
  friend class Jdb_thread_list;
  friend class Sched_ctxts_test;
  friend class Scheduler_test;

  template<typename T>
  friend struct Jdb_thread_list_policy;

public:
  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

  typedef cxx::Sd_list<Sched_context> Fp_list;

  class Ready_queue_base : public Ready_queue_fp<Sched_context>
  {
  public:
    void activate(Sched_context *s)
    { _current_sched = s; }
    Sched_context *current_sched() const { return _current_sched; }

  private:
    Sched_context *_current_sched;
  };

  Context *context() const { return context_of(this); }

private:
  unsigned short _prio;

  friend class Ready_queue_fp<Sched_context>;
};

//----------------------------------------------------------------------------
INTERFACE [sched_fixed_prio && prio_inherit]:

EXTENSION class Sched_context
{
public:
  inline unsigned short pi_regular_prio() const
  { return _pi_regular_prio; }

  inline unsigned short pi_effective_prio() const
  { return _pi_effective_prio; }

  inline void set_pi_effective_prio(unsigned short new_prio)
  { _pi_effective_prio = new_prio; }

private:
  /// Regular priority of the Sched_context's thread.
  unsigned short _pi_regular_prio = Config::Default_prio;

  /**
   * Effective priority of the Sched_context's thread.
   *
   * Defined as the maximum of its regular priority and the effective priority
   * of all its predecessors in the PI chain. In other words the transitive set
   * of all threads that wait on a mutex owned by that thread.
   */
  unsigned short _pi_effective_prio = Config::Default_prio;
};

//----------------------------------------------------------------------------
INTERFACE [sched_fixed_prio]:

EXTENSION class Sched_context
{
private:
  Unsigned64 _quantum;
  Unsigned64 _left;
};

//----------------------------------------------------------------------------
IMPLEMENTATION [sched_fixed_prio]:

#include <cassert>
#include "cpu_lock.h"
#include "std_macros.h"
#include "config.h"


/**
 * Constructor
 */
PUBLIC
Sched_context::Sched_context()
: _prio(Config::Default_prio),
  _quantum(Config::default_time_slice()),
  _left(Config::default_time_slice())
{}


/**
 * Return priority of Sched_context
 */
PUBLIC inline
unsigned short
Sched_context::prio() const
{
  return _prio;
}

/**
 * Set priority of Sched_context
 *
 * \pre Sched_context must not be enqueued in ready queue.
 */
PUBLIC inline
void
Sched_context::set_prio(unsigned short prio)
{
  _prio = prio;
}

PUBLIC static inline
Mword
Sched_context::sched_classes()
{
  return 1UL << (-L4_sched_param_fixed_prio::Class);
}

PUBLIC static constexpr
unsigned
Sched_context::max_param_size()
{
  return sizeof(Sp);
}

PUBLIC static inline NEEDS["config.h"]
int
Sched_context::check_param(L4_sched_param const *_p)
{
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  switch (p->p.sched_class)
    {
    case L4_sched_param_fixed_prio::Class:
      if (!_p->check_length<L4_sched_param_fixed_prio>())
        return -L4_err::EInval;
      if (p->fixed_prio.prio <= Config::Kernel_prio)
        return -L4_err::EInval;
      break;

    default:
      if (!_p->is_legacy())
        return -L4_err::ERange;
      if (p->legacy_fixed_prio.prio <= Smword{Config::Kernel_prio})
        return -L4_err::EInval;
      break;
    }

  return 0;
}

PUBLIC
void
Sched_context::get(Sp *p) const
{
  p->fixed_prio = L4_sched_param_fixed_prio{};
  p->fixed_prio.sched_class = L4_sched_param_fixed_prio::Class;
  p->fixed_prio.length = sizeof(L4_sched_param_fixed_prio);
  p->fixed_prio.quantum = _quantum;
  p->fixed_prio.prio = regular_prio();
}

PUBLIC
void
Sched_context::set(L4_sched_param const *_p)
{
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  if (_p->is_legacy())
    {
      // legacy fixed prio
      _prio = p->legacy_fixed_prio.prio;
      if (p->legacy_fixed_prio.prio > 255)
        _prio = 255;

      _quantum = p->legacy_fixed_prio.quantum;
      if (p->legacy_fixed_prio.quantum == 0)
        _quantum = Config::default_time_slice();
      sync_pi_prio();
      return;
    }

  switch (p->p.sched_class)
    {
    case L4_sched_param_fixed_prio::Class:
      _prio = p->fixed_prio.prio;
      if (p->fixed_prio.prio > 255)
        _prio = 255;

      _quantum = p->fixed_prio.quantum;
      if (p->fixed_prio.quantum == 0)
        _quantum = Config::default_time_slice();
      sync_pi_prio();
      break;

    default:
      assert(false && "Missing check_param()?");
      break;
    }
}

/**
 * Return remaining time quantum of Sched_context
 */
PUBLIC inline
Unsigned64
Sched_context::left() const
{
  return _left;
}

PUBLIC inline NEEDS[Sched_context::set_left]
void
Sched_context::replenish()
{ set_left(_quantum); }

/**
 * Set remaining time quantum of Sched_context
 */
PUBLIC inline
void
Sched_context::set_left(Unsigned64 left)
{
  _left = left;
}


/**
 * Check if Context is in ready-list.
 * @return 1 if thread is in ready-list, 0 otherwise
 */
PUBLIC inline
bool
Sched_context::in_ready_list() const
{
  return Fp_list::in_list(this);
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{ return prio() > sc->prio(); }


//----------------------------------------------------------------------------
IMPLEMENTATION [sched_fixed_prio && !prio_inherit]:

PRIVATE inline
unsigned short
Sched_context::regular_prio() const
{
  return _prio;
}

PRIVATE inline
void
Sched_context::sync_pi_prio()
{}

//----------------------------------------------------------------------------
IMPLEMENTATION [sched_fixed_prio && prio_inherit]:

PRIVATE inline
unsigned short
Sched_context::regular_prio() const
{
  return _pi_regular_prio;
}

PRIVATE
void
Sched_context::sync_pi_prio()
{
  auto old_regular_prio = _pi_regular_prio;

  // Update regular prio of thread.
  _pi_regular_prio = _prio;

  // TODO: Changing prio of mutex that owns or blocks on PI mutex is currently
  //       not supported, i.e. it might mess up priorities in PI chain. To
  //       properly support this we would need to propagate the changed priority
  //       through the PI chain.

  // We do a heuristic check here, i.e. without holding the PI chain lock, of
  // whether the context is engaged in PI to avoid messing up PI chain
  // priorities on a best effort basis.
  if (old_regular_prio != _pi_effective_prio)
    {
      // If thread is engaged with PI mutex, avoid dropping below effective prio.
      if (_prio < _pi_effective_prio)
        _prio = _pi_effective_prio;
    }
  else
    {
      // If thread is not engaged with PI mutex, keep effective and regular prio
      // in sync.
      _pi_effective_prio = _pi_regular_prio;
    }
}
