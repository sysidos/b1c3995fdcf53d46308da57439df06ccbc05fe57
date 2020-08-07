//===--- Decl.h - Swift Language Declaration ASTs ---------------*- C++ -*-===//
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
// This file defines the Decl class and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_DECL_H
#define SWIFT_DECL_H

#include "swift/AST/DeclContext.h"
#include "swift/AST/Identifier.h"
#include "swift/AST/Type.h"
#include "swift/AST/TypeLoc.h"
#include "swift/Basic/SourceLoc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <cstddef>

namespace swift {
  class ArchetypeType;
  class ASTContext;
  class ASTWalker;
  class Type;
  class Expr;
  class FuncDecl;
  class FuncExpr;
  class BraceStmt;
  class Component;
  class DeclAttributes;
  class OneOfElementDecl;
  class NameAliasType;
  class Pattern;
  class ProtocolType;
  enum class Resilience : unsigned char;
  class TypeAliasDecl;
  class Stmt;
  class ValueDecl;
  
enum class DeclKind {
#define DECL(Id, Parent) Id,
#define DECL_RANGE(Id, FirstId, LastId) \
  First_##Id##Decl = FirstId, Last_##Id##Decl = LastId,
#include "swift/AST/DeclNodes.def"
};

/// Decl - Base class for all declarations in Swift.
class Decl {
  class DeclBitfields {
    friend class Decl;
    unsigned Kind : 8;
    bool Invalid : 1;
  };
  enum { NumDeclBits = 9 };
  static_assert(NumDeclBits <= 32, "fits in an unsigned");

  enum { NumValueDeclBits = NumDeclBits };
  static_assert(NumValueDeclBits <= 32, "fits in an unsigned");

  class ValueDeclBitfields {
    friend class ValueDecl;
    unsigned : NumValueDeclBits;

    // The following flags are not necessarily meaningful for all
    // kinds of value-declarations.

    // NeverUsedAsLValue - Whether this decl is ever used as an lvalue 
    // (i.e. used in a context where it could be modified).
    unsigned NeverUsedAsLValue : 1;

    // HasFixedLifetime - Whether the lifetime of this decl matches its
    // scope (i.e. the decl isn't captured, so it can be allocated as part of
    // the stack frame.)
    unsigned HasFixedLifetime : 1;
  };

protected:
  union {
    DeclBitfields DeclBits;
    ValueDeclBitfields ValueDeclBits;
  };

private:
  DeclContext *Context;

  Decl(const Decl&) = delete;
  void operator=(const Decl&) = delete;

protected:
  Decl(DeclKind kind, DeclContext *DC) : Context(DC) {
    DeclBits.Kind = unsigned(kind);
    DeclBits.Invalid = false;
  }

public:
  /// Alignment - The required alignment of Decl objects.
  enum { Alignment = 8 };

  DeclKind getKind() const { return DeclKind(DeclBits.Kind); }

  DeclContext *getDeclContext() const { return Context; }
  void setDeclContext(DeclContext *DC) { Context = DC; }

  /// getASTContext - Return the ASTContext that this decl lives in.
  ASTContext &getASTContext() const {
    assert(Context && "Decl doesn't have an assigned context");
    return Context->getASTContext();
  }

  SourceLoc getStartLoc() const { return getSourceRange().Start; }
  SourceLoc getLoc() const;
  SourceRange getSourceRange() const;

  void dump() const;
  void print(raw_ostream &OS, unsigned Indent = 0) const;

  bool walk(ASTWalker &walker);
  
  /// \brief Return whether this declaration has been determined invalid.
  bool isInvalid() const { return DeclBits.Invalid; }
  
  /// \brief Mark this declaration invalid.
  void setInvalid() { DeclBits.Invalid = true; }
  
  // Make vanilla new/delete illegal for Decls.
  void *operator new(size_t Bytes) = delete;
  void operator delete(void *Data) = delete;

  // Only allow allocation of Decls using the allocator in ASTContext
  // or by doing a placement new.
  void *operator new(size_t Bytes, ASTContext &C,
                     unsigned Alignment = Decl::Alignment);
  void *operator new(size_t Bytes, void *Mem) { 
    assert(Mem); 
    return Mem; 
  }
};

/// GenericParam - A parameter to a generic function or type, as declared in
/// the list of generic parameters, e.g., the T and U in:
///
/// \code
/// func f<T : Range, U>(t : T, u : U) { /* ... */ }
/// \endcode
class GenericParam {
  TypeAliasDecl *TypeParam;

public:
  /// Construct a generic parameter from a type parameter.
  GenericParam(TypeAliasDecl *TypeParam) : TypeParam(TypeParam) { }

  /// getDecl - Retrieve the generic parameter declaration.
  ValueDecl *getDecl() const {
    return reinterpret_cast<ValueDecl *>(TypeParam);
  }

  /// getAsTypeParam - Retrieve the generic parameter as a type parameter.
  TypeAliasDecl *getAsTypeParam() const { return TypeParam; }

  /// setDeclContext - Set the declaration context for the generic parameter,
  /// once it is known.
  void setDeclContext(DeclContext *DC);
};

/// \brief Describes the kind of a requirement that occurs within a requirements
/// clause.
enum class RequirementKind : unsigned int {
  /// \brief A conformance requirement T : P, where T is a type that depends
  /// on a generic parameter and P is a protocol to which T must conform.
  Conformance,
  /// \brief A same-type requirement T == U, where T and U are types that
  /// shall be equivalent.
  SameType
};

/// \brief A single requirement in a requires clause, which places additional
/// restrictions on the generic parameters or associated types of a generic
/// function, class, or protocol.
class Requirement {
  SourceLoc SeparatorLoc;
  RequirementKind Kind : 1;
  bool Invalid : 1;
  TypeLoc Types[2];

