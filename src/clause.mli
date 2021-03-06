(* Copyright (c) 2013 Radek Micek *)

(** Clauses. *)

type t = Lit.t list

(** Simplifies the clause. Here is a list of performed simplifications:

   - Every inequality of variables [x != y] is removed
     and [x] is replaced by [y].
   - The order of the arguments of the commutative symbols is normalized.
   - Duplicate literals are removed.
   - Literals which are never true are removed.

   Returns [None] if the clause is tautology.
*)
val simplify : [> `R] Symb.db -> t -> t option

(** Uses {!Clause.simplify} to simplify the given clauses. Returns only
   the empty clause if there is the empty clause among the simplified clauses.
   Otherwise all simplified clauses are returned.
*)
val simplify_all : [> `R] Symb.db -> t BatDynArray.t -> t BatDynArray.t

(** Counts and renumbers the variables in the given clause.
   The variables are assigned numbers [0,..,n-1] where [n]
   is the count of the distinct variables in the clause.
*)
val normalize_vars : t -> t * int

(** Uses equalities of ground terms to rewrite complex ground terms
   to simpler ground terms. Ground terms are compared by the number
   of function symbols and ties are resolved by comparing ground terms
   themselves.

   Inputs are not modified.
*)
val rewrite_ground_terms :
  [> `R] Symb.db -> t BatDynArray.t -> t BatDynArray.t

(** Returns a logically equivalent clause which is flat
   or [None] if the clause is a tautology.

   A flat clause contains only shallow literals.
   A literal is shallow iff it has one of the following forms:

   - [?p(x1,..,xn)],
   - [x ?= f(x1,..,xn)],
   - [x ?= y].

   Question mark means an optional negation.
*)
val flatten : [> `R] Symb.db -> t -> t option

(** Returns a logically equivalent clause or [None] if the clause
   is a tautology.

   Tries to reduce the number of variables.
*)
val unflatten : [> `R] Symb.db -> t -> t option

(** Returns the set of the variables in the given terms. *)
val vars : t -> Sh.IntSet.t

(** Converts clause to string. *)
val show : t -> string
