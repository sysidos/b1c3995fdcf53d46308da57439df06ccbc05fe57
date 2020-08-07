//===- DiagnosticEngine.h - Diagnostic Display Engine -----------*- C++ -*-===//
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
//  This file declares the DiagnosticEngine class, which manages any diagnostics
//  emitted by Swift.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_BASIC_DIAGNOSTICENGINE_H
#define SWIFT_BASIC_DIAGNOSTICENGINE_H

#include "swift/Basic/LLVM.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/Type.h"
#include "swift/Basic/DiagnosticConsumer.h"
#include "swift/Basic/Optional.h"
#include "swift/Basic/SourceLoc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>

namespace llvm {
  class SourceMgr;
}

namespace swift {
  using llvm::ArrayRef;
  using llvm::StringRef;
  class DiagnosticEngine;
  
  /// \brief Enumeration describing all of possible diagnostics.
  ///
  /// Each of the diagnostics described in Diagnostics.def has an entry in
  /// this enumeration type that uniquely identifies it.
  enum class DiagID : unsigned {
#define DIAG(KIND,ID,Category,Options,Text,Signature) ID,
  #include "Diagnostics.def"
  };

  /// \brief Describes a diagnostic along with its argument types.
  ///
  /// The diagnostics header introduces instances of this type for each 
  /// diagnostic, which provide both the set of argument types (used to
  /// check/convert the arguments at each call site) and the diagnostic ID
  /// (for other information about the diagnostic).
  template<typename ...ArgTypes>
  struct Diag {
    /// \brief The diagnostic ID corresponding to this diagnostic.
    DiagID ID;
  };

  namespace detail {
    /// \brief Describes how to pass a diagnostic argument of the given type.
    ///
    /// By default, diagnostic arguments are passed by value, because they
    /// tend to be small. Larger diagnostic arguments
    /// need to specialize this class template to pass by reference.
    template<typename T>
    struct PassArgument {
      typedef T type;
    };
  }
    
  /// \brief Describes the kind of diagnostic argument we're storing.
  ///
  enum class DiagnosticArgumentKind {
    String,
    Integer,
    Unsigned,
    Identifier,
    Type
  };

  /// \brief Variant type that holds a single diagnostic argument of a known
  /// type.
  ///
  /// All diagnostic arguments are converted to an instance of this class.
  class DiagnosticArgument {
    DiagnosticArgumentKind Kind;
    union {
      int IntegerVal;
      unsigned UnsignedVal;
      StringRef StringVal;
      Identifier IdentifierVal;
      Type TypeVal;
    };
    
  public:
    DiagnosticArgument(StringRef S)
      : Kind(DiagnosticArgumentKind::String), StringVal(S) {
    }

    DiagnosticArgument(int I) 
      : Kind(DiagnosticArgumentKind::Integer), IntegerVal(I) {
    }

    DiagnosticArgument(unsigned I) 
      : Kind(DiagnosticArgumentKind::Unsigned), UnsignedVal(I) {
    }

    DiagnosticArgument(Identifier I)
      : Kind(DiagnosticArgumentKind::Identifier), IdentifierVal(I) {
    }

    DiagnosticArgument(Type T)
      : Kind(DiagnosticArgumentKind::Type), TypeVal(T) {
    }

    DiagnosticArgumentKind getKind() const { return Kind; }

    StringRef getAsString() const {
      assert(Kind == DiagnosticArgumentKind::String);
      return StringVal;
    }

    int getAsInteger() const {
      assert(Kind == DiagnosticArgumentKind::Integer);
      return IntegerVal;
    }

    unsigned getAsUnsigned() const {
      assert(Kind == DiagnosticArgumentKind::Unsigned);
      return UnsignedVal;
    }

    Identifier getAsIdentifier() const {
      assert(Kind == DiagnosticArgumentKind::Identifier);
      return IdentifierVal;
    }

    Type getAsType() const {
      assert(Kind == DiagnosticArgumentKind::Type);
      return TypeVal;
    }
  };
  
  /// Diagnostic - This is a specific instance of a diagnostic along with all of
  /// the DiagnosticArguments that it requires. 
  class Diagnostic {
    DiagID ID;
    SmallVector<DiagnosticArgument, 3> Args;
    SmallVector<SourceRange, 2> Ranges;
    
  public:
    // All constructors are intentionally implicit.
    template<typename ...ArgTypes>
    Diagnostic(Diag<ArgTypes...> ID,
               typename detail::PassArgument<ArgTypes>::type... VArgs)
      : ID(ID.ID) {
      DiagnosticArgument DiagArgs[] = { 
        DiagnosticArgument(0), std::move(VArgs)... 
      };
      Args.append(DiagArgs + 1, DiagArgs + 1 + sizeof...(VArgs));
    }

    /*implicit*/Diagnostic(DiagID ID, ArrayRef<DiagnosticArgument> Args)
      : ID(ID), Args(Args.begin(), Args.end()) {}
    
    // Accessors.
    DiagID getID() const { return ID; }
    ArrayRef<DiagnosticArgument> getArgs() const { return Args; }
    ArrayRef<SourceRange> getRanges() const { return Ranges; }
    
    Diagnostic &operator<<(SourceRange R) {
      Ranges.push_back(R);
      return *this;
    }
  };
  
  /// \brief Describes an in-flight diagnostic, which is currently active
  /// within the diagnostic engine and can be augmented within additional
  /// information (source ranges, Fix-Its, etc.).
  ///
  /// Only a single in-flight diagnostic can be active at one time, and all
  /// additional information must be emitted through the active in-flight
  /// diagnostic.
  class InFlightDiagnostic {
    friend class DiagnosticEngine;
    
