(* Example of datatype replication from the Definition of Standard ML. *)
signature MYBOOL =
sig
    type bool
    val xor: bool * bool -> bool
end;

structure MyBool: MYBOOL =
struct
    datatype bool = datatype bool (* from the inital basis *)
    fun xor (true, false) = true
      | xor (false, true) = true
      | xor _ = false
end;
val x = MyBool.xor(true, false);