  Requirement(SourceLoc SeparatorLoc, RequirementKind Kind, TypeLoc FirstType,
              TypeLoc SecondType)
    : SeparatorLoc(SeparatorLoc), Kind(Kind), Invalid(false),
      Types{FirstType, SecondType} { }
  
public:
  /// \brief Construct a new conformance requirement.
  ///
  /// \param Subject The type that must conform to the given protocol or
  /// composition.
  /// \param ColonLoc The location of the ':', or an invalid location if
  /// this requirement was implied.
  /// \param Protocol The protocol or protocol composition to which the
  /// subject must conform.
  static Requirement getConformance(TypeLoc Subject,
                                    SourceLoc ColonLoc,
                                    TypeLoc Protocol) {
    return { ColonLoc, RequirementKind::Conformance, Subject, Protocol };
  }

  /// \brief Construct a new same-type requirement.
  ///
  /// \param FirstType The first type.
  /// \param EqualLoc The location of the '==' in the same-type constraint, or
  /// an invalid location if this requirement was implied.
  /// \param SecondType The second type.
  static Requirement getSameType(TypeLoc FirstType,
                                 SourceLoc EqualLoc,
                                 TypeLoc SecondType) {
    return { EqualLoc, RequirementKind::SameType, FirstType, SecondType };
  }

  /// \brief Determine the kind of requirement
  RequirementKind getKind() const { return Kind; }

  /// \brief Determine whether this requirement is invalid.
  bool isInvalid() const { return Invalid; }

  /// \brief Mark this requirement invalid.
  void setInvalid() { Invalid = true; }

  /// \brief Determine whether this is an implicitly-generated requirement.
  bool isImplicit() const {
    return SeparatorLoc.isInvalid();
  }

  /// \brief For a conformance requirement, return the subject of the
  /// conformance relationship.
  Type getSubject() const {
    assert(getKind() == RequirementKind::Conformance);
    return Types[0].getType();
  }

  TypeLoc &getSubjectLoc() {
    assert(getKind() == RequirementKind::Conformance);
    return Types[0];
  }

  /// \brief For a conformance requirement, return the protocol to which
  /// the subject conforms.
  Type getProtocol() const {
    assert(getKind() == RequirementKind::Conformance);
    return Types[1].getType();
  }

  TypeLoc &getProtocolLoc() {
    assert(getKind() == RequirementKind::Conformance);
    return Types[1];
  }

  /// \brief Retrieve the location of the ':' in an explicitly-written
  /// conformance requirement.
  SourceLoc getColonLoc() const {
    assert(getKind() == RequirementKind::Conformance);
    assert(!isImplicit() && "Implicit requirements have no location");
    return SeparatorLoc;
  }

  /// \brief Retrieve the first type of a same-type requirement.
  Type getFirstType() const {
    assert(getKind() == RequirementKind::SameType);
    return Types[0].getType();
  }

  TypeLoc &getFirstTypeLoc() {
    assert(getKind() == RequirementKind::SameType);
    return Types[0];
  }

  /// \brief Retrieve the second type of a same-type requirement.
  Type getSecondType() const {
    assert(getKind() == RequirementKind::SameType);
    return Types[1].getType();
  }

  TypeLoc &getSecondTypeLoc() {
    assert(getKind() == RequirementKind::SameType);
    return Types[1];
  }

  /// \brief Retrieve the location of the '==' in an explicitly-written
  /// same-type requirement.
  SourceLoc getEqualLoc() const {
    assert(getKind() == RequirementKind::SameType);
    assert(!isImplicit() && "Implicit requirements have no location");
    return SeparatorLoc;
  }
};

/// GenericParamList - A list of generic parameters that is part of a generic
/// function or type, along with extra requirements placed on those generic
/// parameters and types derived from them.
class GenericParamList {
  SourceRange Brackets;
  unsigned NumParams;
  SourceLoc RequiresLoc;
  MutableArrayRef<Requirement> Requirements;
  ArrayRef<ArchetypeType *> AllArchetypes;

  GenericParamList *OuterParameters;

  GenericParamList(SourceLoc LAngleLoc,
                   ArrayRef<GenericParam> Params,
                   SourceLoc RequiresLoc,
                   MutableArrayRef<Requirement> Requirements,
                   SourceLoc RAngleLoc);

public:
  /// create - Create a new generic parameter list within the given AST context.
  ///
  /// \param Context The ASTContext in which the generic parameter list will
  /// be allocated.
  /// \param LAngleLoc The location of the opening angle bracket ('<')
  /// \param Params The list of generic parameters, which will be copied into
  /// ASTContext-allocated memory.
  /// \param RAngleLoc The location of the closing angle bracket ('>')
  static GenericParamList *create(ASTContext &Context,
                                  SourceLoc LAngleLoc,
                                  ArrayRef<GenericParam> Params,
                                  SourceLoc RAngleLoc);

  /// create - Create a new generic parameter list and requires clause within
  /// the given AST context.
  ///
  /// \param Context The ASTContext in which the generic parameter list will
  /// be allocated.
  /// \param LAngleLoc The location of the opening angle bracket ('<')
  /// \param Params The list of generic parameters, which will be copied into
  /// ASTContext-allocated memory.
  /// \param RequiresLoc The location of the 'requires' keyword, if any.
  /// \param Requirements The list of requirements, which will be copied into
  /// ASTContext-allocated memory.
  /// \param RAngleLoc The location of the closing angle bracket ('>')
  static GenericParamList *create(ASTContext &Context,
                                  SourceLoc LAngleLoc,
                                  ArrayRef<GenericParam> Params,
                                  SourceLoc RequiresLoc,
                                  MutableArrayRef<Requirement> Requirements,
                                  SourceLoc RAngleLoc);

  MutableArrayRef<GenericParam> getParams() {
    return MutableArrayRef<GenericParam>(
             reinterpret_cast<GenericParam *>(this + 1), NumParams);
  }

  ArrayRef<GenericParam> getParams() const {
    return ArrayRef<GenericParam>(
             reinterpret_cast<const GenericParam *>(this + 1), NumParams);
  }

  unsigned size() const { return NumParams; }
  GenericParam *begin() { return getParams().begin(); }
  GenericParam *end()   { return getParams().end(); }
  const GenericParam *begin() const { return getParams().begin(); }
  const GenericParam *end()   const { return getParams().end(); }

