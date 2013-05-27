(* Copyright (c) 2013 Radek Micek *)

open OUnit

module Solver = struct

  type lbool =
    | Ltrue
    | Lfalse
    | Lundef

  type var = int

  type lit = int

  type event =
    | Enew_var of var
    | Enew_false_var of var
    | Eadd_clause of lit array
    | Eadd_symmetry_clause of lit array
    | Eadd_at_least_one_val_clause of lit array
    | Eadd_at_most_one_val_clause of lit array
    | Eremove_clauses_with_lit of lit
    | Esolve of lit array

  type t = {
    log : event BatDynArray.t;
    mutable nvars : int;
  }

  type sign =
    | Pos
    | Neg

  let create () =
    {
      log = BatDynArray.create ();
      nvars = 0;
    }

  let new_var s =
    let v = s.nvars in
    s.nvars <- v + 1;
    BatDynArray.add s.log (Enew_var v);
    v

  let new_false_var s =
    let v = s.nvars in
    s.nvars <- v + 1;
    BatDynArray.add s.log (Enew_false_var v);
    v

  let add_clause s lits len =
    let cl = Array.sub lits 0 len in
    BatDynArray.add s.log (Eadd_clause cl);
    true

  let add_symmetry_clause s lits len =
    let cl = Array.sub lits 0 len in
    BatDynArray.add s.log (Eadd_symmetry_clause cl);
    true

  let add_at_least_one_val_clause s lits len =
    let cl = Array.sub lits 0 len in
    BatDynArray.add s.log (Eadd_at_least_one_val_clause cl);
    true

  let add_at_most_one_val_clause s lits =
    let cl = Array.sub lits 0 2 in
    BatDynArray.add s.log (Eadd_at_most_one_val_clause cl);
    true

  let remove_clauses_with_lit s l =
    BatDynArray.add s.log (Eremove_clauses_with_lit l)

  let solve s assumpts =
    BatDynArray.add s.log (Esolve (Array.copy assumpts));
    Lundef

  let model_value _ _ = failwith "Not implemented"

  let to_lit sign v = match sign with
    | Pos -> v + v
    | Neg -> v + v + 1

  let lit_sign lit =
    if lit mod 2 = 0 then Pos else Neg

  let to_var lit = lit / 2
end

module Inst = Sat_inst.Make (Solver)

let assert_log i exp_log =
  let log = BatDynArray.to_list (Inst.get_solver i).Solver.log in
  assert_equal exp_log log;
  BatDynArray.clear (Inst.get_solver i).Solver.log

let print_log i =
  let lit_to_str lit = match Solver.lit_sign lit with
    | Solver.Pos -> string_of_int (Solver.to_var lit)
    | Solver.Neg -> "~" ^ string_of_int (Solver.to_var lit) in
  let cl_to_str cl =
    String.concat ", " (List.map lit_to_str (Array.to_list cl)) in
  print_endline "Log:";
  BatDynArray.iter
    (function
    | Solver.Enew_var x -> Printf.printf "new var: %d\n" x
    | Solver.Enew_false_var x -> Printf.printf "new false var: %d\n" x
    | Solver.Eadd_clause cl ->
        Printf.printf "add clause: %s\n" (cl_to_str cl)
    | Solver.Eadd_symmetry_clause cl ->
        Printf.printf "add symmetry clause: %s\n" (cl_to_str cl)
    | Solver.Eadd_at_least_one_val_clause cl ->
        Printf.printf "add at least one val clause: %s\n" (cl_to_str cl)
    | Solver.Eadd_at_most_one_val_clause cl ->
        Printf.printf "add at most one val clause: %s\n" (cl_to_str cl)
    | Solver.Eremove_clauses_with_lit lit ->
        Printf.printf "remove clauses with lit: %s\n" (lit_to_str lit)
    | Solver.Esolve assumpts ->
        Printf.printf "solve: %s\n" (cl_to_str assumpts))
    (Inst.get_solver i).Solver.log

let lit v = Solver.to_lit Solver.Pos v
let lit' v = Solver.to_lit Solver.Neg v

module T = Term
module C = Clause

