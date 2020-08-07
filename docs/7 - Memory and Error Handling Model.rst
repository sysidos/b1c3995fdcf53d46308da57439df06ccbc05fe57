.. MemoryAndErrorHandlingModel:

Swift Memory and Error Handling Notes
=====================================

The goal of this writeup is to capture ideas around error handling in APIs
(exceptions etc), and relatedly try to make it possible for swift to be used in
constrained environments (like OS kernels or firmware).

Supporting Constrained Applications
-----------------------------------

Lets start with some high level goals: we want swift to be usable as both a
highly expressive "scripting language" which is more natural to program objc
frameworks in that even ObjC itself. OTOH, we want it to scale all the way down
to writing firmware, kernels, or even dyld and 'init'. Unlike many goals, these
two contradict each other because the former use case (and almost all user space
apps these days) begs for garbage collection, but GC is a non-starter for dyld.

A reasonable implementation approach if GC isn't acceptable is to use a
(recently popularized :) ARC-style approach. However, ARC is completely
dysfunctional with exceptions, either because they cause leaks, or intense code
bloat due to having to drop all the stack references when unwinding through an
ARC frame. Fortunately, most constrained situations don't care about EH either,
would rather not burn the space on EH tables in any case, and may not have an
unwinder that makes sense.

Ok, if we go this route, we end up with two diametrically opposed implementation
approaches:

1. ARR, with no EH ("throw just calls abort"), and with [optionally] completely
   statically linked runtime libraries.

2. GC with EH and the full shebang of Cocoa integration etc.