  /// \brief Retrieve the location of the 'requires' keyword, or an invald
  /// location if 'requires' was not present.
  SourceLoc getRequiresLoc() const { return RequiresLoc; }

  /// \brief Retrieve the set of additional requirements placed on these
  /// generic parameters and types derived from them.
  ///
  /// This list may contain both explicitly-written requirements as well as
  /// implicitly-generated requirements, and may be non-empty even if no
  /// 'requires' keyword were present.
  MutableArrayRef<Requirement> getRequirements() { return Requirements; }

  /// \brief Retrieve the set of additional requirements placed on these
  /// generic parameters and types derived from them.
  ///
  /// This list may contain both explicitly-written requirements as well as
  /// implicitly-generated requirements, and may be non-empty even if no
  /// 'requires' keyword were present.
  ArrayRef<Requirement> getRequirements() const { return Requirements; }

  /// \brief Override the set of requirements associated with this generic
  /// parameter list.
  ///
  /// \param NewRequirements The new set of requirements, which is expected
  /// to be a superset of the existing set of requirements (although this
  /// property is not checked here). It is assumed that the array reference
  /// refers to ASTContext-allocated memory.
  void overrideRequirements(MutableArrayRef<Requirement> NewRequirements) {
    Requirements = NewRequirements;
  }

  /// \brief Retrieves the list containing all archetypes described by this
  /// generic parameter clause.
  ///
  /// In this list of archetypes, the primary archetypes come first followed by
  /// any non-primary archetypes (i.e., those archetypes that encode associated
  /// types of another archetype).
  ArrayRef<ArchetypeType *> getAllArchetypes() const { return AllArchetypes; }

  /// \brief Sets all archetypes *without* copying the source array.
  void setAllArchetypes(ArrayRef<ArchetypeType *> AA) {
    AllArchetypes = AA;
  }

  /// \brief Retrieve the outer generic parameter list, which provides the
  /// generic parameters of the context in which this generic parameter list
  /// exists.
  ///
  /// Consider the following generic class:
  ///
  /// \code
  /// class Vector<T> {
  ///   constructor<R : Range requires R.Element == T>(range : R) { }
  /// }
  /// \endcode
  ///
  /// The generic parameter list <T> has no outer parameters, because it is
  /// the outermost generic parameter list. The generic parameter list
  /// <R : Range...> for the constructor has the generic parameter list <T> as
  /// its outer generic parameter list.
  GenericParamList *getOuterParameters() const { return OuterParameters; }

  /// \brief Set the outer generic parameter list. See \c getOuterParameters
  /// for more information.
  void setOuterParameters(GenericParamList *Outer) { OuterParameters = Outer; }

  SourceRange getSourceRange() const { return Brackets; }
};

/// ImportDecl - This represents a single import declaration, e.g.:
///   import swift
///   import swift.int
class ImportDecl : public Decl {
public:
  typedef std::pair<Identifier, SourceLoc> AccessPathElement;

private:
  SourceLoc ImportLoc;

  /// The number of elements in this path.
  unsigned NumPathElements;

  AccessPathElement *getPathBuffer() {
    return reinterpret_cast<AccessPathElement*>(this+1);
  }
  const AccessPathElement *getPathBuffer() const {
    return reinterpret_cast<const AccessPathElement*>(this+1);
  }
  
  ImportDecl(DeclContext *DC, SourceLoc ImportLoc,
             ArrayRef<AccessPathElement> Path);

public:
  static ImportDecl *create(ASTContext &C, DeclContext *DC,
                            SourceLoc ImportLoc,
                            ArrayRef<AccessPathElement> Path);

  ArrayRef<AccessPathElement> getAccessPath() const {
    return ArrayRef<AccessPathElement>(getPathBuffer(), NumPathElements);
  }

  SourceLoc getStartLoc() const { return ImportLoc; }
  SourceLoc getLoc() const { return ImportLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(ImportLoc, getAccessPath().back().second);
  }

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Import;
  }
};

/// ExtensionDecl - This represents a type extension containing methods
/// associated with the type.  This is not a ValueDecl and has no Type because
/// there are no runtime values of the Extension's type.  
class ExtensionDecl : public Decl, public DeclContext {
  SourceLoc ExtensionLoc;  // Location of 'extension' keyword.
  SourceRange Braces;

  /// ExtendedType - The type being extended.
  TypeLoc ExtendedType;
  MutableArrayRef<TypeLoc> Inherited;
  ArrayRef<Decl*> Members;
public:

  ExtensionDecl(SourceLoc ExtensionLoc, TypeLoc ExtendedType,
                MutableArrayRef<TypeLoc> Inherited,
                DeclContext *Parent)
    : Decl(DeclKind::Extension, Parent),
      DeclContext(DeclContextKind::ExtensionDecl, Parent),
      ExtensionLoc(ExtensionLoc),
      ExtendedType(ExtendedType), Inherited(Inherited) {
  }
  
  SourceLoc getStartLoc() const { return ExtensionLoc; }
  SourceLoc getLoc() const { return ExtensionLoc; }
  SourceRange getSourceRange() const {
    return { ExtensionLoc, Braces.End };
  }

  Type getExtendedType() const { return ExtendedType.getType(); }
  TypeLoc &getExtendedTypeLoc() { return ExtendedType; }

  /// \brief Retrieve the set of protocols that this type inherits (i.e,
  /// explicitly conforms to).
  MutableArrayRef<TypeLoc> getInherited() { return Inherited; }
  ArrayRef<TypeLoc> getInherited() const { return Inherited; }

  ArrayRef<Decl*> getMembers() const { return Members; }
  void setMembers(ArrayRef<Decl*> M, SourceRange B) {
    Members = M;
    Braces = B;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Extension;
  }
  static bool classof(const DeclContext *C) {
    return C->getContextKind() == DeclContextKind::ExtensionDecl;
  }
};

// PatternBindingDecl - This decl contains a pattern and optional initializer
// for a set of one or more VarDecls declared together.  (For example, in
// "var (a,b) = foo()", this contains the pattern "(a,b)" and the intializer
// "foo()".  The same applies to simpler declarations like "var a = foo()".)
class PatternBindingDecl : public Decl {
  SourceLoc VarLoc; // Location of the 'var' keyword
  Pattern *Pat; // The pattern which this decl binds
  Expr *Init; // Initializer for the variables