let test_no_symbols_only_clause () =
  let prob = Prob.create () in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let z = T.Var 2 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    C.cl_lits = [ T.mk_eq x y; T.mk_eq x z ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  (* Log is empty since all clauses are true. *)
  assert_log i [];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 0;
      Solver.Esolve [| lit' 0 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 0) ::
       (* For: x = 1, y = 0, z = 0 and x = 0, y = 1, z = 1 *)
       BatList.make (2 - 0) (Solver.Eadd_clause [| |]));
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 1;
      Solver.Esolve [| lit' 1 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 1) ::
       BatList.make (12 - 2) (Solver.Eadd_clause [| |]));
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 2;
      Solver.Esolve [| lit' 2 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 2) ::
       BatList.make (36 - 12) (Solver.Eadd_clause [| |]));
  assert_equal 4 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 3;
      Solver.Esolve [| lit' 3 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 3) ::
       BatList.make (80 - 36) (Solver.Eadd_clause [| |]));
  assert_equal 5 (Inst.get_max_size i);
  (* Skip: Inst.solve *)

  Inst.incr_max_size i;
  assert_log i
    (BatList.make (6 * 25 - 80) (Solver.Eadd_clause [| |]));
  assert_equal 6 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 4;
      Solver.Esolve [| lit' 4 |];
    ]

let test_nullary_preds () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let p = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let q = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let r = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    C.cl_lits = [ p; T.mk_eq x y; C.neg_lit q ];
  } in
  let clause2 = {
    C.cl_id = Prob.fresh_id prob;
    C.cl_lits = [ C.neg_lit r; q ];
  } in
  List.iter
    (BatDynArray.add prob.Prob.clauses)
    [clause; clause2];
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  let var_p = 0 in
  let var_q = 1 in
  let var_r = 2 in
  assert_log i
    [
      Solver.Enew_var var_p;
      Solver.Enew_var var_q;
      Solver.Enew_var var_r;
      Solver.Eadd_clause [| lit' var_r; lit var_q |];
    ];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i [];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 3;
      Solver.Esolve [| lit' 3 |];
    ];

  let ev_add_clause_pq =
    Solver.Eadd_clause [| lit var_p; lit' var_q |] in

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 3) ::
       BatList.make 2 ev_add_clause_pq);
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 4;
      Solver.Esolve [| lit' 4 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    (Solver.Eremove_clauses_with_lit (lit 4) ::
       BatList.make 4 ev_add_clause_pq);
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 5;
      Solver.Esolve [| lit' 5 |];
    ]

