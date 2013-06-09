(* Copyright (c) 2013 Radek Micek *)

open OUnit

module T = Term
module L = Lit
module C = Clause2

module Make (Inst : Sat_inst.Inst_sig) : sig
  val suite : string -> test
end = struct

  (* injective: f(x) = f(y) -> x = y
     not surjective: f(x) <> c
  *)
  let test_only_infinite_model () =
    let Prob.Wr prob = Prob.create () in
    let db = prob.Prob.symbols in
    let f =
      let s = Symb.add_func db 1 in
      fun a -> T.func (s, [| a |]) in
    let c = T.func (Symb.add_func db 0, [| |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let z = T.var 2 in
    let clause = {
      C.cl_id = Prob.fresh_id prob;
      (* f(x) <> z, z <> f(y), x = y *)
      C.cl_lits = [ L.mk_ineq (f x) z; L.mk_ineq z (f y); L.mk_eq x y ];
    } in
    let clause2 = {
      C.cl_id = Prob.fresh_id prob;
      (* f(x) <> y, y <> c *)
      C.cl_lits = [ L.mk_ineq (f x) y; L.mk_ineq y c ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts in
    for max_size = 1 to 15 do
      Inst.incr_max_size i;
      assert_equal Sat_solver.Lfalse (Inst.solve i)
    done

  let test_only_nullary_preds () =
    let Prob.Wr prob = Prob.create () in
    let db = prob.Prob.symbols in
    let psymb = Symb.add_pred db 0 in
    let qsymb = Symb.add_pred db 0 in
    let rsymb = Symb.add_pred db 0 in
    let p = L.lit (Sh.Pos, psymb, [| |]) in
    let q = L.lit (Sh.Pos, qsymb, [| |]) in
    let r = L.lit (Sh.Pos, rsymb, [| |]) in
    (* Only satisfying assignment is: ~p, q, ~r. *)
    let clause = {
      C.cl_id = Prob.fresh_id prob;
      (* ~p, ~q, ~r *)
      C.cl_lits = [ L.neg p; L.neg q; L.neg r ];
    } in
    let clause2 = {
      C.cl_id = Prob.fresh_id prob;
      (* ~p, ~q, r *)
      C.cl_lits = [ L.neg p; L.neg q; r ];
    } in
    let clause3 = {
      C.cl_id = Prob.fresh_id prob;
      (* ~p, q, ~r *)
      C.cl_lits = [ L.neg p; q; L.neg r ];
    } in
    let clause4 = {
      C.cl_id = Prob.fresh_id prob;
      (* ~p, q, r *)
      C.cl_lits = [ L.neg p; q; r ];
    } in
    let clause5 = {
      C.cl_id = Prob.fresh_id prob;
      (* p, ~q, ~r *)
      C.cl_lits = [ p; L.neg q; L.neg r ];
    } in
    let clause6 = {
      C.cl_id = Prob.fresh_id prob;
      (* p, q, ~r *)
      C.cl_lits = [ p; q; L.neg r ];
    } in
    let clause7 = {
      C.cl_id = Prob.fresh_id prob;
      (* p, q, r *)
      C.cl_lits = [ p; q; r ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2; clause3; clause4; clause5; clause6; clause7];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts in
    for max_size = 1 to 30 do
      Inst.incr_max_size i;
      assert_equal Sat_solver.Ltrue (Inst.solve i);
      (* Check model. *)
      let ms_model = Inst.construct_model i in
      assert_equal max_size ms_model.Ms_model.max_size;
      assert_equal
        {
          Ms_model.param_sizes = [| |];
          Ms_model.values = [| 0 |];
        }
        (Symb.Map.find psymb ms_model.Ms_model.symbs);
      assert_equal
        {
          Ms_model.param_sizes = [| |];
          Ms_model.values = [| 1 |];
        }
        (Symb.Map.find qsymb ms_model.Ms_model.symbs);
      assert_equal
        {
          Ms_model.param_sizes = [| |];
          Ms_model.values = [| 0 |];
        }
        (Symb.Map.find rsymb ms_model.Ms_model.symbs)
    done

  let test_symmetric_pred () =
    let Prob.Wr prob = Prob.create () in
    let db = prob.Prob.symbols in
    let psymb = Symb.add_pred db 2 in
    let p a b = L.lit (Sh.Pos, psymb, [| a; b |]) in
    Symb.set_commutative db psymb true;
    let c1symb = Symb.add_func db 0 in
    let c1 = T.func (c1symb, [| |]) in
    let c2symb = Symb.add_func db 0 in
    let c2 = T.func (c2symb, [| |]) in
    let c3symb = Symb.add_func db 0 in
    let c3 = T.func (c3symb, [| |]) in
    List.iter
      (BatDynArray.add prob.Prob.distinct_consts)
      [c1symb; c2symb; c3symb];
    let nconsts = 3 in
    let x = T.var 0 in
    let y = T.var 1 in
    let clause = {
      C.cl_id = Prob.fresh_id prob;
      (* ~p(c1, c2) *)
      C.cl_lits = [ L.neg (p x y); L.mk_ineq c1 x; L.mk_ineq c2 y ];
    } in
    let clause2 = {
      C.cl_id = Prob.fresh_id prob;
      (* p(c1, c3) *)
      C.cl_lits = [ p x y; L.mk_ineq c1 x; L.mk_ineq c3 y ];
    } in
    let clause3 = {
      C.cl_id = Prob.fresh_id prob;
      (* p(c2, c3) *)
      C.cl_lits = [ p x y; L.mk_ineq c2 x; L.mk_ineq c3 y ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2; clause3];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts in
    for max_size = 1 to nconsts - 1 do
      Inst.incr_max_size i
    done;
    for max_size = nconsts to 15 do
      Inst.incr_max_size i;
      assert_equal Sat_solver.Ltrue (Inst.solve i);
      (* Check model. *)
      let ms_model = Inst.construct_model i in
      assert_equal max_size ms_model.Ms_model.max_size;
      let ptable = Symb.Map.find psymb ms_model.Ms_model.symbs in
      assert_equal [| nconsts; nconsts |] ptable.Ms_model.param_sizes;
      let get_val x y = ptable.Ms_model.values.(x * nconsts + y) in
      (* Check symmetry of the predicate. *)
      for x = 0 to nconsts - 1 do
        for y = x to nconsts - 1 do
          assert_bool "range" (get_val x y = 0 || get_val x y = 1);
          assert_bool "symmetry" (get_val x y = get_val y x)
        done
      done;
      (* Check constants. *)
      let c1table = Symb.Map.find c1symb ms_model.Ms_model.symbs in
      assert_equal [| |] c1table.Ms_model.param_sizes;
      let c2table = Symb.Map.find c2symb ms_model.Ms_model.symbs in
      assert_equal [| |] c2table.Ms_model.param_sizes;
      let c3table = Symb.Map.find c3symb ms_model.Ms_model.symbs in
      assert_equal [| |] c3table.Ms_model.param_sizes;
      let const_values = [
        c1table.Ms_model.values.(0);
        c2table.Ms_model.values.(0);
        c3table.Ms_model.values.(0)
      ] in
      assert_equal nconsts (List.length (BatList.unique const_values));
      List.iter
        (fun c -> assert_bool "range" (c >= 0 && c < nconsts))
        const_values
    done

  let test_latin_square () =
    let Prob.Wr prob = Prob.create () in
    let db = prob.Prob.symbols in
    let fsymb = Symb.add_func db 2 in
    let f a b = T.func (fsymb, [| a; b |]) in
    let x = T.var 0 in
    let row1 = T.var 1 in
    let row2 = T.var 2 in
    let col = T.var 3 in
    let clause = {
      C.cl_id = Prob.fresh_id prob;
      (* row1 = row2, f(row1, col) != x, x != f(row2, col) *)
      C.cl_lits = [
        L.mk_eq row1 row2;
        L.mk_ineq (f row1 col) x;
        L.mk_ineq x (f row2 col) ];
    } in
    let row = T.var 1 in
    let col1 = T.var 2 in
    let col2 = T.var 3 in
    let clause2 = {
      C.cl_id = Prob.fresh_id prob;
      (* col1 = col2, f(row, col1) != x, x != f(row, col2) *)
      C.cl_lits = [
        L.mk_eq col1 col2;
        L.mk_ineq (f row col1) x;
        L.mk_ineq x (f row col2) ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts in
    for max_size = 1 to 15 do
      Inst.incr_max_size i;
      assert_equal Sat_solver.Ltrue (Inst.solve i);
      (* Check model. *)
      let ms_model = Inst.construct_model i in
      assert_equal max_size ms_model.Ms_model.max_size;
      let ftable = Symb.Map.find fsymb ms_model.Ms_model.symbs in
      assert_equal [| max_size; max_size |] ftable.Ms_model.param_sizes;
      let get_val row col = ftable.Ms_model.values.(row * max_size + col) in
      (* Check range. *)
      for row = 0 to max_size - 1 do
        for col = 0 to max_size - 1 do
          let v = get_val row col in
          assert_bool "range" (v >= 0 && v <= max_size - 1)
        done
      done;
      (* Check all different constraints. *)
      for z1 = 0 to max_size - 2 do
        for z2 = z1 + 1 to max_size - 1 do
          (* Columns z1 and z2 are different in each row. *)
          for row = 0 to max_size - 1 do
            assert_bool "row" (get_val row z1 <> get_val row z2)
          done;
          (* Rows z1 and z2 are different in each column. *)
          for col = 0 to max_size - 1 do
            assert_bool "column" (get_val z1 col <> get_val z2 col)
          done
        done
      done
    done

  (* couples:
     f(x) <> x
     f(x) = y -> f(y) = x
  *)
  let test_fin_models_even_size () =
    let Prob.Wr prob = Prob.create () in
    let db = prob.Prob.symbols in
    let fsymb = Symb.add_func db 1 in
    let f a = T.func (fsymb, [| a |]) in
    let x = T.var 0 in
    let y = T.var 1 in
    let clause = {
      C.cl_id = Prob.fresh_id prob;
      (* f(x) <> x *)
      C.cl_lits = [ L.mk_ineq (f x) x ];
    } in
    let clause2 = {
      C.cl_id = Prob.fresh_id prob;
      (* f(x) <> y, f(y) = x *)
      C.cl_lits = [ L.mk_ineq (f x) y; L.mk_eq (f y) x ];
    } in
    List.iter
      (BatDynArray.add prob.Prob.clauses)
      [clause; clause2];
    let sorts = Sorts.of_problem prob in

    let i = Inst.create prob sorts in
    for max_size = 1 to 15 do
      Inst.incr_max_size i;
      if max_size mod 2 = 0 then begin
        assert_equal Sat_solver.Ltrue (Inst.solve i);
        (* Check model. *)
        let ms_model = Inst.construct_model i in
        assert_equal max_size ms_model.Ms_model.max_size;
        let ftable = Symb.Map.find fsymb ms_model.Ms_model.symbs in
        assert_equal [| max_size |] ftable.Ms_model.param_sizes;
        let fvalues = ftable.Ms_model.values in
        for x = 0 to max_size - 1 do
          assert_bool "range"
            (fvalues.(x) >= 0 && fvalues.(x) <= max_size - 1);
          assert_bool "" (fvalues.(x) <> x);
          assert_equal x fvalues.(fvalues.(x))
        done
      end else
        assert_equal Sat_solver.Lfalse (Inst.solve i)
    done

  let suite name =
    (name ^ " suite") >:::
      [
        "only infinite model" >:: test_only_infinite_model;
        "only nullary preds" >:: test_only_nullary_preds;
        "symmetric predicate" >:: test_symmetric_pred;
        "latin square" >:: test_latin_square;
        "finite models of even size" >:: test_fin_models_even_size;
      ]

end