  friend class Decl;
  
public:
  PatternBindingDecl(SourceLoc VarLoc, Pattern *Pat, Expr *E,
                     DeclContext *Parent)
    : Decl(DeclKind::PatternBinding, Parent), VarLoc(VarLoc), Pat(Pat),
      Init(E) {
  }

  SourceLoc getStartLoc() const { return VarLoc; }
  SourceLoc getLoc() const { return VarLoc; }
  SourceRange getSourceRange() const;

  Pattern *getPattern() const { return Pat; }
  void setPattern(Pattern *P) { Pat = P; }

  bool hasInit() const { return Init; }
  Expr *getInit() const { return Init; }
  void setInit(Expr *E) { Init = E; }

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::PatternBinding;
  }

};

/// TopLevelCodeDecl - This decl is used as a container for top-level
/// expressions and statements in the main module.  It is always a direct
/// child of the body of a TranslationUnit.  The primary reason for
/// building these is to give top-level statements a DeclContext which is
/// distinct from the TranslationUnit itself.  This, among other things,
/// makes it easier to distinguish between local top-level variables (which
/// are not live past the end of the statement) and global variables.
class TopLevelCodeDecl : public Decl, public DeclContext {
public:
  typedef llvm::PointerUnion<Expr*, Stmt*> ExprOrStmt;

private:
  ExprOrStmt Body;

public:
  TopLevelCodeDecl(DeclContext *Parent)
    : Decl(DeclKind::TopLevelCode, Parent),
      DeclContext(DeclContextKind::TopLevelCodeDecl, Parent) {}

  ExprOrStmt getBody() { return Body; }
  void setBody(Expr *E) { Body = E; }
  void setBody(Stmt *S) { Body = S; }

  SourceLoc getStartLoc() const;
  SourceLoc getLoc() const { return getStartLoc(); }
  SourceRange getSourceRange() const;

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::TopLevelCode;
  }
  static bool classof(const DeclContext *C) {
    return C->getContextKind() == DeclContextKind::TopLevelCodeDecl;
  }
};


/// ValueDecl - All named decls that are values in the language.  These can
/// have a type, etc.
class ValueDecl : public Decl {
  Identifier Name;
  const DeclAttributes *Attrs;
  static const DeclAttributes EmptyAttrs;
  Type Ty;

protected:
  ValueDecl(DeclKind K, DeclContext *DC, Identifier name, Type ty)
    : Decl(K, DC), Name(name), Attrs(&EmptyAttrs), Ty(ty) {
    ValueDeclBits.NeverUsedAsLValue = false;
    ValueDeclBits.HasFixedLifetime = false;
  }

public:

  /// isDefinition - Return true if this is a definition of a decl, not a
  /// forward declaration (e.g. of a function) that is implemented outside of
  /// the swift code.
  bool isDefinition() const;
  
  Identifier getName() const { return Name; }
  bool isOperator() const { return Name.isOperator(); }
  
  DeclAttributes &getMutableAttrs();
  const DeclAttributes &getAttrs() const { return *Attrs; }
  
  Resilience getResilienceFrom(Component *C) const;

  bool hasType() const { return !Ty.isNull(); }
  Type getType() const {
    assert(!Ty.isNull() && "declaration has no type set yet");
    return Ty;
  }

  /// Set the type of this declaration for the first time.
  void setType(Type T) {
    assert(Ty.isNull() && "changing type of declaration");
    Ty = T;
  }

  /// Overwrite the type of this declaration.
  void overwriteType(Type T) {
    Ty = T;
  }

  /// getTypeOfReference - Returns the type that would arise from a
  /// normal reference to this declaration.  For isReferencedAsLValue()'d decls,
  /// this returns a reference to the value's type.  For non-lvalue decls, this
  /// just returns the decl's type.
  Type getTypeOfReference() const;

  /// isReferencedAsLValue - Returns 'true' if references to this
  /// declaration are l-values.
  bool isReferencedAsLValue() const {
    return getKind() == DeclKind::Var;
  }

  /// isSettable - Determine whether references to this decl may appear
  /// on the left-hand side of an assignment or as the operand of a
  /// `&` or [assignment] operator.
  bool isSettable() const;
  
  void setHasFixedLifetime(bool flag) {
    ValueDeclBits.HasFixedLifetime = flag;
  }
  void setNeverUsedAsLValue(bool flag) {
    ValueDeclBits.NeverUsedAsLValue = flag;
  }

  bool hasFixedLifetime() const {
    return ValueDeclBits.HasFixedLifetime;
  }
  bool isNeverUsedAsLValue() const {
    return ValueDeclBits.NeverUsedAsLValue;
  }

  /// isInstanceMember - Determine whether this value is an instance member
  /// of a oneof or protocol.
  bool isInstanceMember() const;

  /// needsCapture - Check whether referring to this decl from a nested
  /// function requires capturing it.
  bool needsCapture() const;

  static bool classof(const Decl *D) {
    return D->getKind() >= DeclKind::First_ValueDecl &&
           D->getKind() <= DeclKind::Last_ValueDecl;
  }
};

/// This is a common base class for declarations which declare a type.
class TypeDecl : public ValueDecl {
  MutableArrayRef<TypeLoc> Inherited;

public:
  TypeDecl(DeclKind K, DeclContext *DC, Identifier name,
           MutableArrayRef<TypeLoc> inherited, Type ty) :
    ValueDecl(K, DC, name, ty), Inherited(inherited) {}

  Type getDeclaredType() const;

  /// \brief Retrieve the set of protocols that this type inherits (i.e,
  /// explicitly conforms to).
  MutableArrayRef<TypeLoc> getInherited() { return Inherited; }
  ArrayRef<TypeLoc> getInherited() const { return Inherited; }

  void setInherited(MutableArrayRef<TypeLoc> i) { Inherited = i; }

