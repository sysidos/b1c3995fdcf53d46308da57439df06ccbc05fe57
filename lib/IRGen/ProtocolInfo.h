//===--- ProtocolInfo.h - Abstract protocol witness layout ------*- C++ -*-===//
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
// This file defines types for representing the abstract layout of a
// protocol.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_IRGEN_PROTOCOLINFO_H
#define SWIFT_IRGEN_PROTOCOLINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ArrayRef.h"
#include "ValueWitness.h"

namespace swift {
  class CanType;
  class Decl;
  class ProtocolConformance;
  class ProtocolDecl;

namespace irgen {
  class ConformanceInfo; // private to GenProto.cpp
  class IRGenModule;
  class TypeInfo;

/// A class which encapsulates an index into a witness table.
class WitnessIndex {
  unsigned Value;
public:
  WitnessIndex() = default;
  WitnessIndex(ValueWitness index) : Value(unsigned(index)) {}
  explicit WitnessIndex(unsigned index) : Value(index) {}

  unsigned getValue() const { return Value; }

  bool isZero() const { return Value == 0; }
  bool isValueWitness() const { return Value < NumValueWitnesses; }
};

/// A witness to a specific element of a protocol.  Every
/// ProtocolTypeInfo stores one of these for each declaration in the
/// protocol.
/// 
/// The structure of a witness varies by the type of declaration:
///   - a function requires a single witness, the function;
///   - a variable requires two witnesses, a getter and a setter;
///   - a subscript requires two witnesses, a getter and a setter;
///   - a type requires a pointer to a witness for that type and
///     the protocols it obeys.
class WitnessTableEntry {
  Decl *Member;
  WitnessIndex BeginIndex;

 WitnessTableEntry(Decl *member, WitnessIndex begin)
   : Member(member), BeginIndex(begin) {}

public:
  WitnessTableEntry() = default;

  Decl *getMember() const {
    return Member;
  }

  static WitnessTableEntry forPrefixBase(ProtocolDecl *proto) {
    return WitnessTableEntry(proto, WitnessIndex(0));
  }

  static WitnessTableEntry forOutOfLineBase(ProtocolDecl *proto,
                                            WitnessIndex index) {
    assert(!index.isValueWitness());
    return WitnessTableEntry(proto, index);
  }

  /// Is this a base-protocol entry?
  bool isBase() const { return isa<ProtocolDecl>(Member); }

  /// Is the table for this base-protocol entry "out of line",
  /// i.e. there is a witness which indirectly points to it, or is
  /// it a prefix of the layout of this protocol?
  bool isOutOfLineBase() const {
    assert(isBase());
    return !BeginIndex.isZero();
  }

  /// Return the index at which to find the table for this
  /// base-protocol entry.
  WitnessIndex getOutOfLineBaseIndex() const {
    assert(isOutOfLineBase());
    return BeginIndex;
  }

  static WitnessTableEntry forFunction(FuncDecl *func, WitnessIndex index) {
    assert(!index.isValueWitness());
    return WitnessTableEntry(func, index);
  }

  bool isFunction() const { return isa<FuncDecl>(Member); }

  WitnessIndex getFunctionIndex() const {
    assert(isFunction());
    return BeginIndex;
  }
};

/// An abstract description of a protocol.
class ProtocolInfo {
  /// A singly-linked-list of all the protocols that have been laid out.
  const ProtocolInfo *NextConverted;
  friend class TypeConverter;

  /// The number of witnesses in the protocol.
  unsigned NumWitnesses;

  /// The number of table entries in this protocol layout.
  unsigned NumTableEntries;

  /// A table of all the conformances we've needed so far for this
  /// protocol.  We expect this to be quite small for most protocols.
  mutable llvm::SmallDenseMap<const ProtocolConformance*, ConformanceInfo*, 2>
    Conformances;

  WitnessTableEntry *getEntriesBuffer() {
    return reinterpret_cast<WitnessTableEntry*>(this+1);
  }
  const WitnessTableEntry *getEntriesBuffer() const {
    return reinterpret_cast<const WitnessTableEntry*>(this+1);
  }

  ProtocolInfo(unsigned numWitnesses, llvm::ArrayRef<WitnessTableEntry> table)
    : NumWitnesses(numWitnesses), NumTableEntries(table.size()) {
    std::memcpy(getEntriesBuffer(), table.data(),
                sizeof(WitnessTableEntry) * table.size());
  }

  static ProtocolInfo *create(unsigned numWitnesses,
                              llvm::ArrayRef<WitnessTableEntry> table);

public:
  const ConformanceInfo &getConformance(IRGenModule &IGM,
                                        CanType concreteType,
                                        const TypeInfo &concreteTI,
                                        ProtocolDecl *protocol,
                                        const ProtocolConformance &conf) const;

  unsigned getNumTableEntries() const {
    return NumTableEntries;
  }

  llvm::ArrayRef<WitnessTableEntry> getWitnessEntries() const {
    return llvm::ArrayRef<WitnessTableEntry>(getEntriesBuffer(),
                                             getNumTableEntries());
  }

  const WitnessTableEntry &getWitnessEntry(Decl *member) const {
    // FIXME: do a binary search if the number of witnesses is large
    // enough.
    for (auto &witness : getWitnessEntries())
      if (witness.getMember() == member)
        return witness;
    llvm_unreachable("didn't find entry for member!");
  }

  ~ProtocolInfo();
};

} // end namespace irgen
} // end namespace swift

#endif
