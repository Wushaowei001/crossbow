(* Copyright (c) 2013 Radek Micek *)

module S = Symb
module T = Term
module L = Lit
module C = Clause

let (|>) = BatPervasives.(|>)

exception Term of T.t

let define_ground_terms symdb clauses =

  let raise_when_ground_term_depth_two term = match%earr term with
    | T.Var _
    | T.Func (_, [| |]) -> ()
    | T.Func (_, args) as t ->
        if Earray.for_all T.is_const args then
          raise (Term t) in

  let define_ground_term t cs =
    let s = Symb.add_func symdb 0 in
    Symb.set_auxiliary symdb s true;
    BatDynArray.insert cs 0
      [ L.lit (Sh.Pos, Symb.sym_eq,
          Earray.of_array [| t; T.func (s, Earray.empty) |]) ] in

  (* Define nested ground terms. Nested ground term is an argument
     of a function or an argument of a predicate except equality.
  *)
  let rec define_nested cs =
    let find_in_term term =
      Term.iter
        raise_when_ground_term_depth_two
        term in
    let find_in_lit lit = match%earr lit with
      | L.Lit (_, s, [| l; r |]) when s = Symb.sym_eq ->
          l |> T.get_args |> Earray.iter find_in_term;
          r |> T.get_args |> Earray.iter find_in_term
      | L.Lit (_, _, args) ->
          Earray.iter find_in_term args in

    try
      BatDynArray.iter (List.iter find_in_lit) cs;
      cs
    with
      | Term t ->
          define_ground_term t cs;
          define_nested (C.rewrite_ground_terms symdb cs) in

  (* Define remaining ground terms in clauses with at least two literals. *)
  let rec define_in_long_clauses cs =
    let find_in_lit lit = match%earr lit with
      | L.Lit (_, s, [| l; r |]) when s = Symb.sym_eq ->
          List.iter raise_when_ground_term_depth_two [l; r]
      | L.Lit _ -> () in

    try
      BatDynArray.iter
        (fun lits ->
          if List.length lits >= 2 then List.iter find_in_lit lits)
        cs;
      cs
    with
      | Term t ->
          define_ground_term t cs;
          define_in_long_clauses (C.rewrite_ground_terms symdb cs) in

  (* Define remaining ground terms in unit equality clauses l ?= r
     where both l and r are proper functions.
  *)
  let rec define_in_unit_eq_clauses sign cs =
    try
      BatDynArray.iter
        (fun lits -> match%earr lits with
        | [ L.Lit (sign', s, [| l; r |]) ]
          when
            sign' = sign &&
            s = Symb.sym_eq &&
            List.for_all T.is_proper_func [l; r] ->
            List.iter raise_when_ground_term_depth_two [l; r]
        | _ -> ())
        cs;
      cs
    with
      | Term t ->
          define_ground_term t cs;
          define_in_unit_eq_clauses sign (C.rewrite_ground_terms symdb cs) in

  clauses
  |> C.rewrite_ground_terms symdb
  |> define_nested
  |> define_in_long_clauses
  |> define_in_unit_eq_clauses Sh.Pos
  |> define_in_unit_eq_clauses Sh.Neg