  static bool classof(const Decl *D) {
    return D->getKind() >= DeclKind::First_TypeDecl &&
           D->getKind() <= DeclKind::Last_TypeDecl;
  }
};

/// TypeAliasDecl - This is a declaration of a typealias, for example:
///
///    typealias foo : int
///
/// TypeAliasDecl's always have 'MetaTypeType' type.
///
class TypeAliasDecl : public TypeDecl {
  /// The type that represents this (sugared) name alias.
  mutable NameAliasType *AliasTy;

  SourceLoc TypeAliasLoc; // The location of the 'typalias' keyword
  SourceLoc NameLoc; // The location of the declared type
  TypeLoc UnderlyingTy;

public:
  TypeAliasDecl(SourceLoc TypeAliasLoc, Identifier Name,
                SourceLoc NameLoc, TypeLoc UnderlyingTy,
                DeclContext *DC, MutableArrayRef<TypeLoc> Inherited);

  SourceLoc getStartLoc() const { return TypeAliasLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const;

  /// getUnderlyingType - Returns the underlying type, which is
  /// assumed to have been set.
  Type getUnderlyingType() const {
    assert(!UnderlyingTy.getType().isNull() &&
           "getting invalid underlying type");
    return UnderlyingTy.getType();
  }

  /// \brief Determine whether this type alias has an underlying type.
  bool hasUnderlyingType() const { return !UnderlyingTy.getType().isNull(); }

  TypeLoc &getUnderlyingTypeLoc() { return UnderlyingTy; }


  /// getAliasType - Return the sugared version of this decl as a Type.
  NameAliasType *getAliasType() const { return AliasTy; }
  
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::TypeAlias;
  }
};

/// NominalTypeDecl - a declaration of a nominal type, like a struct.  This
/// decl is always a DeclContext.
class NominalTypeDecl : public TypeDecl, public DeclContext {
  SourceRange Braces;
  ArrayRef<Decl*> Members;
  GenericParamList *GenericParams;

protected:
  Type DeclaredTy;
  Type DeclaredTyInContext;
  
public:
  NominalTypeDecl(DeclKind K, DeclContext *DC, Identifier name,
                  MutableArrayRef<TypeLoc> inherited,
                  GenericParamList *GenericParams) :
    TypeDecl(K, DC, name, inherited, Type()),
    DeclContext(DeclContextKind::NominalTypeDecl, DC),
    GenericParams(GenericParams), DeclaredTy(nullptr) {}

  ArrayRef<Decl*> getMembers() const { return Members; }
  SourceRange getBraces() const { return Braces; }
  void setMembers(ArrayRef<Decl*> M, SourceRange B) {
    Members = M;
    Braces = B;
  }

  GenericParamList *getGenericParams() const { return GenericParams; }

  /// getDeclaredType - Retrieve the type declared by this entity.
  Type getDeclaredType() const { return DeclaredTy; }

  Type getDeclaredTypeInContext();
  
  void overwriteDeclaredType(Type DT) {
    DeclaredTy = DT;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= DeclKind::First_NominalTypeDecl &&
           D->getKind() <= DeclKind::Last_NominalTypeDecl;
  }
  static bool classof(const DeclContext *C) {
    return C->getContextKind() == DeclContextKind::NominalTypeDecl;
  }
};

/// OneOfDecl - This is the declaration of a oneof, for example:
///
///    oneof Bool { true, false }
///
/// The type of the decl itself is a MetaTypeType; use getDeclaredType()
/// to get the declared type ("Bool" in the above example).
class OneOfDecl : public NominalTypeDecl {
  SourceLoc OneOfLoc;
  SourceLoc NameLoc;

public:
  OneOfDecl(SourceLoc OneOfLoc, Identifier Name, SourceLoc NameLoc,
            MutableArrayRef<TypeLoc> Inherited,
            GenericParamList *GenericParams, DeclContext *DC);

  SourceLoc getStartLoc() const { return OneOfLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(OneOfLoc, getBraces().End);
  }

  OneOfElementDecl *getElement(Identifier Name) const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::OneOf;
  }
  static bool classof(const NominalTypeDecl *D) {
    return D->getKind() == DeclKind::OneOf;
  }
  static bool classof(const DeclContext *C) {
    return isa<NominalTypeDecl>(C) && classof(cast<NominalTypeDecl>(C));
  }
};

/// StructDecl - This is the declaration of a struct, for example:
///
///    struct Complex { var R : Double, I : Double }
///
/// The type of the decl itself is a MetaTypeType; use getDeclaredType()
/// to get the declared type ("Complex" in the above example).
class StructDecl : public NominalTypeDecl {
  SourceLoc StructLoc;
  SourceLoc NameLoc;

public:
  StructDecl(SourceLoc StructLoc, Identifier Name, SourceLoc NameLoc,
             MutableArrayRef<TypeLoc> Inherited,
             GenericParamList *GenericParams, DeclContext *DC);

  SourceLoc getStartLoc() const { return StructLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(StructLoc, getBraces().End);
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Struct;
  }
  static bool classof(const NominalTypeDecl *D) {
    return D->getKind() == DeclKind::Struct;
  }
  static bool classof(const DeclContext *C) {
    return isa<NominalTypeDecl>(C) && classof(cast<NominalTypeDecl>(C));
  }
};

/// ClassDecl - This is the declaration of a class, for example:
///
///    class Complex { var R : Double, I : Double }
///
/// The type of the decl itself is a MetaTypeType; use getDeclaredType()
/// to get the declared type ("Complex" in the above example).
class ClassDecl : public NominalTypeDecl {
  SourceLoc ClassLoc;
  SourceLoc NameLoc;
  TypeLoc BaseClass;

public:
  ClassDecl(SourceLoc ClassLoc, Identifier Name, SourceLoc NameLoc,
            MutableArrayRef<TypeLoc> Inherited,
            GenericParamList *GenericParams, DeclContext *DC);

  SourceLoc getStartLoc() const { return ClassLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(ClassLoc, getBraces().End);
  }

