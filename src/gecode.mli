(* Copyright (c) 2013 Radek Micek *)

(** Gecode CSP solver.

   See {!Csp_solver.S} for documentation.
*)

type t
type 'a var = private int
type 'a var_array = private int

external create : int -> t = "gecode_create"

external destroy : t -> unit = "gecode_destroy"

external new_bool_var : t -> bool var = "gecode_new_bool_var"

external new_int_var : t -> int -> int var = "gecode_new_int_var"

external new_tmp_bool_var : t -> bool var = "gecode_new_tmp_bool_var"

external new_tmp_int_var : t -> int -> int var = "gecode_new_tmp_int_var"

external new_bool_var_array : t -> (bool var, [> `R]) Earray.t ->
  bool var_array = "gecode_new_bool_var_array"

external new_int_var_array : t -> (int var, [> `R]) Earray.t ->
  int var_array = "gecode_new_int_var_array"

external linear : t -> (int var, [> `R]) Earray.t -> (int, [> `R]) Earray.t ->
  int -> unit = "gecode_linear"

external bool_element : t -> bool var_array -> int var -> bool var -> unit =
    "gecode_bool_element"

external int_element : t -> int var_array -> int var -> int var -> unit =
    "gecode_int_element"

external eq_var_var : t -> int var -> int var -> bool var -> unit =
    "gecode_eq_var_var"

external eq_var_const : t -> int var -> int -> bool var -> unit =
    "gecode_eq_var_const"

external lower_eq : t -> int var -> int -> unit =
    "gecode_lower_eq"

external precede : t -> (int var, [> `R]) Earray.t ->
  (int, [> `R]) Earray.t -> unit = "gecode_precede"

external clause : t -> (bool var, [> `R]) Earray.t ->
  (bool var, [> `R]) Earray.t -> unit = "gecode_clause"

external all_different : t -> (int var, [> `R]) Earray.t -> unit =
    "gecode_all_different"

external solve : t -> Sh.lbool = "gecode_solve"

external interrupt : t -> unit = "gecode_interrupt"

external bool_value : t -> bool var -> int = "gecode_bool_value"

external int_value : t -> int var -> int = "gecode_int_value"