    DiagnosticEngine *Engine;
    bool IsActive;
    
    /// \brief Create a new in-flight diagnostic. 
    ///
    /// This constructor is only available to the DiagnosticEngine.
    InFlightDiagnostic(DiagnosticEngine &Engine)
      : Engine(&Engine), IsActive(true) { }
    
    InFlightDiagnostic(const InFlightDiagnostic &) = delete;
    InFlightDiagnostic &operator=(const InFlightDiagnostic &) = delete;
    InFlightDiagnostic &operator=(InFlightDiagnostic &&) = delete;

  public:
    /// \brief Create an active but unattached in-flight diagnostic.
    /// 
    /// The resulting diagnostic can be used as a dummy, accepting the
    /// syntax to add additional information to a diagnostic without
    /// actually emitting a diagnostic.
    InFlightDiagnostic() : Engine(0), IsActive(true) { }
    
    /// \brief Transfer an in-flight diagnostic to a new object, which is
    /// typically used when returning in-flight diagnostics.
    InFlightDiagnostic(InFlightDiagnostic &&Other)
      : Engine(Other.Engine), IsActive(Other.IsActive) {
      Other.IsActive = false;
    }
    
    ~InFlightDiagnostic() {
      if (IsActive)
        flush();
    }
    
    /// \brief Flush the active diagnostic to the diagnostic output engine.
    void flush();
    
    /// \brief Add a source range to the currently-active diagnostic.
    InFlightDiagnostic &operator<<(SourceRange R);
  };
    
  /// \brief Class responsible for formatting diagnostics and presenting them
  /// to the user.
  class DiagnosticEngine {
    /// \brief The source manager used to interpret source locations and
    /// display diagnostics.
    llvm::SourceMgr &SourceMgr;

    /// \brief The diagnostic consumer that will be responsible for actually
    /// emitting diagnostics.
    DiagnosticConsumer &Consumer;
    
    /// HadAnyError - True if any error diagnostics have been emitted.
    bool HadAnyError;

    /// \brief The source location of the currently active diagnostic, if there
    /// is one.
    SourceLoc ActiveDiagnosticLoc;
    
    /// \brief The currently active diagnostic, if there is one.
    Optional<Diagnostic> ActiveDiagnostic;
    
    friend class InFlightDiagnostic;
    
  public:
    explicit DiagnosticEngine(llvm::SourceMgr &SourceMgr, 
                              DiagnosticConsumer &Consumer)
      : SourceMgr(SourceMgr), Consumer(Consumer), HadAnyError(false), 
        ActiveDiagnostic() { }

    /// hadAnyError - return true if any *error* diagnostics have been emitted.
    bool hadAnyError() const {
      return HadAnyError;
    }

    void resetHadAnyError() {
      HadAnyError = false;
    }

    /// \brief Emit a diagnostic using a preformatted array of diagnostic
    /// arguments.
    ///
    /// \param Loc The location to which the diagnostic refers in the source
    /// code.
    ///
    /// \param ID The diagnostic ID.
    ///
    /// \param Args The preformatted set of diagnostic arguments. The caller
    /// must ensure that the diagnostic arguments have the appropriate type.
    ///
    /// \returns An in-flight diagnostic, to which additional information can
    /// be attached.
    InFlightDiagnostic diagnose(SourceLoc Loc, DiagID ID, 
                                ArrayRef<DiagnosticArgument> Args) {
      assert(!ActiveDiagnostic && "Already have an active diagnostic");
      ActiveDiagnosticLoc = Loc;
      ActiveDiagnostic = Diagnostic(ID, Args);
      return InFlightDiagnostic(*this);
    }

    /// \brief Emit an already-constructed diagnostic at the given location.
    ///
    /// \param Loc The location to which the diagnostic refers in the source
    /// code.
    ///
    /// \param D The diagnostic.
    ///
    /// \returns An in-flight diagnostic, to which additional information can
    /// be attached.
    InFlightDiagnostic diagnose(SourceLoc Loc, const Diagnostic &D) {
      assert(!ActiveDiagnostic && "Already have an active diagnostic");
      ActiveDiagnosticLoc = Loc;
      ActiveDiagnostic = D;
      return InFlightDiagnostic(*this);
    }
    
    /// \brief Emit a diagnostic with the given set of diagnostic arguments.
    ///
    /// \param Loc The location to which the diagnostic refers in the source
    /// code.
    ///
    /// \param ID The diagnostic to be emitted.
    ///
    /// \param Args The diagnostic arguments, which will be converted to
    /// the types expected by the diagnostic \p ID.
    template<typename ...ArgTypes>
    InFlightDiagnostic 
    diagnose(SourceLoc Loc, Diag<ArgTypes...> ID,
             typename detail::PassArgument<ArgTypes>::type... Args) {
      assert(!ActiveDiagnostic && "Already have an active diagnostic");
      ActiveDiagnosticLoc = Loc;
      ActiveDiagnostic = Diagnostic(ID, std::move(Args)...);
      return InFlightDiagnostic(*this);
    }  
       
  private:
    /// \brief Flush the active diagnostic.
    void flushActiveDiagnostic();
    
    /// \brief Retrieve the active diagnostic.
    Diagnostic &getActiveDiagnostic() { return *ActiveDiagnostic; }
  };
  
  inline InFlightDiagnostic &InFlightDiagnostic::operator<<(SourceRange R) {
    assert(IsActive && "Cannot modify an inactive diagnostic");
    if (Engine)
      Engine->getActiveDiagnostic() << R;
    return *this;
  }
} // end namespace swift

#endif
