(* Copyright (c) 2013 Radek Micek *)

module type Solver = sig
  type t

  type lbool =
    | Ltrue
    | Lfalse
    | Lundef

  type var = int

  type lit = private int

  type sign =
    | Pos
    | Neg

  val create : unit -> t

  val new_var : t -> var

  val new_false_var : t -> var

  val add_clause : t -> lit array -> int -> bool

  val add_symmetry_clause : t -> lit array -> int -> bool

  val add_at_least_one_val_clause : t -> lit array -> int -> bool

  val add_at_most_one_val_clause : t -> lit array -> bool

  val remove_clauses_with_lit : t -> lit -> unit

  val solve : t -> lit array -> lbool

  val model_value : t -> var -> lbool

  val to_lit : sign -> var -> lit

  val to_var : lit -> var
end

module type Inst_sig = sig
  type lbool
  type solver

  type t

  val create : Prob.t -> Sorts.t -> t

  val incr_max_size : t -> unit

  val solve : t -> lbool

  val construct_model : t -> Ms_model.t

  val get_solver : t -> solver

  val get_max_size : t -> int
end

module Make (Solv : Solver) :
  Inst_sig with type lbool = Solv.lbool
           and type solver = Solv.t =
struct
  type pvar = Solv.var
  type plit = Solv.lit

  type lbool = Solv.lbool
  type solver = Solv.t

  type lit = {
    l_sign : Solv.sign;

    (* Propositional variables associated with the symbol. *)
    l_pvars : pvar BatDynArray.t;

    (* Variables used as the arguments and the result (if appropriate). *)
    l_vars : Term.var array;

    (* For ranking. *)
    l_commutative : bool;

    (* For ranking. *)
    l_adeq_sizes : int array;
  }

  type clause = {
    (* Adequate sizes of the domains of the variables in the clause. *)
    var_adeq_sizes : int array;

    (* Equalities of variables. *)
    var_equalities : (Term.var * Term.var) array;

    (* Literals for nullary predicates. *)
    nullary_pred_lits : plit array;

    (* Literals with variables. *)
    lits : lit array;
  }

  type commutative = bool

  type t = {
    symred : Symred.t;
    solver : Solv.t;
    symbols : Symb.db;

    (* Propositional variables corresponding to nullary predicates. *)
    nullary_pred_pvars : (Symb.id, pvar) Hashtbl.t;

    (* Propositional variables for symbols (except nullary predicates). *)
    pvars : (Symb.id, pvar BatDynArray.t) Hashtbl.t;

    (* Except nullary predicates. *)
    adeq_sizes : (Symb.id, (int array * commutative)) Hashtbl.t;

    (* Constants and functions. *)
    funcs : Symb.id array;

    clauses : clause array;

    (* Except totality clauses. *)
    max_clause_size : int;

    (* The size of a predicate symbol is its arity,
       the size of a function symbol is its arity + 1.
    *)
    max_symb_size : int;

    (* Minimum max_size (= number of the distinct constants). *)
    min_size : int;

    (* Each totality clause contains this variable (positively),
       so totality clauses can be turned off by setting this variable to true.
    *)
    mutable totality_clauses_switch : pvar option;

    (* Current maximal domain size. *)
    mutable max_size : int;

    (* Cells assigned by symmetry reduction. Each cell is represented
       by a triple (symb_id, rank, max_el).
       Constants have rank = 0 and max_el = -1.
    *)
    assig_by_symred : (Symb.id * int * int, unit) Hashtbl.t;

    mutable can_construct_model : bool;
  }

  let create prob sorts =
    let symred = Symred.create prob sorts in
    let solver = Solv.create () in
    let symbols = prob.Prob.symbols in

    (* Create propositional variables for nullary predicates. *)
    let nullary_pred_pvars = Hashtbl.create 20 in
    Hashtbl.iter
      (fun symb sorts ->
        if Array.length sorts = 0 then
          Hashtbl.add nullary_pred_pvars symb (Solv.new_var solver))
      sorts.Sorts.symb_sorts;

    (* Prepare hashtable with propositional variables of symbols. *)
    let pvars = Hashtbl.create 20 in
    Hashtbl.iter
      (fun symb sorts ->
        if Array.length sorts > 0 then
          Hashtbl.add pvars symb (BatDynArray.create ()))
      sorts.Sorts.symb_sorts;

    let adeq_sizes = Hashtbl.create 20 in
    Hashtbl.iter
      (fun symb sorts' ->
        if Array.length sorts' > 0 then
          let adeq_sizes' =
            Array.map (fun sort -> sorts.Sorts.adeq_sizes.(sort)) sorts' in
          let commutative = Symb.commutative prob.Prob.symbols symb in
          Hashtbl.add adeq_sizes symb (adeq_sizes', commutative))
      sorts.Sorts.symb_sorts;

    let funcs = BatDynArray.create () in
    Hashtbl.iter
      (fun symb sorts ->
        if Array.length sorts = Symb.arity prob.Prob.symbols symb + 1 then
          BatDynArray.add funcs symb)
      sorts.Sorts.symb_sorts;

    let each_clause cl =
      let nullary_pred_lits = BatDynArray.create () in
      let var_eqs = BatDynArray.create () in
      let lits = BatDynArray.create () in
      let each_lit lit =
        let sign = ref Solv.Pos in
        let get_var = function
          | Term.Var x -> x
          | _ -> failwith "expected variable" in
        let each_atom = function
          | Term.Var _ -> failwith "invalid atom"
          | Term.Func (s, [| l; r |]) when s = Symb.sym_eq ->
              begin match l, r with
                | Term.Var x, Term.Var y ->
                    if !sign <> Solv.Pos then failwith "literal is not flat";
                    BatDynArray.add var_eqs (x, y)
                | Term.Func (f, args), Term.Var res
                | Term.Var res, Term.Func (f, args) ->
                    BatDynArray.add lits
                      {
                        l_sign = !sign;
                        l_pvars = Hashtbl.find pvars f;
                        l_vars =
                          Array.init
                            (Array.length args + 1)
                            (fun i ->
                              if i < Array.length args
                              then get_var args.(i)
                              else res);
                        l_commutative = Symb.commutative prob.Prob.symbols f;
                        l_adeq_sizes =
                          Array.map
                            (fun sort -> sorts.Sorts.adeq_sizes.(sort))
                            (Hashtbl.find sorts.Sorts.symb_sorts f);
                      }
                | _, _ -> failwith "literal is not flat"
              end
          | Term.Func (p, args) ->
              (* Nullary predicate. *)
              if args = [| |] then
                let pvar = Hashtbl.find nullary_pred_pvars p in
                let plit = Solv.to_lit !sign pvar in
                BatDynArray.add nullary_pred_lits plit
              else
                BatDynArray.add lits
                  {
                    l_sign = !sign;
                    l_pvars = Hashtbl.find pvars p;
                    l_vars = Array.map get_var args;
                    l_commutative = Symb.commutative prob.Prob.symbols p;
                    l_adeq_sizes =
                      Array.map
                        (fun sort -> sorts.Sorts.adeq_sizes.(sort))
                        (Hashtbl.find sorts.Sorts.symb_sorts p);
                  } in
        match lit with
          | Term.Var _ -> failwith "invalid literal"
          | Term.Func (s, [| atom |]) when s = Symb.sym_not ->
              sign := Solv.Neg;
              each_atom atom
          | atom -> each_atom atom in
      List.iter each_lit cl.Clause.cl_lits;
      let _, nvars = Clause.normalize_vars cl in
      {
        var_adeq_sizes =
          Array.init nvars
            (fun v ->
              let sort =
                Hashtbl.find
                  sorts.Sorts.var_sorts
                  (cl.Clause.cl_id, v) in
              sorts.Sorts.adeq_sizes.(sort));
        var_equalities = BatDynArray.to_array var_eqs;
        nullary_pred_lits = BatDynArray.to_array nullary_pred_lits;
        lits = BatDynArray.to_array lits;
      } in
    let clauses = BatDynArray.map each_clause prob.Prob.clauses in

    (* Instantiate clauses without variables. *)
    BatDynArray.keep
      (fun cl ->
        if Array.length cl.var_adeq_sizes = 0 then begin
          ignore (Solv.add_clause
                    solver
                    cl.nullary_pred_lits
                    (Array.length cl.nullary_pred_lits));
          false
        end else
          true)
      clauses;

    (* 2 for "at most one value" clauses. *)
    let max_clause_size =
      BatDynArray.fold_left
        (fun m cl ->
          max m (Array.length cl.lits + Array.length cl.nullary_pred_lits))
        2 clauses in

    let max_symb_size =
      Hashtbl.fold
        (fun _ sorts acc -> max (Array.length sorts) acc)
        sorts.Sorts.symb_sorts
        0 in

    {
      symred;
      solver;
      symbols;
      nullary_pred_pvars;
      pvars;
      adeq_sizes;
      funcs = BatDynArray.to_array funcs;
      clauses = BatDynArray.to_array clauses;
      max_clause_size;
      max_symb_size;
      min_size = BatDynArray.length prob.Prob.distinct_consts;
      totality_clauses_switch = None;
      max_size = 0;
      assig_by_symred = Hashtbl.create 50;
      can_construct_model = false;
    }

  (* Add propositional variables for predicate and function symbols. *)
  let add_prop_vars inst =
    Hashtbl.iter
      (fun symb (adeq_sizes, commutative) ->
        let cnt =
          let count =
            if commutative
            then Assignment.count_comm_me
            else Assignment.count_me in
          count 0 (Array.length adeq_sizes) adeq_sizes inst.max_size in
        if cnt > 0 then begin
          let pvars = Hashtbl.find inst.pvars symb in
          BatDynArray.add pvars (Solv.new_var inst.solver);
          for i = 2 to cnt do
            ignore (Solv.new_var inst.solver)
          done
        end)
      inst.adeq_sizes

  let symmetry_reduction inst pclause =
    let assigned_cells = Symred.incr_max_size inst.symred in
    List.iter
      (fun ((symb, args), (lo, hi)) ->
        let adeq_sizes, commutative = Hashtbl.find inst.adeq_sizes symb in
        let pvars = Hashtbl.find inst.pvars symb in
        let arity = Array.length args in
        let rank =
          if commutative
          then Assignment.rank_comm_me
          else Assignment.rank_me in
        let a = Array.copy adeq_sizes in
        Array.blit args 0 a 0 arity;
        (* Create literals. *)
        for result = lo to hi do
          a.(arity) <- result;
          let r, max_el_idx = rank a 0 (arity+1) adeq_sizes in
          let pvar = r + BatDynArray.get pvars a.(max_el_idx) in
          let plit = Solv.to_lit Solv.Pos pvar in
          pclause.(result - lo) <- plit
        done;
        ignore (Solv.add_symmetry_clause inst.solver pclause (hi - lo + 1));

        (* Mark cell as assigned by symmetry reduction. *)
        if Array.length args = 0 then
          Hashtbl.add inst.assig_by_symred (symb, 0, -1) ()
        else
          let r, max_el_idx = rank args 0 (Array.length args) adeq_sizes in
          Hashtbl.add inst.assig_by_symred (symb, r, args.(max_el_idx)) ()
      )
      assigned_cells

  let add_at_most_one_val_clauses inst pclause =
    Array.iter
      (fun f ->
        let adeq_sizes, commutative = Hashtbl.find inst.adeq_sizes f in
        let pvars = Hashtbl.find inst.pvars f in
        let arity = Array.length adeq_sizes - 1 in
        let res_max_el =
          if
            adeq_sizes.(arity) = 0 ||
            adeq_sizes.(arity) >= inst.max_size
          then inst.max_size - 1
          else adeq_sizes.(arity) - 1 in
        let each, rank =
          if commutative
          then Assignment.each_comm_me, Assignment.rank_comm_me
          else Assignment.each_me, Assignment.rank_me in
        let a = Array.copy adeq_sizes in
        let mk_lit a =
          let r, max_el_idx = rank a 0 (arity+1) adeq_sizes in
          let pvar = r + BatDynArray.get pvars a.(max_el_idx) in
          Solv.to_lit Solv.Neg pvar in

        (* Points without maximal element. *)
        if res_max_el = inst.max_size - 1 then
          let proc_arg_vec a =
            (* The first result is always the maximal element. *)
            a.(arity) <- res_max_el;
            pclause.(0) <- mk_lit a;
            (* The second result. *)
            for result = 0 to res_max_el - 1 do
              a.(arity) <- result;
              pclause.(1) <- mk_lit a;
              ignore (Solv.add_at_most_one_val_clause inst.solver pclause)
            done in
          (* Constants are processed separately since both each_me
             and each_comm_me don't produce any assignment.
          *)
          if arity = 0 then
            proc_arg_vec a
          else
            for max_size = 1 to inst.max_size - 1 do
              each a 0 arity adeq_sizes max_size proc_arg_vec
            done;
        (* Points with maximal element. *)
        each a 0 arity adeq_sizes inst.max_size
          (fun a ->
            for result = 0 to res_max_el - 1 do
              a.(arity) <- result;
              pclause.(0) <- mk_lit a;
              for result2 = result + 1 to res_max_el do
                a.(arity) <- result2;
                pclause.(1) <- mk_lit a;
                ignore (Solv.add_at_most_one_val_clause inst.solver pclause)
              done
            done))
      inst.funcs

  let instantiate_clauses inst pclause =
    (* Array where the arguments and the result (if appropriate) are stored
       before they are ranked.
    *)
    let symb_elems = Array.make inst.max_symb_size 0 in

    Array.iter
      (fun cl ->
        let nullary_preds_cnt = Array.length cl.nullary_pred_lits in
        Array.blit cl.nullary_pred_lits 0 pclause 0 nullary_preds_cnt;
        let a = Array.copy cl.var_adeq_sizes in
        (* For each assignment of the variables with max_el. *)
        Assignment.each_me
          a 0 (Array.length cl.var_adeq_sizes)
          cl.var_adeq_sizes inst.max_size
          (fun a ->
            let var_eq_sat =
              BatArray.exists
                (fun (x, y) -> a.(x) = a.(y))
                cl.var_equalities in
            if not var_eq_sat then begin
              (* Add remaining literals. *)
              Array.iteri
                (fun i lit ->
                  (* Copy values of variables into symb_elems. *)
                  Array.iteri
                    (fun j x -> symb_elems.(j) <- a.(x))
                    lit.l_vars;
                  let rank =
                    if lit.l_commutative
                    then Assignment.rank_comm_me
                    else Assignment.rank_me in
                  let r, max_el_idx =
                    rank
                      symb_elems 0 (Array.length lit.l_vars)
                      lit.l_adeq_sizes in
                  let pvar =
                    r + BatDynArray.get lit.l_pvars symb_elems.(max_el_idx) in
                  let plit = Solv.to_lit lit.l_sign pvar in
                  pclause.(i + nullary_preds_cnt) <- plit)
                cl.lits;
              ignore
                (Solv.add_clause
                   inst.solver pclause
                   (nullary_preds_cnt + Array.length cl.lits))
            end))
      inst.clauses

  let incr_max_size inst =
    inst.max_size <- inst.max_size + 1;
    inst.can_construct_model <- false;

    add_prop_vars inst;

    (* Disable old "at least one value" clauses. *)
    begin match inst.totality_clauses_switch with
      | None -> ()
      | Some pvar ->
          let plit = Solv.to_lit Solv.Pos pvar in
          Solv.remove_clauses_with_lit inst.solver plit;
          inst.totality_clauses_switch <- None;
    end;

    (* Array where the propositional literals are stored
       before they are added as a new clause.
    *)
    let pclause =
      Array.make
        (* inst.max_size is for symmetry clauses. *)
        (max inst.max_clause_size inst.max_size)
        (Solv.to_lit Solv.Pos 0) in

    symmetry_reduction inst pclause;
    add_at_most_one_val_clauses inst pclause;
    instantiate_clauses inst pclause

  let add_at_least_one_val_clauses inst =
    if inst.totality_clauses_switch = None then begin
      let switch = Solv.new_false_var inst.solver in
      inst.totality_clauses_switch <- Some switch;

      let pclause =
        Array.make
          (* + 1 is for the switch. *)
          (inst.max_size + 1)
          (Solv.to_lit Solv.Pos 0) in

      Array.iter
        (fun f ->
          let adeq_sizes, commutative = Hashtbl.find inst.adeq_sizes f in
          let pvars = Hashtbl.find inst.pvars f in
          let arity = Array.length adeq_sizes - 1 in
          let res_max_el =
            if
              adeq_sizes.(arity) = 0 ||
              adeq_sizes.(arity) >= inst.max_size
            then inst.max_size - 1
            else adeq_sizes.(arity) - 1 in
          let each, rank =
            if commutative
            then Assignment.each_comm_me, Assignment.rank_comm_me
            else Assignment.each_me, Assignment.rank_me in
          (* Process argument vector. *)
          let proc_arg_vec a =
            let assig_by_symred =
              if arity = 0 then
                Hashtbl.mem inst.assig_by_symred (f, 0, -1)
              else
                let r, max_el_idx = rank a 0 arity adeq_sizes in
                Hashtbl.mem inst.assig_by_symred (f, r, a.(max_el_idx)) in
            (* Skip cells assigned by symmetry reduction. *)
            if not assig_by_symred then begin
              for result = 0 to res_max_el  do
                a.(arity) <- result;
                let r, max_el_idx = rank a 0 (arity + 1) adeq_sizes in
                let pvar = r + BatDynArray.get pvars a.(max_el_idx) in
                let plit = Solv.to_lit Solv.Pos pvar in
                pclause.(result) <- plit
              done;
              pclause.(res_max_el + 1) <- Solv.to_lit Solv.Pos switch;
              ignore (Solv.add_at_least_one_val_clause
                        inst.solver
                        pclause
                        (res_max_el + 2))
            end in

          let a = Array.copy adeq_sizes in

          (* Constants are processed separately since both each_me
             and each_comm_me don't produce any assignment.
          *)
          if arity = 0 then
            proc_arg_vec a
          else
            for max_size = 1 to inst.max_size do
              each a 0 arity adeq_sizes max_size proc_arg_vec
            done)
        inst.funcs
    end

  let solve inst =
    if inst.max_size < 1 then
      failwith "solve: max_size must be at least 1";
    if inst.max_size < inst.min_size then
      failwith "solve: max_size is too small";
    add_at_least_one_val_clauses inst;
    match inst.totality_clauses_switch with
      | None -> failwith "solve: impossible"
      | Some switch ->
          let result =
            Solv.solve inst.solver [| Solv.to_lit Solv.Neg switch |] in
          inst.can_construct_model <- result = Solv.Ltrue;
          result

  let construct_model inst =
    if not inst.can_construct_model then
      failwith "construct_model: no model";

    let get_val pvar =
      match Solv.model_value inst.solver pvar with
        | Solv.Ltrue -> 1
        | Solv.Lfalse -> 0
        | Solv.Lundef ->
            failwith "construct_model: unassigned propositional variable" in

    let model = {
      Ms_model.max_size = inst.max_size;
      Ms_model.symbs = Hashtbl.create 10;
    } in

    (* Nullary predicates. *)
    Hashtbl.iter
      (fun s pvar ->
        if not (Symb.auxiliary inst.symbols s) then
          Hashtbl.add model.Ms_model.symbs s
            {
              Ms_model.param_sizes = [| |];
              Ms_model.values = [| get_val pvar |];
            })
      inst.nullary_pred_pvars;

    (* Functions, constants, non-nullary predicates. *)
    Hashtbl.iter
      (fun s (adeq_sizes, commutative) ->
        let arity = Symb.arity inst.symbols s in
        if not (Symb.auxiliary inst.symbols s) || arity = 0 then begin
          let dsize i =
            if
              adeq_sizes.(i) = 0 ||
              adeq_sizes.(i) >= inst.max_size
            then inst.max_size
            else adeq_sizes.(i) in
          let i = ref 0 in
          let values =
            Array.make
              (Assignment.count 0 arity adeq_sizes inst.max_size)
              ~-1 in
          Hashtbl.add model.Ms_model.symbs s
            {
              Ms_model.param_sizes = Array.init arity dsize;
              Ms_model.values;
            };
          let pvars = Hashtbl.find inst.pvars s in
          let a = Array.copy adeq_sizes in
          let rank =
            if commutative
            then Assignment.rank_comm_me
            else Assignment.rank_me in
          if arity = Array.length adeq_sizes then
            (* Non-nullary predicate. *)
            Assignment.each a 0 arity adeq_sizes inst.max_size
              (fun a ->
                let r, max_el_idx = rank a 0 arity adeq_sizes in
                let pvar = r + BatDynArray.get pvars a.(max_el_idx) in
                values.(!i) <- get_val pvar;
                incr i)
          else
            (* Function or constant. *)
            let res_max_el = dsize arity - 1 in
            Assignment.each a 0 arity adeq_sizes inst.max_size
              (fun a ->
                let result = ref ~-1 in
                (* Optimization: The loop can be interrupted
                   when the result is found.
                *)
                for res = 0 to res_max_el do
                  a.(arity) <- res;
                  let r, max_el_idx = rank a 0 (arity+1) adeq_sizes in
                  let pvar = r + BatDynArray.get pvars a.(max_el_idx) in
                  if get_val pvar = 1 then
                    if !result = ~-1 then
                      result := res
                    else
                      failwith "construct_model: function with more values"
                done;
                if !result = ~- 1 then
                  failwith "construct_model: function with no value";
                values.(!i) <- !result;
                incr i)
        end)
      inst.adeq_sizes;

    model

  let get_solver inst = inst.solver

  let get_max_size inst = inst.max_size

end