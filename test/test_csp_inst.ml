(* Copyright (c) 2013 Radek Micek *)

open OUnit

module Solver = struct
  type 'a var = int
  type 'a var_array = int

  type event =
    | Enew_bool_var of bool var
    | Enew_int_var of int * int var
    | Enew_tmp_bool_var of bool var
    | Enew_tmp_int_var of int * int var
    | Enew_bool_var_array of bool var array * bool var_array
    | Enew_int_var_array of int var array * int var_array
    | Ebool_element of bool var_array * int var * bool var
    | Eint_element of int var_array * int var * int var
    | Elinear of int var array * int array * int
    | Eeq_var_var of int var * int var * bool var
    | Eeq_var_const of int var * int * bool var
    | Eclause of bool var array * bool var array
    | Eall_different of int var array

  type t = {
    log : event BatDynArray.t;
    mutable last_bool_var : int;
    mutable last_int_var : int;
    mutable last_tmp_bool_var : int;
    mutable last_tmp_int_var : int;
    mutable last_bool_var_array : int;
    mutable last_int_var_array : int;
  }

  let create _ =
    {
      log = BatDynArray.create ();
      last_bool_var = ~-1;
      last_int_var = ~-1;
      last_tmp_bool_var = 0;
      last_tmp_int_var = 0;
      last_bool_var_array = ~-1;
      last_int_var_array = ~-1;
    }

  let new_bool_var s =
    s.last_bool_var <- s.last_bool_var + 1;
    BatDynArray.add s.log (Enew_bool_var s.last_bool_var);
    s.last_bool_var

  let new_int_var s dom_size =
    s.last_int_var <- s.last_int_var + 1;
    BatDynArray.add s.log (Enew_int_var (dom_size, s.last_int_var));
    s.last_int_var

  let new_tmp_bool_var s =
    s.last_tmp_bool_var <- s.last_tmp_bool_var - 1;
    BatDynArray.add s.log (Enew_tmp_bool_var s.last_tmp_bool_var);
    s.last_tmp_bool_var

  let new_tmp_int_var s dom_size =
    s.last_tmp_int_var <- s.last_tmp_int_var - 1;
    BatDynArray.add s.log (Enew_tmp_int_var (dom_size, s.last_tmp_int_var));
    s.last_tmp_int_var

  let new_bool_var_array s arr =
    s.last_bool_var_array <- s.last_bool_var_array + 1;
    let idx = s.last_bool_var_array in
    BatDynArray.add s.log (Enew_bool_var_array (arr, idx));
    idx

  let new_int_var_array s arr =
    s.last_int_var_array <- s.last_int_var_array + 1;
    let idx = s.last_int_var_array in
    BatDynArray.add s.log (Enew_int_var_array (arr, idx));
    idx

  let bool_element s arr i y =
    BatDynArray.add s.log (Ebool_element (arr, i, y))

  let int_element s arr i y =
    BatDynArray.add s.log (Eint_element (arr, i, y))

  let linear s vars coefs c =
    BatDynArray.add s.log (Elinear (vars, coefs, c))

  let eq_var_var s x x' y =
    BatDynArray.add s.log (Eeq_var_var (x, x', y))

  let eq_var_const s x c y =
    BatDynArray.add s.log (Eeq_var_const (x, c, y))

  let clause s pos_lits neg_lits =
    BatDynArray.add s.log (Eclause (Array.copy pos_lits, Array.copy neg_lits))

  let all_different s vars =
    BatDynArray.add s.log (Eall_different (Array.copy vars))

  let solve s = Sh.Lundef

  let interrupt _ = failwith "Not implemented"

  let bool_value _ _ = failwith "Not implemented"
  let int_value _ _ = failwith "Not implemented"
end

module Inst = Csp_inst.Make (Solver)
module Solv = Solver

let assert_log i exp_log =
  let log = BatDynArray.to_list (Inst.get_solver i).Solver.log in
  assert_equal exp_log log;
  BatDynArray.clear (Inst.get_solver i).Solver.log

let print_log i =
  let int_arr_to_str arr =
    let s = String.concat ", " (List.map string_of_int (Array.to_list arr)) in
    "[" ^ s ^ "]" in
  print_endline "Log:";
  BatDynArray.iter
    (function
    | Solv.Enew_bool_var x ->
        Printf.printf "new bool var: %d\n" x
    | Solv.Enew_int_var (dom_size, x) ->
        Printf.printf "new int var: %d %d\n" dom_size x
    | Solv.Enew_tmp_bool_var x ->
        Printf.printf "new tmp bool var: %d\n" x
    | Solv.Enew_tmp_int_var (dom_size, x) ->
        Printf.printf "new tmp int var: %d %d\n" dom_size x
    | Solv.Enew_bool_var_array (arr, id) ->
        Printf.printf "new bool var array: %s %d\n"
          (int_arr_to_str arr) id
    | Solv.Enew_int_var_array (arr, id) ->
        Printf.printf "new int var array: %s %d\n"
          (int_arr_to_str arr) id
    | Solv.Ebool_element (arr, x, y) ->
        Printf.printf "bool element: %d %d %d\n"
          arr x y
    | Solv.Eint_element (arr, x, y) ->
        Printf.printf "int element: %d %d %d\n"
          arr x y
    | Solv.Elinear (vars, coefs, c) ->
        Printf.printf "linear: %s %s %d\n"
          (int_arr_to_str vars) (int_arr_to_str coefs) c
    | Solv.Eeq_var_var (x, x', y) ->
        Printf.printf "eq_var_var: %d %d %d\n" x x' y
    | Solv.Eeq_var_const (x, c, y) ->
        Printf.printf "eq_var_var: %d %d %d\n" x c y
    | Solv.Eclause (pos, neg) ->
        Printf.printf "clause: %s %s\n"
          (int_arr_to_str pos) (int_arr_to_str neg)
    | Solv.Eall_different vars ->
        Printf.printf "all_different: %s\n"
          (int_arr_to_str vars))
    (Inst.get_solver i).Solver.log

module S = Symb
module T = Term
module L = Lit
module C2 = Clause2

let test_empty () =
  let Prob.Wr prob = Prob.create () in

  for max_size = 1 to 20 do
    assert_log (Inst.create prob max_size) []
  done

let test_flat_pred () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let p = Symb.add_pred db 3 in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.lit (Sh.Pos, p, [| T.var 1; T.var 0; T.var 1 |]) ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i1 = Inst.create prob 1 in
  assert_log i1 [
    Solv.Enew_bool_var 0;
    Solv.Enew_bool_var_array ([| 0 |], 0);
    Solv.Eclause ([| 0 |], [| |]);
  ];

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_bool_var 0; (* p(0, 0, 0) *)
    Solv.Enew_bool_var 1; (* p(0, 0, 1) *)
    Solv.Enew_bool_var 2; (* p(0, 1, 0) *)
    Solv.Enew_bool_var 3; (* p(0, 1, 1) *)
    Solv.Enew_bool_var 4; (* p(1, 0, 0) *)
    Solv.Enew_bool_var 5; (* p(1, 0, 1) *)
    Solv.Enew_bool_var 6; (* p(1, 1, 0) *)
    Solv.Enew_bool_var 7; (* p(1, 1, 1) *)
    Solv.Enew_bool_var_array ([| 0; 1; 2; 3; 4; 5; 6; 7 |], 0);
    Solv.Eclause ([| 0 |], [| |]);
    Solv.Eclause ([| 5 |], [| |]);
    Solv.Eclause ([| 2 |], [| |]);
    Solv.Eclause ([| 7 |], [| |]);
  ]

let test_flat_func () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 2 in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.mk_ineq
        (T.var 0)
        (T.func (f, [| T.var 0; T.var 1 |]));
    ];
  } in
  let clause2 = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.mk_eq
        (T.func (f, [| T.var 0; T.var 1 |]))
        (T.func (f, [| T.var 0; T.var 0 |]));
    ];
  } in
  List.iter (BatDynArray.add prob.Prob.clauses) [clause; clause2];

  let i1 = Inst.create prob 1 in
  assert_log i1 [
    Solv.Enew_int_var (1, 0);
    Solv.Enew_int_var_array ([| 0 |], 0);
    (* Clause 1. *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1);
    Solv.Eclause ([| |], [| ~-1 |]);
    (* Clause 2. *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_var (0, 0, ~-2);
    Solv.Eclause ([| ~-2 |], [| |]);
  ];

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_int_var (2, 0); (* f(0, 0) *)
    Solv.Enew_int_var (2, 1); (* f(0, 1) *)
    Solv.Enew_int_var (2, 2); (* f(1, 0) *)
    Solv.Enew_int_var (2, 3); (* f(1, 1) *)
    Solv.Enew_int_var_array ([| 0; 1; 2; 3 |], 0);
    (* Clause 1. *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1); (* f(0, 0) = 0 *)
    Solv.Eclause ([| |], [| ~-1 |]);
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_const (1, 0, ~-2); (* f(0, 1) = 0 *)
    Solv.Eclause ([| |], [| ~-2 |]);
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_const (2, 1, ~-3); (* f(1, 0) = 1 *)
    Solv.Eclause ([| |], [| ~-3 |]);
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_const (3, 1, ~-4); (* f(1, 1) = 1 *)
    Solv.Eclause ([| |], [| ~-4 |]);
    (* Clause 2. *)
    Solv.Enew_tmp_bool_var ~-5;
    Solv.Eeq_var_var (0, 0, ~-5); (* f(0, 0) = f(0, 0) *)
    Solv.Eclause ([| ~-5 |], [| |]);
    Solv.Enew_tmp_bool_var ~-6;
    Solv.Eeq_var_var (0, 1, ~-6); (* f(0, 1) = f(0, 0) *)
    Solv.Eclause ([| ~-6 |], [| |]);
    Solv.Enew_tmp_bool_var ~-7;
    Solv.Eeq_var_var (2, 3, ~-7); (* f(1, 0) = f(1, 1) *)
    Solv.Eclause ([| ~-7 |], [| |]);
    Solv.Enew_tmp_bool_var ~-8;
    Solv.Eeq_var_var (3, 3, ~-8); (* f(1, 1) = f(1, 1) *)
    Solv.Eclause ([| ~-8 |], [| |]);
  ]

