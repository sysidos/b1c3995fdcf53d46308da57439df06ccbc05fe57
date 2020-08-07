//===--- IndirectTypeInfo.h - Convenience for indirected types --*- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines IndirectTypeInfo, which is a convenient abstract
// implementation of TypeInfo for working with types that are always
// passed or returned indirectly.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_INDIRECTTYPEINFO_H
#define SWIFT_IRGEN_INDIRECTTYPEINFO_H

#include "Explosion.h"
#include "GenInit.h"
#include "TypeInfo.h"
#include "IRGenFunction.h"

namespace swift {
namespace irgen {

/// IndirectTypeInfo - An abstract class designed for use when
/// implementing a type which is always passed indirectly.
///
/// Subclasses must implement the following operations:
///   OwnedAddress allocate(IRGenFunction &IGF, Initialization &init, Object obj,
///                         OnHeap_t onHeap, const llvm::Twine &name) const;
///   void assignWithCopy(IRGenFunction &IGF, Address dest, Address src) const;
///   void initializeWithCopy(IRGenFunction &IGF, Address dest, Address src) const;
///   void destroy(IRGenFunction &IGF, Address obj) const;
template <class Derived, class Base>
class IndirectTypeInfo : public Base {
protected:
  template <class... T> IndirectTypeInfo(T &&...args)
    : Base(::std::forward<T>(args)...) {}

  const Derived &asDerived() const {
    return static_cast<const Derived &>(*this);
  }

public:
  void getSchema(ExplosionSchema &schema) const {
    schema.add(ExplosionSchema::Element::forAggregate(this->getStorageType(),
                                                      this->StorageAlignment));
  }

  unsigned getExplosionSize(ExplosionKind kind) const { return 1; }

  void initializeWithTake(IRGenFunction &IGF, Address dest, Address src) const {
    IGF.emitMemCpy(dest, src, this->Base::StorageSize);
  }

  void load(IRGenFunction &IGF, Address src, Explosion &out) const {
    // Create a temporary.
    Initialization init;
    auto temp = init.getObjectForTemporary();
    auto cleanup = init.registerObject(IGF, temp, NotOnHeap, *this);
    Address dest = asDerived().Derived::allocate(IGF, init, temp, NotOnHeap,
                                                 "temporary.forLoad");

    // Initialize it with a copy of the source.
    asDerived().Derived::initializeWithCopy(IGF, dest, src);
    init.markInitialized(IGF, temp);

    // Enter a cleanup for the temporary.
    out.add(ManagedValue(dest.getAddress(), cleanup));
  }

  void loadAsTake(IRGenFunction &IGF, Address src, Explosion &out) const {
    // Create a temporary and memcpy into it, then enter a cleanup
    // to destroy that.
    Initialization init;
    auto temp = init.getObjectForTemporary();
    auto cleanup = init.registerObject(IGF, temp, NotOnHeap, *this);
    Address dest = asDerived().Derived::allocate(IGF, init, temp, NotOnHeap,
                                                 "temporary.forLoad");

    // Initialize it with a take of the source.
    asDerived().Derived::initializeWithTake(IGF, dest, src);
    init.markInitialized(IGF, temp);

    // Enter a cleanup for the temporary.
    out.add(ManagedValue(dest.getAddress(), cleanup));
  }

  void assign(IRGenFunction &IGF, Explosion &in, Address dest) const {
    // Destroy the old value.  This is safe because the value in the
    // explosion is already +1, so even if there's any aliasing
    // going on, we're totally fine.
    asDerived().Derived::destroy(IGF, dest);

    // Take the new value.
    asDerived().Derived::initialize(IGF, in, dest);
  }

  void assignWithTake(IRGenFunction &IGF, Address dest, Address src) const {
    asDerived().Derived::destroy(IGF, dest);
    asDerived().Derived::initializeWithTake(IGF, dest, src);
  }

  void initialize(IRGenFunction &IGF, Explosion &in, Address dest) const {
    // Take ownership of the temporary and memcpy it into place.
    Address src(in.forwardNext(IGF), this->StorageAlignment);
    asDerived().Derived::initializeWithTake(IGF, dest, src);
  }

  void reexplode(IRGenFunction &IGF, Explosion &src, Explosion &dest) const {
    dest.add(src.claimNext());
  }

  void copy(IRGenFunction &IGF, Explosion &in, Explosion &out) const {
    auto srcManaged = in.claimNext();
    Address src(srcManaged.getValue(), this->StorageAlignment);
    asDerived().Derived::load(IGF, src, out);
    // Force the cleanup here?
  }

  void manage(IRGenFunction &IGF, Explosion &in, Explosion &out) const {
    Address obj(in.claimUnmanagedNext(), this->StorageAlignment);
    if (asDerived().Derived::isPOD(ResilienceScope::Local)) {
      out.addUnmanaged(obj.getAddress());
    } else {
      IGF.enterDestroyCleanup(obj, *this, out);
    }
  }
};

}
}

#endif
