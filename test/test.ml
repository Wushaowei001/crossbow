(* Copyright (c) 2013 Radek Micek *)

open OUnit

let suite =
  TestList
    [
      Test_earray.suite;
    ]

let () = ignore (run_test_tt suite)