let test_flat_comm_func () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 2 in
  S.set_commutative db f true;
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.mk_eq
        (T.var 0)
        (T.func (f, [| T.var 0; T.var 1 |]));
    ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i1 = Inst.create prob 1 in
  assert_log i1 [
    Solv.Enew_int_var (1, 0);
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1);
    Solv.Eclause ([| ~-1 |], [| |]);
  ];

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_int_var (2, 0); (* f(0, 0) *)
    Solv.Enew_int_var (2, 1); (* f(0, 1) *)
    Solv.Enew_int_var (2, 2); (* f(1, 1) *)
    Solv.Enew_int_var_array ([| 0; 1; 1; 2 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1); (* f(0, 0) = 0 *)
    Solv.Eclause ([| ~-1 |], [| |]);
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_const (1, 0, ~-2); (* f(0, 1) = 0 *)
    Solv.Eclause ([| ~-2 |], [| |]);
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_const (1, 1, ~-3); (* f(1, 0) = 1 *)
    Solv.Eclause ([| ~-3 |], [| |]);
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_const (2, 1, ~-4); (* f(1, 1) = 1 *)
    Solv.Eclause ([| ~-4 |], [| |]);
  ];

  let i3 = Inst.create prob 3 in
  assert_log i3 [
    Solv.Enew_int_var (3, 0); (* f(0, 0) *)
    Solv.Enew_int_var (3, 1); (* f(0, 1) *)
    Solv.Enew_int_var (3, 2); (* f(1, 1) *)
    Solv.Enew_int_var (3, 3); (* f(0, 2) *)
    Solv.Enew_int_var (3, 4); (* f(1, 2) *)
    Solv.Enew_int_var (3, 5); (* f(2, 2) *)
    Solv.Enew_int_var_array ([| 0; 1; 3; 1; 2; 4; 3; 4; 5 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1); (* f(0, 0) = 0 *)
    Solv.Eclause ([| ~-1 |], [| |]);
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_const (1, 0, ~-2); (* f(0, 1) = 0 *)
    Solv.Eclause ([| ~-2 |], [| |]);
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_const (3, 0, ~-3); (* f(0, 2) = 0 *)
    Solv.Eclause ([| ~-3 |], [| |]);
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_const (1, 1, ~-4); (* f(1, 0) = 1 *)
    Solv.Eclause ([| ~-4 |], [| |]);
    Solv.Enew_tmp_bool_var ~-5;
    Solv.Eeq_var_const (2, 1, ~-5); (* f(1, 1) = 1 *)
    Solv.Eclause ([| ~-5 |], [| |]);
    Solv.Enew_tmp_bool_var ~-6;
    Solv.Eeq_var_const (4, 1, ~-6); (* f(1, 2) = 1 *)
    Solv.Eclause ([| ~-6 |], [| |]);
    Solv.Enew_tmp_bool_var ~-7;
    Solv.Eeq_var_const (3, 2, ~-7); (* f(2, 0) = 2 *)
    Solv.Eclause ([| ~-7 |], [| |]);
    Solv.Enew_tmp_bool_var ~-8;
    Solv.Eeq_var_const (4, 2, ~-8); (* f(2, 1) = 2 *)
    Solv.Eclause ([| ~-8 |], [| |]);
    Solv.Enew_tmp_bool_var ~-9;
    Solv.Eeq_var_const (5, 2, ~-9); (* f(2, 2) = 2 *)
    Solv.Eclause ([| ~-9 |], [| |]);
  ]

let test_nested () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let p = Symb.add_pred db 3 in
  let f = Symb.add_func db 2 in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.lit (Sh.Neg, p,
             [|
               T.func (f, [| T.var 0; c |]);
               T.var 0;
               T.func (f, [| c; T.var 0 |]);
             |]);
    ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i1 = Inst.create prob 1 in
  assert_log i1 [
    Solv.Enew_bool_var 0; (* p(0, 0, 0) *)
    Solv.Enew_bool_var_array ([| 0 |], 0);
    Solv.Enew_int_var (1, 0); (* f(0, 0) *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_int_var (1, 1); (* c *)
    Solv.Enew_int_var_array ([| 1 |], 1);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (1, ~-1);
    Solv.Eint_element (0, 1, ~-1);
    (* f(0, c) shares variable with f(c, 0) *)
    (* p(f(0, c), 0, f(c, 0)) *)
    Solv.Enew_tmp_int_var (1, ~-2);
    Solv.Elinear ([| ~-1; ~-2 |], [| 2; ~-1 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, ~-2, ~-1);
    Solv.Eclause ([| |], [| ~-1 |]);
  ];

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_bool_var 0; (* p(0, 0, 0) *)
    Solv.Enew_bool_var 1; (* p(0, 0, 1) *)
    Solv.Enew_bool_var 2; (* p(0, 1, 0) *)
    Solv.Enew_bool_var 3; (* p(0, 1, 1) *)
    Solv.Enew_bool_var 4; (* p(1, 0, 0) *)
    Solv.Enew_bool_var 5; (* p(1, 0, 1) *)
    Solv.Enew_bool_var 6; (* p(1, 1, 0) *)
    Solv.Enew_bool_var 7; (* p(1, 1, 1) *)
    Solv.Enew_bool_var_array ([| 0; 1; 2; 3; 4; 5; 6; 7 |], 0);
    Solv.Enew_int_var (2, 0); (* f(0, 0) *)
    Solv.Enew_int_var (2, 1); (* f(0, 1) *)
    Solv.Enew_int_var (2, 2); (* f(1, 0) *)
    Solv.Enew_int_var (2, 3); (* f(1, 1) *)
    Solv.Enew_int_var_array ([| 0; 1; 2; 3 |], 0);
    Solv.Enew_int_var (2, 4); (* c *)
    Solv.Enew_int_var_array ([| 4 |], 1);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (3, ~-1);
    Solv.Elinear ([| 4; ~-1 |], [| 2; ~-1 |], 0);
    Solv.Enew_tmp_int_var (2, ~-2);
    Solv.Eint_element (0, ~-1, ~-2);
    (* f(0, c) *)
    Solv.Enew_tmp_int_var (2, ~-3);
    Solv.Eint_element (0, 4, ~-3);
    (* p(f(0, c), 0, f(c, 0)) *)
    Solv.Enew_tmp_int_var (6, ~-4);
    Solv.Elinear ([| ~-3; ~-2; ~-4 |], [| 4; 1; ~-1 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, ~-4, ~-1);
    Solv.Eclause ([| |], [| ~-1 |]);
    (* f(c, 1) *)
    Solv.Enew_tmp_int_var (4, ~-5);
    Solv.Elinear ([| 4; ~-5 |], [| 2; ~-1 |], ~-1);
    Solv.Enew_tmp_int_var (2, ~-6);
    Solv.Eint_element (0, ~-5, ~-6);
    (* f(1, c) *)
    Solv.Enew_tmp_int_var (4, ~-7);
    Solv.Elinear ([| 4; ~-7 |], [| 1; ~-1 |], ~-2);
    Solv.Enew_tmp_int_var (2, ~-8);
    Solv.Eint_element (0, ~-7, ~-8);
    (* p(f(1, c), 1, f(c, 1)) *)
    Solv.Enew_tmp_int_var (8, ~-9);
    Solv.Elinear ([| ~-8; ~-6; ~-9 |], [| 4; 1; ~-1 |], ~-2);
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Ebool_element (0, ~-9, ~-2);
    Solv.Eclause ([| |], [| ~-2 |]);
  ]

let test_nested_comm_func () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 2 in
  S.set_commutative db f true;
  let f a b = T.func (f, [| a; b |]) in
  let g = Symb.add_func db 1 in
  let g a = T.func (g, [| a |]) in
  let p = Symb.add_pred db 1 in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.lit (Sh.Pos, p, [| (g (f c (T.var 0))) |]) ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i1 = Inst.create prob 1 in
  assert_log i1 [
    Solv.Enew_bool_var 0; (* p(0) *)
    Solv.Enew_bool_var_array ([| 0 |], 0);
    Solv.Enew_int_var (1, 0); (* g(0) *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_int_var (1, 1); (* f(0, 0) *)
    Solv.Enew_int_var_array ([| 1 |], 1);
    Solv.Enew_int_var (1, 2); (* c *)
    Solv.Enew_int_var_array ([| 2 |], 2);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (1, ~-1);
    Solv.Eint_element (1, 2, ~-1);
    (* g(f(c, 0)) *)
    Solv.Enew_tmp_int_var (1, ~-2);
    Solv.Eint_element (0, ~-1, ~-2);
    (* p(g(f(c, 0))) *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, ~-2, ~-1);
    Solv.Eclause ([| ~-1 |], [| |]);
  ];

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_bool_var 0; (* p(0) *)
    Solv.Enew_bool_var 1; (* p(1) *)
    Solv.Enew_bool_var_array ([| 0; 1 |], 0);
    Solv.Enew_int_var (2, 0); (* g(0) *)
    Solv.Enew_int_var (2, 1); (* g(1) *)
    Solv.Enew_int_var_array ([| 0; 1; |], 0);
    Solv.Enew_int_var (2, 2); (* f(0, 0) *)
    Solv.Enew_int_var (2, 3); (* f(0, 1) *)
    Solv.Enew_int_var (2, 4); (* f(1, 1) *)
    Solv.Enew_int_var_array ([| 2; 3; 3; 4 |], 1);
    Solv.Enew_int_var (2, 5); (* c *)
    Solv.Enew_int_var_array ([| 5 |], 2);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (3, ~-1);
    Solv.Elinear ([| 5; ~-1 |], [| 2; ~-1 |], 0);
    Solv.Enew_tmp_int_var (2, ~-2);
    Solv.Eint_element (1, ~-1, ~-2);
    (* g(f(c, 0)) *)
    Solv.Enew_tmp_int_var (2, ~-3);
    Solv.Eint_element (0, ~-2, ~-3);
    (* p(g(f(c, 0))) *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, ~-3, ~-1);
    Solv.Eclause ([| ~-1 |], [| |]);
    (* f(c, 1) *)
    Solv.Enew_tmp_int_var (4, ~-4);
    Solv.Elinear ([| 5; ~-4 |], [| 2; ~-1 |], ~-1);
    Solv.Enew_tmp_int_var (2, ~-5);
    Solv.Eint_element (1, ~-4, ~-5);
    (* g(f(c, 1)) *)
    Solv.Enew_tmp_int_var (2, ~-6);
    Solv.Eint_element (0, ~-5, ~-6);
    (* p(g(f(c, 1))) *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Ebool_element (0, ~-6, ~-2);
    Solv.Eclause ([| ~-2 |], [| |]);
  ];

  let i3 = Inst.create prob 3 in
  assert_log i3 [
    Solv.Enew_bool_var 0; (* p(0) *)
    Solv.Enew_bool_var 1; (* p(1) *)
    Solv.Enew_bool_var 2; (* p(2) *)
    Solv.Enew_bool_var_array ([| 0; 1; 2 |], 0);
    Solv.Enew_int_var (3, 0); (* g(0) *)
    Solv.Enew_int_var (3, 1); (* g(1) *)
    Solv.Enew_int_var (3, 2); (* g(2) *)
    Solv.Enew_int_var_array ([| 0; 1; 2 |], 0);
    Solv.Enew_int_var (3, 3); (* f(0, 0) *)
    Solv.Enew_int_var (3, 4); (* f(0, 1) *)
    Solv.Enew_int_var (3, 5); (* f(1, 1) *)
    Solv.Enew_int_var (3, 6); (* f(0, 2) *)
    Solv.Enew_int_var (3, 7); (* f(1, 2) *)
    Solv.Enew_int_var (3, 8); (* f(2, 2) *)
    Solv.Enew_int_var_array ([| 3; 4; 6; 4; 5; 7; 6; 7; 8 |], 1);
    Solv.Enew_int_var (3, 9); (* c *)
    Solv.Enew_int_var_array ([| 9 |], 2);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (7, ~-1);
    Solv.Elinear ([| 9; ~-1 |], [| 3; ~-1 |], 0);
    Solv.Enew_tmp_int_var (3, ~-2);
    Solv.Eint_element (1, ~-1, ~-2);
    (* g(f(c, 0)) *)
    Solv.Enew_tmp_int_var (3, ~-3);
    Solv.Eint_element (0, ~-2, ~-3);
    (* p(g(f(c, 0))) *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, ~-3, ~-1);
    Solv.Eclause ([| ~-1 |], [| |]);
    (* f(c, 1) *)
    Solv.Enew_tmp_int_var (8, ~-4);
    Solv.Elinear ([| 9; ~-4 |], [| 3; ~-1 |], ~-1);
    Solv.Enew_tmp_int_var (3, ~-5);
    Solv.Eint_element (1, ~-4, ~-5);
    (* g(f(c, 1)) *)
    Solv.Enew_tmp_int_var (3, ~-6);
    Solv.Eint_element (0, ~-5, ~-6);
    (* p(g(f(c, 1))) *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Ebool_element (0, ~-6, ~-2);
    Solv.Eclause ([| ~-2 |], [| |]);
    (* f(c, 2) *)
    Solv.Enew_tmp_int_var (9, ~-7);
    Solv.Elinear ([| 9; ~-7 |], [| 3; ~-1 |], ~-2);
    Solv.Enew_tmp_int_var (3, ~-8);
    Solv.Eint_element (1, ~-7, ~-8);
    (* g(f(c, 2)) *)
    Solv.Enew_tmp_int_var (3, ~-9);
    Solv.Eint_element (0, ~-8, ~-9);
    (* p(g(f(c, 2))) *)
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Ebool_element (0, ~-9, ~-3);
    Solv.Eclause ([| ~-3 |], [| |]);
  ]

let test_var_eqs_and_ineqs () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 2 in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.mk_eq (T.var 1) (T.var 2);
      L.mk_eq (T.func (f, [| T.var 0; T.var 2 |])) c;
      L.mk_ineq (T.var 1) (T.var 0);
    ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i3 = Inst.create prob 3 in
  assert_log i3 [
    Solv.Enew_int_var (3, 0); (* f(0, 0) *)
    Solv.Enew_int_var (3, 1); (* f(0, 1) *)
    Solv.Enew_int_var (3, 2); (* f(0, 2) *)
    Solv.Enew_int_var (3, 3); (* f(1, 0) *)
    Solv.Enew_int_var (3, 4); (* f(1, 1) *)
    Solv.Enew_int_var (3, 5); (* f(1, 2) *)
    Solv.Enew_int_var (3, 6); (* f(2, 0) *)
    Solv.Enew_int_var (3, 7); (* f(2, 1) *)
    Solv.Enew_int_var (3, 8); (* f(2, 2) *)
    Solv.Enew_int_var_array ([| 0; 1; 2; 3; 4; 5; 6; 7; 8 |], 0);
    Solv.Enew_int_var (3, 9); (* c *)
    Solv.Enew_int_var_array ([| 9 |], 1);
    (* x0 = x1 = 0, x2 = 1 *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_var (1, 9, ~-1);
    Solv.Eclause ([| ~-1 |], [| |]);
    (* x0 = x1 = 0, x2 = 2 *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_var (2, 9, ~-2);
    Solv.Eclause ([| ~-2 |], [| |]);
    (* x0 = x1 = 1, x2 = 0 *)
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_var (3, 9, ~-3);
    Solv.Eclause ([| ~-3 |], [| |]);
    (* x0 = x1 = 1, x2 = 2 *)
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_var (5, 9, ~-4);
    Solv.Eclause ([| ~-4 |], [| |]);
    (* x0 = x1 = 2, x2 = 0 *)
    Solv.Enew_tmp_bool_var ~-5;
    Solv.Eeq_var_var (6, 9, ~-5);
    Solv.Eclause ([| ~-5 |], [| |]);
    (* x0 = x1 = 2, x2 = 1 *)
    Solv.Enew_tmp_bool_var ~-6;
    Solv.Eeq_var_var (7, 9, ~-6);
    Solv.Eclause ([| ~-6 |], [| |]);
  ]

let test_shared_linear () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 2 in
  let f a b = T.func (f, [| a; b |]) in
  let g = Symb.add_func db 2 in
  let g a b = T.func (g, [| a; b |]) in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.mk_ineq (g c (T.var 0)) (f c (T.var 1)) ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i3 = Inst.create prob 3 in
  assert_log i3 [
    Solv.Enew_int_var (3, 0); (* g(0, 0) *)
    Solv.Enew_int_var (3, 1); (* g(0, 1) *)
    Solv.Enew_int_var (3, 2); (* g(0, 2) *)
    Solv.Enew_int_var (3, 3); (* g(1, 0) *)
    Solv.Enew_int_var (3, 4); (* g(1, 1) *)
    Solv.Enew_int_var (3, 5); (* g(1, 2) *)
    Solv.Enew_int_var (3, 6); (* g(2, 0) *)
    Solv.Enew_int_var (3, 7); (* g(2, 1) *)
    Solv.Enew_int_var (3, 8); (* g(2, 2) *)
    Solv.Enew_int_var_array ([| 0; 1; 2; 3; 4; 5; 6; 7; 8 |], 0);
    Solv.Enew_int_var (3, 9); (* c *)
    Solv.Enew_int_var_array ([| 9 |], 1);
    Solv.Enew_int_var (3, 10); (* f(0, 0) *)
    Solv.Enew_int_var (3, 11); (* f(0, 1) *)
    Solv.Enew_int_var (3, 12); (* f(0, 2) *)
    Solv.Enew_int_var (3, 13); (* f(1, 0) *)
    Solv.Enew_int_var (3, 14); (* f(1, 1) *)
    Solv.Enew_int_var (3, 15); (* f(1, 2) *)
    Solv.Enew_int_var (3, 16); (* f(2, 0) *)
    Solv.Enew_int_var (3, 17); (* f(2, 1) *)
    Solv.Enew_int_var (3, 18); (* f(2, 2) *)
    Solv.Enew_int_var_array ([| 10; 11; 12; 13; 14; 15; 16; 17; 18 |], 2);
    (* g(c, 0) *)
    Solv.Enew_tmp_int_var (7, ~-1);
    Solv.Elinear ([| 9; ~-1 |], [| 3; ~-1 |], 0);
    Solv.Enew_tmp_int_var (3, ~-2);
    Solv.Eint_element (0, ~-1, ~-2);
    (* f(c, 0) *)
    Solv.Enew_tmp_int_var (3, ~-3);
    Solv.Eint_element (2, ~-1, ~-3);
    (* g(c, 0) <> f(c, 0) *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_var (~-3, ~-2, ~-1);
    Solv.Eclause ([| |], [| ~-1 |]);
    (* g(c, 0) *)
    (* f(c, 1) *)
    Solv.Enew_tmp_int_var (8, ~-4);
    Solv.Elinear ([| 9; ~-4 |], [| 3; ~-1 |], ~-1);
    Solv.Enew_tmp_int_var (3, ~-5);
    Solv.Eint_element (2, ~-4, ~-5);
    (* g(c, 0) <> f(c, 1) *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_var (~-5, ~-2, ~-2);
    Solv.Eclause ([| |], [| ~-2 |]);
    (* g(c, 0) *)
    (* f(c, 2) *)
    Solv.Enew_tmp_int_var (9, ~-6);
    Solv.Elinear ([| 9; ~-6 |], [| 3; ~-1 |], ~-2);
    Solv.Enew_tmp_int_var (3, ~-7);
    Solv.Eint_element (2, ~-6, ~-7);
    (* g(c, 0) <> f(c, 2) *)
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_var (~-7, ~-2, ~-3);
    Solv.Eclause ([| |], [| ~-3 |]);
    (* g(c, 1) *)
    Solv.Enew_tmp_int_var (3, ~-8);
    Solv.Eint_element (0, ~-4, ~-8);
    (* f(c, 0) *)
    (* g(c, 1) <> f(c, 0) *)
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_var (~-8, ~-3, ~-4);
    Solv.Eclause ([| |], [| ~-4 |]);
    (* g(c, 1) *)
    (* f(c, 1) *)
    (* g(c, 1) <> f(c, 1) *)
    Solv.Enew_tmp_bool_var ~-5;
    Solv.Eeq_var_var (~-8, ~-5, ~-5);
    Solv.Eclause ([| |], [| ~-5 |]);
    (* g(c, 1) *)
    (* f(c, 2) *)
    (* g(c, 1) <> f(c, 2) *)
    Solv.Enew_tmp_bool_var ~-6;
    Solv.Eeq_var_var (~-8, ~-7, ~-6);
    Solv.Eclause ([| |], [| ~-6 |]);
    (* g(c, 2) *)
    Solv.Enew_tmp_int_var (3, ~-9);
    Solv.Eint_element (0, ~-6, ~-9);
    (* f(c, 0) *)
    (* g(c, 2) <> f(c, 0) *)
    Solv.Enew_tmp_bool_var ~-7;
    Solv.Eeq_var_var (~-9, ~-3, ~-7);
    Solv.Eclause ([| |], [| ~-7 |]);
    (* g(c, 2) *)
    (* f(c, 1) *)
    (* g(c, 2) <> f(c, 1) *)
    Solv.Enew_tmp_bool_var ~-8;
    Solv.Eeq_var_var (~-9, ~-5, ~-8);
    Solv.Eclause ([| |], [| ~-8 |]);
    (* g(c, 2) *)
    (* f(c, 2) *)
    (* g(c, 2) <> f(c, 2) *)
    Solv.Enew_tmp_bool_var ~-9;
    Solv.Eeq_var_var (~-9, ~-7, ~-9);
    Solv.Eclause ([| |], [| ~-9 |]);
  ]

let test_shared_bool_element () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let p = Symb.add_pred db 1 in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.lit (Sh.Pos, p, [| c |]);
      L.lit (Sh.Neg, p, [| c |]);
    ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_bool_var 0; (* p(0) *)
    Solv.Enew_bool_var 1; (* p(1) *)
    Solv.Enew_bool_var_array ([| 0; 1 |], 0);
    Solv.Enew_int_var (2, 0); (* c *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Ebool_element (0, 0, ~-1);
    Solv.Eclause ([| ~-1 |], [| ~-1 |]);
  ]

let test_shared_int_element () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let f = Symb.add_func db 1 in
  let f a = T.func (f, [| a |]) in
  let g = Symb.add_func db 1 in
  let g a = T.func (g, [| a |]) in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.mk_ineq (f c) (g (f c)) ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_int_var (2, 0); (* f(0) *)
    Solv.Enew_int_var (2, 1); (* f(1) *)
    Solv.Enew_int_var_array ([| 0; 1 |], 0);
    Solv.Enew_int_var (2, 2); (* c *)
    Solv.Enew_int_var_array ([| 2 |], 1);
    Solv.Enew_int_var (2, 3); (* g(0) *)
    Solv.Enew_int_var (2, 4); (* g(1) *)
    Solv.Enew_int_var_array ([| 3; 4 |], 2);
    (* f(c) *)
    Solv.Enew_tmp_int_var (2, ~-1);
    Solv.Eint_element (0, 2, ~-1);
    (* g(f(c)) *)
    Solv.Enew_tmp_int_var (2, ~-2);
    Solv.Eint_element (2, ~-1, ~-2);
    (* f(c) <> g(f(c)) *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_var (~-2, ~-1, ~-1);
    Solv.Eclause ([| |], [| ~-1 |]);
  ]

let test_shared_eq_var_var () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let f = Symb.add_func db 1 in
  let f a = T.func (f, [| a |]) in
  let d = Symb.add_func db 0 in
  let d = T.func (d, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.mk_eq c d; L.mk_eq (f (T.var 0)) d ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i4 = Inst.create prob 4 in
  assert_log i4 [
    Solv.Enew_int_var (4, 0); (* c *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_int_var (4, 1); (* d *)
    Solv.Enew_int_var_array ([| 1 |], 1);
    Solv.Enew_int_var (4, 2); (* f(0) *)
    Solv.Enew_int_var (4, 3); (* f(1) *)
    Solv.Enew_int_var (4, 4); (* f(2) *)
    Solv.Enew_int_var (4, 5); (* f(3) *)
    Solv.Enew_int_var_array ([| 2; 3; 4; 5 |], 2);
    (* c = d *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_var (0, 1, ~-1);
    (* f(0) = d *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_var (1, 2, ~-2);
    Solv.Eclause ([| ~-1; ~-2 |], [| |]);
    (* f(1) = d *)
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_var (1, 3, ~-3);
    Solv.Eclause ([| ~-1; ~-3 |], [| |]);
    (* f(2) = d *)
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_var (1, 4, ~-4);
    Solv.Eclause ([| ~-1; ~-4 |], [| |]);
    (* f(3) = d *)
    Solv.Enew_tmp_bool_var ~-5;
    Solv.Eeq_var_var (1, 5, ~-5);
    Solv.Eclause ([| ~-1; ~-5 |], [| |]);
  ]

let test_shared_eq_var_const () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [ L.mk_ineq (T.var 0) c; L.mk_ineq c (T.var 1) ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_int_var (2, 0); (* c *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    (* 0 <> c *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (0, 0, ~-1);
    (* c <> 0 *)
    (* clause *)
    Solv.Eclause ([| |], [| ~-1; ~-1 |]);
    (* 0 <> c *)
    (* c <> 1 *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_const (0, 1, ~-2);
    (* clause *)
    Solv.Eclause ([| |], [| ~-1; ~-2 |]);
    (* 1 <> c *)
    (* c <> 0 *)
    (* clause *)
    Solv.Eclause ([| |], [| ~-2; ~-1 |]);
    (* 1 <> c *)
    (* c <> 1 *)
    Solv.Eclause ([| |], [| ~-2; ~-2 |]);
  ]

let test_distinct_consts () =
  let Prob.Wr prob = Prob.create () in
  let db = prob.Prob.symbols in
  let _ = Symb.add_func db 0 in
  let c = Symb.add_func db 0 in
  let d1 = Symb.add_func db 0 in
  let d2 = Symb.add_func db 0 in
  let d3 = Symb.add_func db 0 in
  List.iter
    (BatDynArray.add prob.Prob.distinct_consts)
    [d1; d2; d3];
  let clause = {
    C2.cl_id = Prob.fresh_id prob;
    C2.cl_lits = [
      L.mk_ineq (T.var 0) (T.func (d1, [| |]));
      L.mk_eq (T.func (c, [| |])) (T.var 0);
    ];
  } in
  BatDynArray.add prob.Prob.clauses clause;

  let i2 = Inst.create prob 2 in
  assert_log i2 [
    Solv.Enew_int_var (2, 0); (* d1 *)
    Solv.Enew_int_var_array ([| 0 |], 0);
    Solv.Enew_int_var (2, 1); (* d2 *)
    Solv.Enew_int_var_array ([| 1 |], 1);
    Solv.Enew_int_var (2, 2); (* d3 *)
    Solv.Enew_int_var_array ([| 2 |], 2);
    Solv.Eall_different [| 0; 1; 2 |];
    Solv.Enew_int_var (2, 3);
    Solv.Enew_int_var_array ([| 3 |], 3);
    (* c = 0 *)
    Solv.Enew_tmp_bool_var ~-1;
    Solv.Eeq_var_const (3, 0, ~-1);
    (* 0 <> d1 *)
    Solv.Enew_tmp_bool_var ~-2;
    Solv.Eeq_var_const (0, 0, ~-2);
    (* clause *)
    Solv.Eclause ([| ~-1 |], [| ~-2 |]);
    (* c = 1 *)
    Solv.Enew_tmp_bool_var ~-3;
    Solv.Eeq_var_const (3, 1, ~-3);
    (* 1 <> d1 *)
    Solv.Enew_tmp_bool_var ~-4;
    Solv.Eeq_var_const (0, 1, ~-4);
    (* clause *)
    Solv.Eclause ([| ~-3 |], [| ~-4 |]);
  ]

let suite =
  "Csp_inst suite" >:::
    [
      "empty" >:: test_empty;
      "flat pred" >:: test_flat_pred;
      "flat func" >:: test_flat_func;
      "flat comm func" >:: test_flat_comm_func;
      "nested" >:: test_nested;
      "nested comm func" >:: test_nested_comm_func;
      "variable (in)equalities" >:: test_var_eqs_and_ineqs;
      "shared linear" >:: test_shared_linear;
      "shared bool_element" >:: test_shared_bool_element;
      "shared int_element" >:: test_shared_int_element;
      "shared eq_var_var" >:: test_shared_eq_var_var;
      "shared eq_var_const" >:: test_shared_eq_var_const;
      "distinct consts" >:: test_distinct_consts;
    ]