let test_constants () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let c = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let d = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let x = T.Var 0 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* c <> d *)
    C.cl_lits = [ T.mk_ineq c x; T.mk_ineq x d ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: c = 0 *)
      Solver.Enew_var 1; (* For: d = 0 *)
      Solver.Eadd_symmetry_clause [| lit 1 |];
      Solver.Eadd_clause [| lit' 0; lit' 1 |];
    ];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 2;
      Solver.Eadd_at_least_one_val_clause [| lit 0; lit 2 |];
      Solver.Esolve [| lit' 2 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 3; (* For: c = 1 *)
      Solver.Enew_var 4; (* For: d = 1 *)
      Solver.Eremove_clauses_with_lit (lit 2);
      Solver.Eadd_symmetry_clause [| lit 0; lit 3 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 3; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 4; lit' 1 |];
      Solver.Eadd_clause [| lit' 3; lit' 4 |];
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 5;
      Solver.Esolve [| lit' 5 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Eremove_clauses_with_lit (lit 5);
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 6;
      Solver.Esolve [| lit' 6 |]
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Eremove_clauses_with_lit (lit 6);
    ];
  assert_equal 4 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 7;
      Solver.Esolve [| lit' 7 |]
    ]

let test_distinct_consts () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let c = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let d =
    let s = Symb.add_anon_symb db 0 in
    BatDynArray.add prob.Prob.distinct_consts s;
    T.Func (s, [| |]) in
  let d2 =
    let s = Symb.add_anon_symb db 0 in
    BatDynArray.add prob.Prob.distinct_consts s;
    T.Func (s, [| |]) in
  let c2 = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* c = x, x <> d *)
    C.cl_lits = [ T.mk_eq c x; T.mk_ineq x d ];
  } in
  let clause2 = {
    C.cl_id = Prob.fresh_id prob;
    (* c2 <> y, d2 = x *)
    C.cl_lits = [ T.mk_ineq c2 y; T.mk_eq d2 x ];
  } in
  List.iter
    (BatDynArray.add prob.Prob.clauses)
    [clause; clause2];
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: c = 0 *)
      Solver.Enew_var 1; (* For: d = 0 *)
      Solver.Enew_var 2; (* For: d2 = 0 *)
      Solver.Enew_var 3; (* For: c2 = 0 *)
      Solver.Eadd_symmetry_clause [| lit 1 |]; (* d = 0 *)
      Solver.Eadd_symmetry_clause [| lit 3 |]; (* c2 = 0 *)
      Solver.Eadd_clause [| lit 0; lit' 1 |];
      Solver.Eadd_clause [| lit' 3; lit 2 |];
    ];
  assert_equal 1 (Inst.get_max_size i);
  (* (Inst.solve i) raises Failure since there are 2 distinct constants. *)

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 4; (* For: c = 1 *)
      Solver.Enew_var 5; (* For: d = 1 *)
      Solver.Enew_var 6; (* For: d2 = 1 *)
      Solver.Eadd_symmetry_clause [| lit 6 |]; (* d2 = 1 *)
      Solver.Eadd_at_most_one_val_clause [| lit' 4; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 5; lit' 1 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 6; lit' 2 |];
      Solver.Eadd_clause [| lit 4; lit' 5 |];
      Solver.Eadd_clause [| lit' 3; lit 6 |]; (* clause2: x = 1, y = 0 *)
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 7;
      Solver.Eadd_at_least_one_val_clause [| lit 0; lit 4; lit 7 |]; (* c *)
      Solver.Esolve [| lit' 7 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 8; (* For: c = 2 *)
      Solver.Enew_var 9; (* For: d = 2 *)
      Solver.Enew_var 10; (* For: d2 = 2 *)
      Solver.Eremove_clauses_with_lit (lit 7);
      Solver.Eadd_symmetry_clause [| lit 0; lit 4; lit 8 |]; (* c *)
      Solver.Eadd_at_most_one_val_clause [| lit' 8; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 8; lit' 4 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 9; lit' 1 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 9; lit' 5 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 10; lit' 2 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 10; lit' 6 |];
      Solver.Eadd_clause [| lit 8; lit' 9 |];
      Solver.Eadd_clause [| lit' 3; lit 10 |]; (* clause2: x = 2, y = 0 *)
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 11;
      Solver.Esolve [| lit' 11 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 12; (* For: c = 3 *)
      Solver.Enew_var 13; (* For: d = 3 *)
      Solver.Enew_var 14; (* For: d2 = 3 *)
      Solver.Eremove_clauses_with_lit (lit 11);
      Solver.Eadd_at_most_one_val_clause [| lit' 12; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 12; lit' 4 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 12; lit' 8 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 13; lit' 1 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 13; lit' 5 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 13; lit' 9 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 14; lit' 2 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 14; lit' 6 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 14; lit' 10 |];
      Solver.Eadd_clause [| lit 12; lit' 13 |];
      Solver.Eadd_clause [| lit' 3; lit 14 |]; (* clause2: x = 3, y = 0 *)
    ];
  assert_equal 4 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 15;
      Solver.Esolve [| lit' 15 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Eremove_clauses_with_lit (lit 15);
    ];
  assert_equal 5 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 16;
      Solver.Esolve [| lit' 16 |];
    ]

let test_unary_func () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f =
    let s = Symb.add_anon_symb db 1 in
    fun a -> T.Func (s, [| a |]) in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* f(x) = y *)
    C.cl_lits = [ T.mk_eq (f x) y ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: f(0) = 0 *)
      Solver.Eadd_symmetry_clause [| 0 |];
      Solver.Eadd_clause [| lit 0 |];
    ];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 1;
      Solver.Esolve [| lit' 1 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 2; (* For: f(0) = 1 *)
      Solver.Eremove_clauses_with_lit (lit 1);
      Solver.Eadd_at_most_one_val_clause [| lit' 2; lit' 0 |];
      Solver.Eadd_clause [| lit 2 |];
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 3;
      Solver.Esolve [| lit' 3 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 4; (* For: f(0) = 2 *)
      Solver.Eremove_clauses_with_lit (lit 3);
      Solver.Eadd_at_most_one_val_clause [| lit' 4; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 4; lit' 2 |];
      Solver.Eadd_clause [| lit 4 |];
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 5;
      Solver.Esolve [| lit' 5 |];
    ]

let test_unary_pred () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let c = T.Func (Symb.add_anon_symb db 0, [| |]) in
  let p =
    let s = Symb.add_anon_symb db 1 in
    fun a -> T.Func (s, [| a |]) in
  let x = T.Var 0 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* ~p(x), x = c *)
    C.cl_lits = [ C.neg_lit (p x); T.mk_eq x c  ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: c = 0 *)
      Solver.Enew_var 1; (* For: p(0) *)
      Solver.Eadd_symmetry_clause [| 0 |];
      Solver.Eadd_clause [| lit' 1; lit 0 |];
    ];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 2;
      Solver.Esolve [| lit' 2 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 3; (* For: c = 1 *)
      Solver.Enew_var 4; (* For: p(1) *)
      Solver.Eremove_clauses_with_lit (lit 2);
      Solver.Eadd_at_most_one_val_clause [| lit' 3; lit' 0 |];
      Solver.Eadd_clause [| lit' 4; lit 3 |];
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 5;
      Solver.Esolve [| lit' 5 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Eremove_clauses_with_lit (lit 5);
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 6;
      Solver.Esolve [| lit' 6 |];
    ]

let test_commutative_func () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f =
    let s = Symb.add_anon_symb db 2 in
    Symb.set_commutative db s true;
    fun a b -> T.Func (s, [| a; b |]) in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* f(x, y) = y *)
    C.cl_lits = [ T.mk_eq (f x y) y ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: f(0, 0) = 0 *)
      Solver.Eadd_clause [| lit 0 |];
    ];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 1;
      Solver.Eadd_at_least_one_val_clause [| lit 0; lit 1 |];
      Solver.Esolve [| lit' 1 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 2; (* For: f(0, 1) = 0 *)
      Solver.Enew_var 3; (* For: f(0, 1) = 1 *)
      Solver.Enew_var 4; (* For: f(1, 1) = 0 *)
      Solver.Enew_var 5; (* For: f(1, 1) = 1 *)
      Solver.Enew_var 6; (* For: f(0, 0) = 1 *)
      Solver.Eremove_clauses_with_lit (lit 1);
      (* f(0, 0) = 0, f(0, 0) = 1 *)
      Solver.Eadd_symmetry_clause [| lit 0; lit 6 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 6; lit' 0 |]; (* f(0, 0) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 2; lit' 3 |]; (* f(0, 1) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 4; lit' 5 |]; (* f(1, 1) *)
      Solver.Eadd_clause [| lit 2 |]; (* x = 1, y = 0 *)
      Solver.Eadd_clause [| lit 5 |]; (* x = 1, y = 1 *)
      Solver.Eadd_clause [| lit 3 |]; (* x = 0, y = 1 *)
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 7;
      (* f(0, 1) *)
      Solver.Eadd_at_least_one_val_clause [| lit 2; lit 3; lit 7 |];
      (* f(1, 1) *)
      Solver.Eadd_at_least_one_val_clause [| lit 4; lit 5; lit 7 |];
      Solver.Esolve [| lit' 7 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 8; (* For: f(0, 2) = 0 *)
      Solver.Enew_var 9; (* For: f(0, 2) = 1 *)
      Solver.Enew_var 10; (* For: f(0, 2) = 2 *)
      Solver.Enew_var 11; (* For: f(1, 2) = 0 *)
      Solver.Enew_var 12; (* For: f(1, 2) = 1 *)
      Solver.Enew_var 13; (* For: f(1, 2) = 2 *)
      Solver.Enew_var 14; (* For: f(2, 2) = 0 *)
      Solver.Enew_var 15; (* For: f(2, 2) = 1 *)
      Solver.Enew_var 16; (* For: f(2, 2) = 2 *)
      Solver.Enew_var 17; (* For: f(0, 0) = 2 *)
      Solver.Enew_var 18; (* For: f(0, 1) = 2 *)
      Solver.Enew_var 19; (* For: f(1, 1) = 2 *)
      Solver.Eremove_clauses_with_lit (lit 7);
      (* f(0, 1) = 0, f(0, 1) = 1, f(0, 1) = 2 *)
      Solver.Eadd_symmetry_clause [| lit 2; lit 3; lit 18; |];
      (* f(0, 0) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 17; lit' 0 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 17; lit' 6 |];
      (* f(0, 1) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 18; lit' 2 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 18; lit' 3 |];
      (* f(1, 1) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 19; lit' 4 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 19; lit' 5 |];
      (* f(0, 2) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 8; lit' 9 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 8; lit' 10 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 9; lit' 10 |];
      (* f(1, 2) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 11; lit' 12 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 11; lit' 13 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 12; lit' 13 |];
      (* f(2, 2) *)
      Solver.Eadd_at_most_one_val_clause [| lit' 14; lit' 15 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 14; lit' 16 |];
      Solver.Eadd_at_most_one_val_clause [| lit' 15; lit' 16 |];
      Solver.Eadd_clause [| lit 8 |]; (* x = 2, y = 0 *)
      Solver.Eadd_clause [| lit 12 |]; (* x = 2, y = 1 *)
      Solver.Eadd_clause [| lit 16 |]; (* x = 2, y = 2 *)
      Solver.Eadd_clause [| lit 10 |]; (* x = 0, y = 2 *)
      Solver.Eadd_clause [| lit 13 |]; (* x = 1, y = 2 *)
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 20;
      (* f(1, 1) *)
      Solver.Eadd_at_least_one_val_clause
        [| lit 4; lit 5; lit 19; lit 20 |];
      (* f(0, 2) *)
      Solver.Eadd_at_least_one_val_clause
        [| lit 8; lit 9; lit 10; lit 20 |];
      (* f(1, 2) *)
      Solver.Eadd_at_least_one_val_clause
        [| lit 11; lit 12; lit 13; lit 20 |];
      (* f(2, 2) *)
      Solver.Eadd_at_least_one_val_clause
        [| lit 14; lit 15; lit 16; lit 20 |];
      Solver.Esolve [| lit' 20 |];
    ]

let test_symmetric_pred () =
  let prob = Prob.create () in
  let db = prob.Prob.symbols in
  let p =
    let s = Symb.add_anon_symb db 2 in
    Symb.set_commutative db s true;
    fun a b -> T.Func (s, [| a; b |]) in
  let x = T.Var 0 in
  let y = T.Var 1 in
  let clause = {
    C.cl_id = Prob.fresh_id prob;
    (* p(x, y), x = y *)
    C.cl_lits = [ p x y; T.mk_eq x y ];
  } in
  BatDynArray.add prob.Prob.clauses clause;
  let sorts = Sorts.of_problem prob in

  let i = Inst.create prob sorts in
  assert_log i [];
  assert_equal 0 (Inst.get_max_size i);

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 0; (* For: p(0, 0) *)
    ];
  assert_equal 1 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 1;
      Solver.Esolve [| lit' 1 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 2; (* For: p(0, 1) *)
      Solver.Enew_var 3; (* For: p(1, 1) *)
      Solver.Eremove_clauses_with_lit (lit 1);
      Solver.Eadd_clause [| lit 2 |]; (* x = 1, y = 0 *)
      Solver.Eadd_clause [| lit 2 |]; (* x = 0, y = 1 *)
    ];
  assert_equal 2 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 4;
      Solver.Esolve [| lit' 4 |];
    ];

  Inst.incr_max_size i;
  assert_log i
    [
      Solver.Enew_var 5; (* For: p(0, 2) *)
      Solver.Enew_var 6; (* For: p(1, 2) *)
      Solver.Enew_var 7; (* For: p(2, 2) *)
      Solver.Eremove_clauses_with_lit (lit 4);
      Solver.Eadd_clause [| lit 5 |]; (* x = 2, y = 0 *)
      Solver.Eadd_clause [| lit 6 |]; (* x = 2, y = 1 *)
      Solver.Eadd_clause [| lit 5 |]; (* x = 0, y = 2 *)
      Solver.Eadd_clause [| lit 6 |]; (* x = 1, y = 2 *)
    ];
  assert_equal 3 (Inst.get_max_size i);
  assert_equal Solver.Lundef (Inst.solve i);
  assert_log i
    [
      Solver.Enew_false_var 8;
      Solver.Esolve [| lit' 8 |];
    ]

let suite =
  "Sat_inst suite" >:::
    [
      "no symbols, only clause" >:: test_no_symbols_only_clause;
      "nullary preds" >:: test_nullary_preds;
      "constants" >:: test_constants;
      "distinct consts" >:: test_distinct_consts;
      "unary func" >:: test_unary_func;
      "unary pred" >:: test_unary_pred;
      "commutative_func" >:: test_commutative_func;
      "symmetric_pred" >:: test_symmetric_pred;
    ]