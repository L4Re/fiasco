INTERFACE:

/**
 * The stack frame, if the kernel was entered due to a raised exception.
 */
class Trap_state
{
public:
  using Handler = FIASCO_FASTCALL int (*)(Trap_state *, Cpu_number cpu);

  /// Provide information for a page fault exception.
  void set_pagefault(Mword pfa, Mword error);

  /// Return the number of the exception.
  Mword trapno() const;

  /// Return the error code of the exception.
  Mword error() const;

  /// Return the instruction pointer where the exception was triggered.
  Mword ip() const;

  /// Modify the instruction pointer where the exception was triggered.
  void ip(Mword new_ip);

  /// Return the stack pointer when the exception was triggered.
  Mword sp() const;

  /// Return true if exceptions of this type are not added to the tracebuffer.
  bool exclude_logging() const;

  /**
   * Copy the user-visible registers of this trap state to `dst`.
   *
   * Fields that are private to the kernel, for example the kernel stack
   * pointer or a pending continuation, are cleared in `dst` instead of being
   * copied. Thus `dst` never receives any kernel state.
   *
   * Must be used instead of a plain assignment whenever a trap state is made
   * visible to user space, i.e. when it is copied into a UTCB or into the vCPU
   * state.
   *
   * This is the counterpart of copy_and_sanitize(), which imports a trap state
   * from user space and likewise leaves the kernel-private fields alone.
   */
  void copy_for_user(Trap_state *dst) const;

  /// Dump for debugging purposes.
  void dump() const;
};

/**
 * The register state transferred via UTCB to/from the exception handler.
 */
struct Trex
{
  Trap_state s;

  void set_ipc_upcall();

  /// Dump for debugging purposes.
  void dump()
  { s.dump(); }
};