  bool hasBaseClass() { return BaseClass.getType(); }
  Type getBaseClass() { return BaseClass.getType(); }
  TypeLoc &getBaseClassLoc() { return BaseClass; }
  void setBaseClassLoc(TypeLoc base) { BaseClass = base; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Class;
  }
  static bool classof(const NominalTypeDecl *D) {
    return D->getKind() == DeclKind::Class;
  }
  static bool classof(const DeclContext *C) {
    return isa<NominalTypeDecl>(C) && classof(cast<NominalTypeDecl>(C));
  }
};


/// ProtocolDecl - A declaration of a protocol, for example:
///
///   protocol Drawable {
///     func draw()
///   }
class ProtocolDecl : public NominalTypeDecl {
  SourceLoc ProtocolLoc;
  SourceLoc NameLoc;
  
public:
  ProtocolDecl(DeclContext *DC, SourceLoc ProtocolLoc, SourceLoc NameLoc,
               Identifier Name, MutableArrayRef<TypeLoc> Inherited);
  
  using Decl::getASTContext;

  void setMembers(MutableArrayRef<Decl *> M, SourceRange B) {
    NominalTypeDecl::setMembers(M, B);
  }

  /// \brief Determine whether this protocol inherits from the given ("super")
  /// protocol.
  bool inheritsFrom(const ProtocolDecl *Super) const;
  
  /// \brief Collect all of the inherited protocols into the given set.
  void collectInherited(llvm::SmallPtrSet<ProtocolDecl *, 4> &Inherited);
  
  ProtocolType *getDeclaredType() const {
    return reinterpret_cast<ProtocolType *>(DeclaredTy.getPointer());
  }
  
  SourceLoc getStartLoc() const { return ProtocolLoc; }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const {
    return SourceRange(ProtocolLoc, getBraces().End);
  }

  /// \brief Retrieve the associated type 'This'.
  TypeAliasDecl *getThis() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Protocol;
  }
  static bool classof(const NominalTypeDecl *D) {
    return D->getKind() == DeclKind::Protocol;
  }
  static bool classof(const DeclContext *C) {
    return isa<NominalTypeDecl>(C) && classof(cast<NominalTypeDecl>(C));
  }
};

/// VarDecl - 'var' declaration.
class VarDecl : public ValueDecl {
private:
  SourceLoc VarLoc;    // Location of the 'var' token.
  
  struct GetSetRecord {
    SourceRange Braces;
    FuncDecl *Get;       // User-defined getter
    FuncDecl *Set;       // User-defined setter
  };
  
  GetSetRecord *GetSet;
  VarDecl *OverriddenDecl;

public:
  VarDecl(SourceLoc VarLoc, Identifier Name, Type Ty, DeclContext *DC)
    : ValueDecl(DeclKind::Var, DC, Name, Ty),
      VarLoc(VarLoc), GetSet(), OverriddenDecl(nullptr) {}

  SourceLoc getLoc() const { return VarLoc; }
  SourceLoc getStartLoc() const { return VarLoc; }
  SourceRange getSourceRange() const { return VarLoc; }

  /// \brief Determine whether this variable is actually a property, which
  /// has no storage but does have a user-defined getter or setter.
  bool isProperty() const { return GetSet != nullptr; }
  
  /// \brief Make this variable into a property, providing a getter and
  /// setter.
  void setProperty(ASTContext &Context, SourceLoc LBraceLoc, FuncDecl *Get,
                   FuncDecl *Set, SourceLoc RBraceLoc);

  /// \brief Retrieve the getter used to access the value of this variable.
  FuncDecl *getGetter() const { return GetSet? GetSet->Get : nullptr; }

  /// \brief Retrieve the setter used to mutate the value of this variable.
  FuncDecl *getSetter() const { return GetSet? GetSet->Set : nullptr; }
  
  /// \brief Returns whether the var is settable, either because it is a
  /// simple var or because it is a property with a setter.
  bool isSettable() const { return !GetSet || GetSet->Set; }
  
  VarDecl *getOverriddenDecl() const { return OverriddenDecl; }
  void setOverriddenDecl(VarDecl *over) { OverriddenDecl = over; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == DeclKind::Var; }
};


/// FuncDecl - 'func' declaration.
class FuncDecl : public ValueDecl {
  SourceLoc StaticLoc;  // Location of the 'static' token or invalid.
  SourceLoc FuncLoc;    // Location of the 'func' token.
  SourceLoc NameLoc;
  GenericParamList *GenericParams;
  FuncExpr *Body;
  llvm::PointerIntPair<Decl *, 1, bool> GetOrSetDecl;
  FuncDecl *OverriddenDecl;

public:
  FuncDecl(SourceLoc StaticLoc, SourceLoc FuncLoc, Identifier Name,
           SourceLoc NameLoc, GenericParamList *GenericParams, Type Ty,
           FuncExpr *Body, DeclContext *DC)
    : ValueDecl(DeclKind::Func, DC, Name, Ty), StaticLoc(StaticLoc),
      FuncLoc(FuncLoc), NameLoc(NameLoc), GenericParams(GenericParams),
      Body(Body), OverriddenDecl(nullptr) { }
  
  bool isStatic() const {
    return StaticLoc.isValid() || getName().isOperator();
  }

  FuncExpr *getBody() const { return Body; }
  void setBody(FuncExpr *NewBody) { Body = NewBody; }

  /// getNaturalArgumentCount - Returns the "natural" number of
  /// argument clauses taken by this function.  This value is always
  /// at least one, and it may be more if the function is implicitly
  /// or explicitly curried.
  ///
  /// For example, this function:
  ///   func negate(x : Int) -> Int { return -x }
  /// has a natural argument count of 1 if it is freestanding.  If it is
  /// a method, it has a natural argument count of 2, as does this
  /// curried function:
  ///   func add(x : Int)(y : Int) -> Int { return x + y }
  ///
  /// This value never exceeds the number of chained function types
  /// in the function's type, but it can be less for functions which
  /// return a value of function type:
  ///   func const(x : Int) -> () -> Int { return { x } } // NAC==1
  unsigned getNaturalArgumentCount() const;
  
  /// getExtensionType - If this is a method in a type extension for some type,
  /// return that type, otherwise return Type().
  Type getExtensionType() const;
  