I think that these are actually two great models to support: the first competes
head to head with C, and the second competes head to head with just about every
language that is higher in the ecosystem :).  Having two clearly different and
easy to distinguish models makes it easy to explain things. It is just important
that #1 be a subset of #2, so all code that is valid in #1 should also work
in #2. The other good thing about this is that #1 is easier to start with since
the implementation is substantially simpler and LLVM has all the needed
infrastructure. As more of the team starts working on swift-related stuff, #2
can come up in parallel. If Swift has no shared mutable state, then both GC and
ARR are more efficient (the former only has to stop one thread/actor at a time,
the later doesn't have to do atomic refcounts).

The downsides of this approach are:

1. The subset of libraries that work in both modes (e.g. the standard data
   structure library) need to be aware of cycles and other ARC issues.

2. The language has to be designed to "work" properly in both modes.  This
   primarily impacts error handling since exceptions are not around.  It has to
   be possible to handle errors naturally even when EH is off and the libraries
   should not have to "work hard" because of this. We don't want to force
   inelegant or horrible APIs because EH can't be counted on being around all
   the time.

3. We have two completely different codegen models to implement and for people
   to be aware of. There will be pressure for all "non-UI" libraries (e.g. an
   XML processing library) to be ARC safe, which eliminates some of the
   programming model wins of supporting GC in the first place.

In the long term, we want to provide a portable code representation (a
"bytecode") that can be shoveled around. Whenever we get around to implementing
this, we would want to make sure that the bytecode could codegen to either mode
(assuming that it is ARR safe).

Error Handling in Full Mode
---------------------------

Given a high level idea of what we want to shoot for, lets return to the topic
of error handling, first looking at the "full on everything goes" approach that
we'd expect most large-scale user space apps to use, certainly anything
GUI. Here we have a number of goals:

1. Exceptions are really the most natural want to handle errors in a
   non-local way.

2. RAII is really really great for deterministically releasing non- memory
   resources. Things like "unlocking locks", eagerly disposing of large
   resources ("throw away this image when the function returns even if it is
   referenced"), closing off expensive things (file descriptors, database
   connections etc). These are important when they get used, but relatively rare
   if you have GC, so "cleanups" are rare.

3. Some error conditions are *not* exceptional, and only the client knows if it
   is so. Two different clients may want different behavior: one may want
   opening a file to throw an exception (unwinding the stack) if the file
   doesn't exist, one may be probing around and have *lots* of cases where the
   file doesn't exist, which would make unwinding impractically slow.

4. We have runtime errors to deal with, such as "null pointer dereference",
   "array out of bounds" etc. I personally lean towards these being full "actor
   kill" events which run destructors on the stack (RAII) but that cannot be
   "caught and rethrown".

5. Memory allocation failures are a specific important subcategory of runtime
   failure, one which is pervasive throughout the library. The "right"
   programming model here is that allocation can never fail, unless the
   programmer is specifically checking for the error case by using a "try
   allocation" function.

6. Declaring what failures can be thrown (e.g. with a throw() clause in Java or
   C++) is generally a good idea for simple things with very well nailed down
   semantics (such as a standard library for a language), but not such a good
   idea for highly abstracted libraries like GUI libraries and cases where you
   expect tons of inheritance.  When layers build on top of layers, it is harder
   to reason about and declare what could possibly be thrown, and the value in
   this goes down quite fast.

If you accept these assumptions, then a model along these lines makes sense
(this isn't a serious syntax proposal, just something to get the ideas across),
is for all functions to default to being able to throw random exceptions, but
also allow an (opt-in!) explicit declaration of what failure can occur.

We want to provide the normal "errors throw exceptions everywhere" style of
programming by default because it leads to more natural and elegant code, as a
silly example::

  func foo1(path : string) {
    var f = File.open(path)
    f.write(...data...)
    f.close()
  }

This would be better written with File.open returning an RAII object, but I lets
ignore that, the point is that either the open or the write can throw. Given
this sort of code, the expected thing happens: if either open or write throws,
the stack is unwound. The interesting part is the declaration of open, which
would be something like this (again, actual syntax is not being proposed)::

  method File::open(path : string) -> File "or fails with" FileError

Basically, we're getting some sort of annotation saying that open can either
return an instance of FileError or return an instance of File (it is a
oneof/discriminated union). "Error"s (or "Failure"s, or ??)  we be a simple
class hierarchy which could have failure-specific information about what
happened in derived classes.

This declaration compiles to a function that *returns normally* in this
(declared) error condition (returning oneof { File, FileError }) and to another
entrypoint that calls the first and "throws" in the error case. This allows us
to efficiently either get a (declared) error result without any stack unwinding
involved, or to efficiently get an "unwind stack on error" entrypoint. The foo1
example above would call the later entrypoint.

Now lets look at a similar example locally nested in a try block:

When in a try block, e.g.::

  func foo2(path : string) {
    try {
      var f = File.open(path);
      f.write(...data...);
      f.close();
    } catch (e : Error) {
      ...
    }
  }

In this case, the compiler would generate a call to the first entrypoint, and
then we get a direct branch to the catch case. That means that if "open" throws,
that no stack unwinding actually happens.  This could similarly be generated
after inlining by the optimizer. The point here is that there is no high
overhead EH cost for these operations which *really can fail* and for which code
might want to handle their failures efficiently. Also, the programmer doesn't
have to know about this runtime optimization.

When defining a function that can fail, named failures do not involve stack
unwinding at runtime. For example, if we have::

  func foo3(path : string) "fails with" FileError {
    var f = File.open(path);
    f.write(...data...);
    f.close();
  }

foo3 would be codegen to the two-entrypoint sequence, and propagating an error
from File.open would not involve stack unwinding. If f.write threw some other
error condition, then it *would* involve stack unwinding. As mentioned before,
declared exceptions like this are really only useful for stuff low down on the
stack, at high abstraction levels, they are impractical.

Another random codegen example is::

  func foo4() {
    bar() // can throw anything.
  }

but we want to avoid generating a "catch" for::

  func foo5() "fails with" Error {
    bar()
  }

Instead, a try {} block would just catch stack unwinds and normal
error returns::

  try {
    foo5()
  } catch (e : Error) {
  }

Anyway, in summary, in "full mode" any function call can throw an arbitrary
exception, but an API that wants to declare what set of failures it can have can
do so, and this yields a performance benefit if those errors actually occur. We
also get an efficient way to do operations that fail in non-exceptional
situations, such as opening a file that doesn't exist.

Error Handling in Constrained Mode
----------------------------------

Constrained mode is intended for use by low-level clients that (for example)
don't need full UI support, and don't need highly layered APIs that evolve
rapidly over time. The idea is to use this mode in cases where C really is a
good answer, things like kernels, and other super low level stuff. Just because
they are low level though, it doesn't mean that failures can't happen, it is
just that stack unwinding can't be used as the answer for propagating them.

The approach to take here is that (when building code in constrained mode) stack
unwinding can never happen, that all failures have to be explicitly declared,
and that all failures have to be locally handled.  This means that:

1. foo1 is not allowed (is a compile-time error), because the errors raised by
   open are not locally handled and foo1 does not propagate the error
   explicitly.

2. foo2 is allowed, because the errors are locally handled.

3. foo3 is allowed, because the errors are propagated to the caller.

4. foo4 and foo5 are allowed, because bar can never fail if it doesn't
   have any declared exceptions.

5. Similarly, simple functions like this are not allowed::

     fun foo6() { throw IntError(42) }

6. but this is::

     fun foo6() "fails with" IntError { throw IntError(42) }

I think that this set of tradeoffs is acceptable, because particularly in these
low-level situations, the sets of failures that can happen are simple and
predictable.
