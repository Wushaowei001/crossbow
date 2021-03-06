(* Copyright (c) 2013 Radek Micek *)

module Array = Earray.Array
module S = Symb

type table = {
  param_sizes : (int, [`R]) Earray.t;
  values : (int, [`R]) Earray.t;
}

type t = {
  max_size : int;
  symbs : table Symb.Map.t;
}

let equal a b =
  a.max_size = b.max_size &&
  Symb.Map.equal (=) a.symbs b.symbs

let compare a b =
  let r = compare a.max_size b.max_size in
  if r = 0 then
    Symb.Map.compare compare a.symbs b.symbs
  else
    r

let canonize model sorts =
  let max_size = model.max_size in
  let nsorts = Earray.length sorts.Sorts.adeq_sizes in
  let dsize s =
    if
      sorts.Sorts.adeq_sizes.(s) = 0 ||
      sorts.Sorts.adeq_sizes.(s) >= max_size
    then max_size
    else sorts.Sorts.adeq_sizes.(s) in
  let g = Bliss.create_graph () in
  let used_colors = ref 0 in
  let dom_elems_cnt = ref 0 in

  (* Create vertices for domain elements. *)
  let sort_to_vert =
    Earray.init
      nsorts
      (fun sort ->
        let v = Bliss.add_vertex g !used_colors in
        for i = 2 to dsize sort do
          ignore (Bliss.add_vertex g !used_colors)
        done;
        incr used_colors;
        dom_elems_cnt := !dom_elems_cnt + dsize sort;
        v) in

  (* Get vertex for value v from the sort s. *)
  let vert s v = sort_to_vert.(s) + v in

  Symb.Map.iter
    (fun symb table ->
      let arity = Symb.arity symb in
      let kind = Symb.kind symb in
      let sorts' = Hashtbl.find sorts.Sorts.symb_sorts symb in
      let i = ref 0 in
      let a = Earray.copy sorts' in
      (* Colors for the vertices of the symbol. *)
      let colors =
        Earray.init
          (Earray.length sorts')
          (fun _ -> let c = !used_colors in incr used_colors; c) in
      (* For vertices for the symbol (for its parameters and result). *)
      let vs = Earray.copy sorts' in
      match arity, kind with
        | 0, S.Pred -> ()
        | _, S.Func ->
            Assignment.each a 0 arity table.param_sizes max_size
              (fun a ->
                (* Add vertices for parameters and result. *)
                Earray.iteri
                  (fun j _ -> vs.(j) <- Bliss.add_vertex g colors.(j))
                  vs;
                (* Connect parameters to values. *)
                for j = 0 to arity - 1 do
                  Bliss.add_edge g vs.(j) (vert sorts'.(j) a.(j))
                done;
                (* Connect result to value *)
                Bliss.add_edge
                  g
                  vs.(arity)
                  (vert sorts'.(arity) table.values.(!i));
                incr i;
                (* Connect parameters and result together. *)
                for j = 1 to arity do
                  Bliss.add_edge g vs.(j-1) vs.(j)
                done)
        | _, S.Pred ->
            Assignment.each a 0 arity table.param_sizes max_size
              (fun a ->
                if table.values.(!i) = 1 then begin
                  (* Add vertices for parameters. *)
                  Earray.iteri
                    (fun j _ -> vs.(j) <- Bliss.add_vertex g colors.(j))
                    vs;
                  (* Connect parameters to values. *)
                  for j = 0 to arity - 1 do
                    Bliss.add_edge g vs.(j) (vert sorts'.(j) a.(j))
                  done;
                  (* Connect parameters together. *)
                  for j = 1 to arity - 1 do
                    Bliss.add_edge g vs.(j-1) vs.(j)
                  done;
                end;
                incr i))
    model.symbs;

  let lab = Earray.of_array (Bliss.canonical_form g !dom_elems_cnt) in
  (* Renaming to canonical form. *)
  let renaming =
    Earray.init
      nsorts
      (fun sort ->
        Earray.init
          (dsize sort)
          (fun i ->
            let dest_vert = lab.(vert sort i) in
            let dest_value = dest_vert - sort_to_vert.(sort) in
            dest_value)) in
  (* Renaming from canonical form to original. *)
  let inv_renaming =
    Earray.init
      nsorts
      (fun sort ->
        let inv = Earray.copy renaming.(sort) in
        for i = 0 to Earray.length inv - 1 do
          inv.(renaming.(sort).(i)) <- i
        done;
        inv) in

  let rank param_sizes args =
    let r = ref 0 in
    Earray.iter2
      (fun size arg -> r := !r * size + arg)
      param_sizes
      args;
    !r in

  let symbs' =
    Symb.Map.mapi
      (fun symb table ->
        let arity = Symb.arity symb in
        let kind = Symb.kind symb in
        let sorts' = Hashtbl.find sorts.Sorts.symb_sorts symb in
        match arity, kind with
          | 0, S.Pred -> table
          | _, _ ->
              let i = ref 0 in
              let a = Earray.make arity 0 in
              let a2 = Earray.make arity 0 in
              let new_values = Earray.copy table.values in
              Assignment.each a 0 arity table.param_sizes max_size
                (fun a ->
                  (* Get original result. *)
                  Earray.iteri
                    (fun j v -> a2.(j) <- inv_renaming.(sorts'.(j)).(v))
                    a;
                  let orig_result =
                    table.values.(rank table.param_sizes a2) in

                  let result =
                    if kind = S.Pred then
                      orig_result
                    else
                      renaming.(sorts'.(arity)).(orig_result) in
                  new_values.(!i) <- result;
                  incr i);
              { table with values = Earray.read_only new_values })
      model.symbs in

  { model with symbs = symbs' }
