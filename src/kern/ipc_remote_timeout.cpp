INTERFACE:

#include "timeout.h"

class Receiver;

class IPC_remote_timeout : public Timeout
{
  friend class Timeouts_test;
};

//----------------------------------------------------------------------------
IMPLEMENTATION:

#include "context.h"
#include "receiver.h"
#include "thread_state.h"

PUBLIC inline
IPC_remote_timeout::IPC_remote_timeout()
{}

PUBLIC virtual inline NEEDS [IPC_remote_timeout::owner, "receiver.h"]
IPC_remote_timeout::~IPC_remote_timeout()
{
  owner()->set_timeout(nullptr); // reset owner's timeout field
}

PRIVATE inline
Receiver *
IPC_remote_timeout::owner()
{
  // We could have saved our context in our constructor, but computing
  // it this way is easier and saves space. We can do this as we know
  // that IPC_remote_timeouts are always created on the kernel stack of the
  // owner context.
  return reinterpret_cast<Receiver *>(context_of(this));
}

/**
 * Timeout expiration callback function
 * \retval Reschedule::Yes if a reschedule is necessary
 * \retval Reschedule::No  if no reschedule is necessary
 */
PRIVATE
Reschedule
IPC_remote_timeout::expired() override
{
  Context *owner = context_of(this);
  if (!owner->abort_drq())
    return Reschedule::No;

  owner->state_change_dirty(~Thread_drq_wait, Thread_ready);
  owner->utcb().access()->error = L4_error::Timeout;
  // Flag reschedule if owner's priority is higher than the current
  // thread's (own or timeslice-donated) priority.
  Sched_context *cur_ctx = current()->sched();
  return Sched_context::rq.current().deblock(owner->sched(), cur_ctx, false);
}
