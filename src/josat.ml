(* Copyright (c) 2013 Radek Micek *)

type t

type var = int

type lit = int

external create : unit -> t = "josat_create"

external new_var : t -> var = "josat_new_var"

external add_clause : t -> (lit, [> `R]) Earray.t -> int -> bool =
  "josat_add_clause"

external solve : t -> (lit, [> `R]) Earray.t -> Sh.lbool = "josat_solve"

external model_value : t -> var -> Sh.lbool = "josat_model_value"

external interrupt : t -> unit = "josat_interrupt"

external clear_interrupt : t -> unit = "josat_clear_interrupt"

let to_lit sign v = match sign with
  | Sh.Pos -> v + v
  | Sh.Neg -> v + v + 1

let to_var lit = lit / 2
