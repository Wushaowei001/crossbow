(* Copyright (c) 2013, 2015 Radek Micek *)

open OUnit

module S = Symb
module T = Term
module L = Lit
module C2 = Clause2

module Make (Inst : Csp_inst.Inst_sig) : sig
  val suite : string -> test
end = struct

  module Array = Earray.Array

  (* injective: f(x) = f(y) -> x = y
     not surjective: f(x) <> c
  *)
  let test_only_infinite_model () =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let f =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let c = T.func (Symb.add_func db 0, [| |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let clause = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x) <> f(y), x = y *)
      C2.cl_lits = [ L.mk_ineq (f x) (f y); L.mk_eq x y ];
    } in
    let clause2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x) <> c *)
      C2.cl_lits = [ L.mk_ineq (f x) c ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    for max_size = 1 to 15 do
      let i = Inst.create prob sorts max_size in
      assert_equal Sh.Lfalse (Inst.solve i)
    done

  (* couples:
     f(x) <> x
     f(x) = y -> f(y) = x
  *)
  let test_fin_models_even_size () =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let fsymb = Symb.add_func db 1 in
    let f a = T.func (fsymb, [| a |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let clause = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x) <> x *)
      C2.cl_lits = [ L.mk_ineq (f x) x ];
    } in
    let clause2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x) <> y, f(y) = x *)
      C2.cl_lits = [ L.mk_ineq (f x) y; L.mk_eq (f y) x ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    for max_size = 1 to 15 do
      let i = Inst.create prob sorts max_size in
      if max_size mod 2 = 0 then begin
        assert_equal Sh.Ltrue (Inst.solve i);

        (* Check model. *)
        let model = Inst.construct_model i in
        assert_equal max_size model.Ms_model.max_size;
        let ftable = Symb.Map.find fsymb model.Ms_model.symbs in
        let fvalues = ftable.Ms_model.values in
        for x = 0 to max_size - 1 do
          assert_bool "range"
            (fvalues.(x) >= 0 && fvalues.(x) <= max_size - 1);
          assert_bool "" (fvalues.(x) <> x);
          assert_equal x fvalues.(fvalues.(x))
        done

      end else
        assert_equal Sh.Lfalse (Inst.solve i)
    done

  let make_group () : [> `W] Prob.t * S.id * S.id * S.id =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let zero' = Symb.add_func db 0 in
    let zero = T.func (zero', [| |]) in
    let g' = Symb.add_func db 1 in
    let g a = T.func (g', [| a |]) in
    let f' = Symb.add_func db 2 in
    let f a b = T.func (f', [| a; b |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let z = T.var 2 in
    let clause = {
      C2.cl_id = Prob.fresh_id prob;
      (* g(0) = 0 *)
      C2.cl_lits = [ L.mk_eq (g zero) zero ];
    } in
    let clause2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* g(g(x)) = x *)
      C2.cl_lits = [ L.mk_eq (g (g x)) x ];
    } in
    let clause3 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x, 0) = x *)
      C2.cl_lits = [ L.mk_eq (f x zero) x ];
    } in
    let clause4 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(0, x) = x *)
      C2.cl_lits = [ L.mk_eq (f zero x) x ];
    } in
    let clause5 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x, g(x)) = 0 *)
      C2.cl_lits = [ L.mk_eq (f x (g x)) zero ];
    } in
    let clause6 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(g(x), x) = 0 *)
      C2.cl_lits = [ L.mk_eq (f (g x) x) zero ];
    } in
    let clause7 =
      {
        C2.cl_id = Prob.fresh_id prob;
        (* f(f(x, y), z) = f(x, f(y, z)) *)
        C2.cl_lits = [ L.mk_eq (f (f x y) z) (f x (f y z)) ];
      } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2; clause3; clause4; clause5; clause6; clause7];
    (prob, zero', g', f')

  let count_models prob max_size =
    let sorts = Sorts.of_problem prob in
    let i = Inst.create prob sorts max_size in
    let found = ref true in
    let models = ref (BatSet.PSet.create Model.compare) in
    while !found do
      if Inst.solve i = Sh.Ltrue then begin
        let ms_model = Inst.construct_model i in
        let model = Model.of_ms_model ms_model sorts in
        assert_equal max_size model.Model.max_size;
        let cano_model = Model.canonize model in
        if not (BatSet.PSet.mem cano_model !models) then begin
          models := BatSet.PSet.add cano_model !models;
        end
      end else
        found := false
    done;
    BatSet.PSet.cardinal !models

  let test_abelian_groups () =
    let (prob, _, _, f) = make_group () in
    Symb.set_commutative prob.Prob.symbols f true;

    let exp_counts = (* up to max_size = 16 *)
      [| -1; 1; 1; 1; 2; 1; 1; 1; 3; 2; 1; 1; 2; 1; 1; 1; 5 |] in
    for max_size = 1 to 9 do
      let model_cnt = count_models prob max_size in
      assert_equal exp_counts.(max_size) model_cnt
    done

  let test_abelian_groups2 () =
    let (prob, _, _, f) = make_group () in
    BatDynArray.add
      prob.Prob.clauses
      {
        C2.cl_id = Prob.fresh_id prob;
        (* f(x, y) = f(y, x) *)
        C2.cl_lits = [
          L.mk_eq
            (T.func (f, [| T.var 0; T.var 1 |]))
            (T.func (f, [| T.var 1; T.var 0 |]));
        ];
      };

    let exp_counts = (* up to max_size = 16 *)
      [| -1; 1; 1; 1; 2; 1; 1; 1; 3; 2; 1; 1; 2; 1; 1; 1; 5 |] in
    for max_size = 1 to 9 do
      let model_cnt = count_models prob max_size in
      assert_equal exp_counts.(max_size) model_cnt
    done

  let test_groups () =
    let (prob, _, _, _) = make_group () in

    let exp_counts = (* up to max_size = 16 *)
      [| -1; 1; 1; 1; 2; 1; 2; 1; 5; 2; 2; 1; 5; 1; 2; 1; 14 |] in
    for max_size = 1 to 9 do
      let model_cnt = count_models prob max_size in
      assert_equal exp_counts.(max_size) model_cnt
    done

  (* Two sorts. Elements of the first sort are in pairs (cycles of length 2):

       f(x) <> x
       f(f(x)) = x

     Elements of the second sort are in triples (cycles of length 3):

       g(x) <> x
       g(g(x)) <> x
       g(g(g(x))) = x

     This implies that every model has size which is multiple of 6.
  *)
  let test_two_sorts () =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let f =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let g =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let x = T.var 0 in
    let clause_f1 = {
      C2.cl_id = Prob.fresh_id prob;
      C2.cl_lits = [ L.mk_ineq (f x) x ];
    } in
    let clause_f2 = {
      C2.cl_id = Prob.fresh_id prob;
      C2.cl_lits = [ L.mk_eq (f (f x)) x ];
    } in
    let clause_g1 = {
      C2.cl_id = Prob.fresh_id prob;
      C2.cl_lits = [ L.mk_ineq (g x) x ];
    } in
    let clause_g2 = {
      C2.cl_id = Prob.fresh_id prob;
      C2.cl_lits = [ L.mk_ineq (g (g x)) x ];
    } in
    let clause_g3 = {
      C2.cl_id = Prob.fresh_id prob;
      C2.cl_lits = [ L.mk_eq (g (g (g x))) x ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause_f1; clause_f2; clause_g1; clause_g2; clause_g3];
    let sorts = Sorts.of_problem prob in

    for max_size = 1 to 40 do
      let i = Inst.create prob sorts max_size in
      if max_size mod 6 = 0 then begin
        assert_equal Sh.Ltrue (Inst.solve i);
        let ms_model = Inst.construct_model i in
        assert_equal max_size ms_model.Ms_model.max_size;
        let model = Model.of_ms_model ms_model sorts in
        assert_equal max_size model.Model.max_size
      end else begin
        assert_equal Sh.Lfalse (Inst.solve i)
      end
    done

  (* Same as [test_two_sorts] but clauses are flat. *)
  let test_two_sorts_flat () =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let f =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let g =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let z = T.var 2 in
    let clause_f1 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x) <> x *)
      C2.cl_lits = [ L.mk_ineq (f x) x ];
    } in
    let clause_f2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(f(x)) = x which is equivalent to
         f(x) = y -> f(y) = x
      *)
      C2.cl_lits = [ L.mk_ineq (f x) y; L.mk_eq (f y) x ];
    } in
    let clause_g1 = {
      C2.cl_id = Prob.fresh_id prob;
      (* g(x) <> x *)
      C2.cl_lits = [ L.mk_ineq (g x) x ];
    } in
    let clause_g2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* g(g(x)) <> x which is equivalent to
         g(x) = y -> g(y) <> x
      *)
      C2.cl_lits = [ L.mk_ineq (g x) y; L.mk_ineq (g y) x ];
    } in
    let clause_g3 = {
      C2.cl_id = Prob.fresh_id prob;
      (* g(g(g(x))) = x which is equivalent to
         g(x) = y -> g(y) = z -> g(z) = x
      *)
      C2.cl_lits = [ L.mk_ineq (g x) y; L.mk_ineq (g y) z; L.mk_eq (g z) x ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause_f1; clause_f2; clause_g1; clause_g2; clause_g3];
    let sorts = Sorts.of_problem prob in

    for max_size = 1 to 40 do
      let i = Inst.create prob sorts max_size in
      if max_size mod 6 = 0 then begin
        assert_equal Sh.Ltrue (Inst.solve i);
        let ms_model = Inst.construct_model i in
        assert_equal max_size ms_model.Ms_model.max_size;
        let model = Model.of_ms_model ms_model sorts in
        assert_equal max_size model.Model.max_size
      end else begin
        assert_equal Sh.Lfalse (Inst.solve i)
      end
    done

  (* f(x, x) = f(y, y) -> x = y
     f(x, x) <> c
  *)
  let test_solve_timed () =
    let prob = Prob.create () in
    let db = prob.Prob.symbols in
    let f =
      let s = Symb.add_func db 2 in
      fun a b -> T.func (s, [| a; b |]) in
    let c = T.func (Symb.add_func db 0, [| |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let clause = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x, x) <> f(y, y), x = y *)
      C2.cl_lits = [ L.mk_ineq (f x x) (f y y); L.mk_eq x y ];
    } in
    let clause2 = {
      C2.cl_id = Prob.fresh_id prob;
      (* f(x, x) <> c *)
      C2.cl_lits = [ L.mk_ineq (f x x) c ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts 40 in (* 40 means large search space. *)
    let max_ms = 900 in
    assert_equal (Sh.Lundef, true) (Inst.solve_timed i max_ms)

  let suite name =
    (name ^ " suite") >:::
      [
        "only infinite model" >:: test_only_infinite_model;
        "finite models of even size" >:: test_fin_models_even_size;
        "abelian groups" >:: test_abelian_groups;
        "abelian groups 2" >:: test_abelian_groups2;
        "groups" >:: test_groups;
        "two sorts" >:: test_two_sorts;
        "two sorts flat" >:: test_two_sorts_flat;
        "solve_timed" >:: test_solve_timed;
      ]

end