  /// computeThisType - If this is a method in a type extension for some type,
  /// compute and return the type to be used for the 'this' argument of the
  /// type (which varies based on whether the extended type is a reference type
  /// or not), or an empty Type() if no 'this' argument should exist.  This can
  /// only be used after name binding has resolved types.
  ///
  /// \param OuterGenericParams If non-NULL, and this function is an instance
  /// of a generic type, will be set to the generic parameter list of that
  /// generic type.
  Type computeThisType(GenericParamList **OuterGenericParams = nullptr) const;
  
  /// getImplicitThisDecl - If this FuncDecl is a non-static method in an
  /// extension context, it will have a 'this' argument.  This method returns it
  /// if present, or returns null if not.
  VarDecl *getImplicitThisDecl();
  const VarDecl *getImplicitThisDecl() const {
    return const_cast<FuncDecl*>(this)->getImplicitThisDecl();
  }
  
  SourceLoc getStaticLoc() const { return StaticLoc; }
  SourceLoc getFuncLoc() const { return FuncLoc; }

  SourceLoc getStartLoc() const {
    return StaticLoc.isValid() ? StaticLoc : FuncLoc;
  }
  SourceLoc getLoc() const { return NameLoc; }
  SourceRange getSourceRange() const;

  /// getGenericParams - Retrieve the set of parameters to a generic function,
  /// or null if this function is not generic.
  GenericParamList *getGenericParams() const { return GenericParams; }

  /// isGeneric - Determine whether this is a generic function, which can only
  /// be used when each of the archetypes is bound to a particular concrete
  /// type.
  bool isGeneric() const { return GenericParams != nullptr; }

  /// makeGetter - Note that this function is the getter for the given
  /// declaration, which may be either a variable or a subscript declaration.
  void makeGetter(Decl *D) {
    GetOrSetDecl.setPointer(D);
    GetOrSetDecl.setInt(false);
  }
  
  /// makeSetter - Note that this function is the setter for the given
  /// declaration, which may be either a variable or a subscript declaration.
  void makeSetter(Decl *D) {
    GetOrSetDecl.setPointer(D);
    GetOrSetDecl.setInt(true);
  }
  
  /// getGetterVar - If this function is a getter, retrieve the declaration for
  /// which it is a getter. Otherwise, returns null.
  Decl *getGetterDecl() const {
    return GetOrSetDecl.getInt()? nullptr : GetOrSetDecl.getPointer();
  }

  /// getSetterVar - If this function is a setter, retrieve the declaration for
  /// which it is a setter. Otherwise, returns null.
  Decl *getSetterDecl() const {
    return GetOrSetDecl.getInt()? GetOrSetDecl.getPointer() : nullptr;
  }

  /// isGetterOrSetter - Determine whether this is a getter or a setter vs.
  /// a normal function.
  bool isGetterOrSetter() const { return getGetterOrSetterDecl() != 0; }

  /// getGetterOrSetterDecl - Return the declaration for which this function
  /// is a getter or setter, if it is one.
  Decl *getGetterOrSetterDecl() const { return GetOrSetDecl.getPointer(); }

  /// Given that this is an Objective-C method declaration, produce
  /// its selector in the given buffer (as UTF-8).
  StringRef getObjCSelector(llvm::SmallVectorImpl<char> &buffer) const;

  FuncDecl *getOverriddenDecl() const { return OverriddenDecl; }
  void setOverriddenDecl(FuncDecl *over) { OverriddenDecl = over; }

  static bool classof(const Decl *D) { return D->getKind() == DeclKind::Func; }
};

/// OneOfElementDecl - This represents an element of a 'oneof' declaration, e.g.
/// X and Y in:
///   oneof d { X : int, Y : int, Z }
/// The type of a OneOfElementDecl is always the OneOfType for the containing
/// oneof.
class OneOfElementDecl : public ValueDecl {
  SourceLoc IdentifierLoc;

  /// ArgumentType - This is the type specified with the oneof element.  For
  /// example 'int' in the Y example above.  This is null if there is no type
  /// associated with this element (such as in the Z example).
  TypeLoc ArgumentType;
    
public:
  OneOfElementDecl(SourceLoc IdentifierLoc, Identifier Name,
                   TypeLoc ArgumentType, DeclContext *DC)
  : ValueDecl(DeclKind::OneOfElement, DC, Name, Type()),
    IdentifierLoc(IdentifierLoc), ArgumentType(ArgumentType) {}

  bool hasArgumentType() const { return !ArgumentType.getType().isNull(); }
  Type getArgumentType() const { return ArgumentType.getType(); }
  TypeLoc &getArgumentTypeLoc() { return ArgumentType; }

  SourceLoc getStartLoc() const { return IdentifierLoc; }
  SourceLoc getLoc() const { return IdentifierLoc; }
  SourceRange getSourceRange() const;

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::OneOfElement;
  }
};

/// SubscriptDecl - Declares a subscripting operator for a type.
///
/// A subscript declaration is defined as a get/set pair that produces a
/// specific type. For example:
///
/// \code
/// subscript (i : Int) -> String {
///   get { /* return ith String */ }
///   set { /* set ith string to value */ }
/// }
/// \endcode
///
/// A type with a subscript declaration can be used as the base of a subscript
/// expression a[i], where a is of the subscriptable type and i is the type
/// of the index. A subscript can have multiple indices:
///
/// struct Matrix {
///   subscript (i : Int, j : Int) -> Double {
///     get { /* return element at position (i, j) */ }
///     set { /* set element at position (i, j) */ }
///   }
/// }
///
/// A given type can have multiple subscript declarations, so long as the
/// signatures (indices and element type) are distinct.
///
/// FIXME: SubscriptDecl isn't naturally a ValueDecl, but it's currently useful
/// to get name lookup to find it with a bogus name.
class SubscriptDecl : public ValueDecl {
  SourceLoc SubscriptLoc;
  SourceLoc ArrowLoc;
  Pattern *Indices;
  TypeLoc ElementTy;
  SourceRange Braces;
  FuncDecl *Get;
  FuncDecl *Set;
  SubscriptDecl *OverriddenDecl;

public:
  SubscriptDecl(Identifier NameHack, SourceLoc SubscriptLoc, Pattern *Indices,
                SourceLoc ArrowLoc, TypeLoc ElementTy,
                SourceRange Braces, FuncDecl *Get, FuncDecl *Set,
                DeclContext *Parent)
    : ValueDecl(DeclKind::Subscript, Parent, NameHack, Type()),
      SubscriptLoc(SubscriptLoc),
      ArrowLoc(ArrowLoc), Indices(Indices), ElementTy(ElementTy),
      Braces(Braces), Get(Get), Set(Set), OverriddenDecl(nullptr) { }
  
