(* Copyright (c) 2013 Radek Micek *)

open OUnit

module S = Symb
module T = Term

let test_is_var_is_const_is_proper_func () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 1 in
  let c = Symb.add_func db 0 in
  let t1 = T.var 2 in
  let t2 = T.func (c, [| |]) in
  let t3 = T.func (f, [| T.var 1 |]) in
  assert_equal true (T.is_var t1);
  assert_equal false (T.is_var t2);
  assert_equal false (T.is_var t3);
  assert_equal false (T.is_const t1);
  assert_equal true (T.is_const t2);
  assert_equal false (T.is_const t3);
  assert_equal false (T.is_proper_func t1);
  assert_equal false (T.is_proper_func t2);
  assert_equal true (T.is_proper_func t3)

let test_contains1 () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let g = Symb.add_func db 2 in
  let i = Symb.add_func db 1 in
  let subterm = T.func (f, [| T.var 0; T.func (i, [| T.var 2 |]) |]) in
  let term =
    T.func (g,
            [|
              T.func (f, [| T.var 0; T.func (i, [| T.var 1 |]) |]);
              T.func (f, [| T.func (i, [| T.var 2 |]); T.var 0 |]);
            |]
    ) in
  assert_bool "" (not (T.contains subterm term))

let test_contains2 () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 1 in
  let g = Symb.add_func db 2 in
  let subterm = T.func (f, [| T.var 1 |]) in
  let term =
    T.func (g,
            [|
              T.func (f, [| T.func (f, [| T.var 1 |]) |]);
              T.var 1;
            |]
    ) in
  assert_bool "" (T.contains subterm term)

let test_is_ground () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 1 in
  let c = Symb.add_func db 0 in
  let term =
    T.func (f,
            [|
              T.func (f, [| T.func (c, [| |]) |]);
            |]
    ) in
  assert_bool "" (T.is_ground term)

let test_is_ground2 () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let c = Symb.add_func db 0 in
  let term =
    T.func (f,
            [|
              T.func (f, [| T.func (c, [| |]); T.var 2 |]);
              T.func (c, [| |]);
            |]
    ) in
  assert_bool "" (not (T.is_ground term))

let test_iter () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let g = Symb.add_func db 1 in
  let x = T.var 0 in
  let y = T.var 1 in
  let z = T.var 2 in
  let t1 = T.func (f, [| x; z |]) in
  let t2 = T.func (g, [| t1 |]) in
  (* f(g(f(x, z)), y) *)
  let term = T.func (f, [| t2; y |]) in
  let subterms = ref [term; t2; t1; x; z; y] in
  let each_subterm t =
    assert_equal (List.hd !subterms) t;
    subterms := List.tl !subterms in
  T.iter each_subterm term;
  assert_equal [] !subterms

let test_count_symbs () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let g = Symb.add_func db 1 in
  let x = T.var 0 in
  let y = T.var 1 in
  let t1 = T.func (f, [| x; y |]) in
  let t2 = T.func (g, [| T.func (g, [| y |]) |]) in
  let t3 = T.func (f, [| t1; t2 |]) in
  let t4 = x in
  assert_equal 1 (T.count_symbs t1);
  assert_equal 2 (T.count_symbs t2);
  assert_equal 4 (T.count_symbs t3);
  assert_equal 0 (T.count_symbs t4)

let test_normalize_comm () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let g = Symb.add_func db 2 in
  let h = Symb.add_func db 2 in
  Symb.set_commutative db f true;
  Symb.set_commutative db h true;
  let orig =
    T.func (f,
            [|
              T.func (h,
                      [|
                        T.func (g, [| T.var 3; T.var 4 |]);
                        T.func (g, [| T.var 4; T.var 3 |]);
                      |]
              );
              T.func (h, [| T.var 3; T.var 1 |]);
            |]
    ) in
  let normalized =
    T.func (f,
            [|
              T.func (h, [| T.var 1; T.var 3 |]);
              T.func (h,
                      [|
                        T.func (g, [| T.var 3; T.var 4 |]);
                        T.func (g, [| T.var 4; T.var 3 |]);
                      |]
              );
            |]
    ) in
  assert_equal normalized (T.normalize_comm db orig)


module IntSet = Sh.IntSet

let test_vars () =
  let db = Symb.create_db () in
  let f = Symb.add_func db 2 in
  let f a b = T.func (f, [| a; b |]) in
  let c = Symb.add_func db 0 in
  let c = T.func (c, [| |]) in
  let x = T.var 1 in
  let y = T.var 5 in
  let z = T.var 2 in
  let term = f (f x (f c y)) (f (f x z) c) in
  let exp_vars = List.fold_right IntSet.add [1; 5; 2] IntSet.empty in
  assert_equal ~cmp:IntSet.equal exp_vars (T.vars term)

let suite =
  "Term suite" >:::
    [
      "is_var, is_const, is_proper_func" >::
        test_is_var_is_const_is_proper_func;
      "contains 1" >:: test_contains1;
      "contains 2" >:: test_contains2;
      "is_ground" >:: test_is_ground;
      "is_ground 2" >:: test_is_ground2;
      "iter" >:: test_iter;
      "count_symbs" >:: test_count_symbs;
      "normalize_comm" >:: test_normalize_comm;
      "vars" >:: test_vars;
    ]