  SourceLoc getStartLoc() const { return SubscriptLoc; }
  SourceLoc getLoc() const;
  SourceRange getSourceRange() const;

  /// \brief Retrieve the indices for this subscript operation.
  Pattern *getIndices() const { return Indices; }
  
  /// \brief Retrieve the type of the element referenced by a subscript
  /// operation.
  Type getElementType() const { return ElementTy.getType(); }
  TypeLoc &getElementTypeLoc() { return ElementTy; }

  /// \brief Retrieve the subscript getter, a function that takes the indices
  /// and produces a value of the element type.
  FuncDecl *getGetter() const { return Get; }
  
  /// \brief Retrieve the subscript setter, a function that takes the indices
  /// and a new value of the lement type and updates the corresponding value.
  ///
  /// The subscript setter is optional.
  FuncDecl *getSetter() const { return Set; }
  
  /// \brief Returns whether the subscript operation has a setter.
  bool isSettable() const { return Set; }
  
  SubscriptDecl *getOverriddenDecl() const { return OverriddenDecl; }
  void setOverriddenDecl(SubscriptDecl *over) { OverriddenDecl = over; }

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Subscript;
  }
};

/// ConstructorDecl - Declares a constructor for a type.  For example:
///
/// \code
/// struct X {
///   var x : Int
///   constructor(i : Int) {
///      x = i
///   }
/// }
/// \endcode
class ConstructorDecl : public ValueDecl, public DeclContext {
  SourceLoc ConstructorLoc;
  Pattern *Arguments;
  BraceStmt *Body;
  VarDecl *ImplicitThisDecl;
  GenericParamList *GenericParams;

public:
  ConstructorDecl(Identifier NameHack, SourceLoc ConstructorLoc,
                  Pattern *Arguments, VarDecl *ImplicitThisDecl,
                  GenericParamList *GenericParams, DeclContext *Parent)
    : ValueDecl(DeclKind::Constructor, Parent, NameHack, Type()),
      DeclContext(DeclContextKind::ConstructorDecl, Parent),
      ConstructorLoc(ConstructorLoc), Arguments(Arguments), Body(nullptr),
      ImplicitThisDecl(ImplicitThisDecl), GenericParams(GenericParams) {}
  
  SourceLoc getStartLoc() const { return ConstructorLoc; }
  SourceLoc getLoc() const;
  SourceRange getSourceRange() const;

  Pattern *getArguments() { return Arguments; }
  void setArguments(Pattern *args) {
    assert(!Arguments && "Resetting arguments?");
    Arguments = args;
  }

  BraceStmt *getBody() const { return Body; }
  void setBody(BraceStmt *b) { Body = b; }

  /// computeThisType - compute and return the type of 'this'.
  Type computeThisType(GenericParamList **OuterGenericParams = nullptr) const;

  /// getArgumentType - get the type of the argument tuple
  Type getArgumentType() const;

  /// getImplicitThisDecl - This method returns the implicit 'this' decl.
  VarDecl *getImplicitThisDecl() const { return ImplicitThisDecl; }

  GenericParamList *getGenericParams() const { return GenericParams; }
  bool isGeneric() const { return GenericParams != nullptr; }

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Constructor;
  }
  static bool classof(const DeclContext *DC) {
    return DC->getContextKind() == DeclContextKind::ConstructorDecl;
  }
};

/// DestructorDecl - Declares a destructor for a type.  For example:
///
/// \code
/// struct X {
///   var fd : Int
///   destructor {
///      close(fd)
///   }
/// }
/// \endcode
class DestructorDecl : public ValueDecl, public DeclContext {
  SourceLoc DestructorLoc;
  BraceStmt *Body;
  VarDecl *ImplicitThisDecl;
  
public:
  DestructorDecl(Identifier NameHack, SourceLoc DestructorLoc,
                  VarDecl *ImplicitThisDecl, DeclContext *Parent)
    : ValueDecl(DeclKind::Destructor, Parent, NameHack, Type()),
      DeclContext(DeclContextKind::DestructorDecl, Parent),
      DestructorLoc(DestructorLoc), Body(nullptr),
      ImplicitThisDecl(ImplicitThisDecl) {}
  
  SourceLoc getStartLoc() const { return DestructorLoc; }
  SourceLoc getLoc() const { return DestructorLoc; }
  SourceRange getSourceRange() const;

  BraceStmt *getBody() const { return Body; }
  void setBody(BraceStmt *b) { Body = b; }

  /// computeThisType - compute and return the type of 'this'.
  Type computeThisType(GenericParamList **OuterGenericParams = nullptr) const;

  /// getImplicitThisDecl - This method returns the implicit 'this' decl.
  VarDecl *getImplicitThisDecl() const { return ImplicitThisDecl; }

  static bool classof(const Decl *D) {
    return D->getKind() == DeclKind::Destructor;
  }
  static bool classof(const DeclContext *DC) {
    return DC->getContextKind() == DeclContextKind::DestructorDecl;
  }
};

inline void GenericParam::setDeclContext(DeclContext *DC) {
  TypeParam->setDeclContext(DC);
}

inline bool ValueDecl::isSettable() const {
  if (auto vd = dyn_cast<VarDecl>(this)) {
    return vd->isSettable();
  } else if (auto sd = dyn_cast<SubscriptDecl>(this)) {
    return sd->isSettable();
  } else
    return false;
}
  
} // end namespace swift

#endif
